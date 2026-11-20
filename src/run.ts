// Run panel state for the active file: one Input, the program's Output, and an optional
// Expected output to check it against. Saved per file in the app's data folder.
import { create } from "zustand";
import { invoke } from "@tauri-apps/api/core";
import { useApp } from "./store";
import * as ws from "./lib/workspace";
import { RUNNABLE, fileType } from "./lib/fileTypes";
import { baseName } from "./lib/api";

export type Verdict = "AC" | "WA" | "RE" | "TLE" | "MLE" | "OK" | "STOPPED";

export interface TestResult {
  index: number;
  verdict: Verdict;
  output: string;
  stderr: string;
  timeMs: number;
  memoryKb: number;
  message?: string | null;
}

export interface CompileResult {
  ok: boolean;
  output: string;
  cached: boolean;
  ms: number;
  command: string;
}

interface RunResult {
  compile: CompileResult;
  tests: TestResult[];
  missing?: string | null;
  error?: string | null;
}

/** On disk (same shape as before, so older saved tests still load). */
interface Saved {
  tests: { input: string; expected: string }[];
  timeLimitMs: number;
  memoryLimitMb: number;
}

interface RunState {
  open: boolean;
  width: number;
  path: string | null;
  /** Changes when another file's input is loaded, so the text boxes reset. */
  version: number;
  input: string;
  expected: string;
  result: TestResult | null;
  timeLimitMs: number;
  memoryLimitMb: number;
  running: boolean;
  compile: CompileResult | null;
  missing: string | null;
  error: string | null;

  toggle(open?: boolean): void;
  setWidth(w: number): void;
  load(path: string | null): Promise<void>;
  setInput(text: string): void;
  setExpected(text: string): void;
  setLimits(timeLimitMs: number, memoryLimitMb: number): void;
  run(): Promise<void>;
  stop(): void;
}

export const TOOL_NAMES: Record<string, string> = {
  gpp: "GCC (g++)", gcc: "GCC", javac: "a Java JDK", python: "Python", node: "Node.js", go: "Go", rustc: "Rust", kotlinc: "Kotlin", csc: "a C# compiler",
};

const DEFAULT_TL = 2000;
const DEFAULT_ML = 256;
let saveTimer = 0;

export const canRun = (path: string | null) => !!path && !ws.isUntitled(path) && RUNNABLE.has(fileType(baseName(path)).lang);

export const useRun = create<RunState>((set, get) => {
  const persist = () => {
    clearTimeout(saveTimer);
    const { path, input, expected, timeLimitMs, memoryLimitMb } = get();
    if (!path) return;
    saveTimer = window.setTimeout(() => {
      const empty = !input.trim() && !expected.trim() && timeLimitMs === DEFAULT_TL && memoryLimitMb === DEFAULT_ML;
      const data: Saved | null = empty ? null : { tests: [{ input, expected }], timeLimitMs, memoryLimitMb };
      invoke("save_tests", { path, data }).catch(() => {});
    }, 400);
  };

  return {
    open: false,
    width: 400,
    path: null,
    version: 0,
    input: "",
    expected: "",
    result: null,
    timeLimitMs: DEFAULT_TL,
    memoryLimitMb: DEFAULT_ML,
    running: false,
    compile: null,
    missing: null,
    error: null,

    toggle: (open) => set((s) => ({ open: open ?? !s.open })),
    setWidth: (w) => set({ width: Math.max(300, Math.min(900, w)) }),

    async load(path) {
      if (path === get().path) return;
      if (get().path) persist(); // write the previous file's input now
      set((s) => ({ path, version: s.version + 1, input: "", expected: "", result: null, compile: null, missing: null, error: null, timeLimitMs: DEFAULT_TL, memoryLimitMb: DEFAULT_ML }));
      if (!canRun(path)) return;
      const saved = await invoke<Saved | null>("load_tests", { path }).catch(() => null);
      if (get().path !== path || !saved) return;
      const first = saved.tests?.[0];
      set((s) => ({ version: s.version + 1, input: first?.input ?? "", expected: first?.expected ?? "", timeLimitMs: saved.timeLimitMs ?? DEFAULT_TL, memoryLimitMb: saved.memoryLimitMb ?? DEFAULT_ML }));
    },

    setInput(input) { set({ input }); persist(); },
    setExpected(expected) { set({ expected }); persist(); },
    setLimits(timeLimitMs, memoryLimitMb) {
      set({ timeLimitMs: Math.max(100, Math.min(60000, timeLimitMs || DEFAULT_TL)), memoryLimitMb: Math.max(16, Math.min(8192, memoryLimitMb || DEFAULT_ML)) });
      persist();
    },

    async run() {
      const app = useApp.getState();
      const path = app.active;
      if (!path) return;
      if (ws.isUntitled(path)) {
        await app.saveAs(path);
        return app.notify("Saved. Press Ctrl+Enter again to run.");
      }
      if (!canRun(path)) return app.notify("NppDev can run C, C++, Java, Python, JavaScript, Go, Rust, Kotlin, C# and SQL files.", "error");
      if (get().running) return;
      if (ws.isDirty(path)) await app.save(path);
      await get().load(path);
      const s = get();
      set({ open: true, running: true, result: null, compile: null, missing: null, error: null });
      try {
        const res = await invoke<RunResult>("run_tests", {
          req: { path, tests: [{ input: s.input, expected: s.expected.trim() ? s.expected : null }], timeLimitMs: s.timeLimitMs, memoryLimitMb: s.memoryLimitMb, only: [] },
        });
        if (get().path !== path) return;
        set({ compile: res.compile, missing: res.missing ?? null, error: res.error ?? null, result: res.tests[0] ?? null });
      } catch (e) {
        set({ error: String(e) });
      } finally {
        set({ running: false });
      }
    },

    stop() {
      invoke("stop_run").catch(() => {});
    },
  };
});

// the panel follows the active tab
useApp.subscribe((s, prev) => { if (s.active !== prev.active) useRun.getState().load(s.active); });
