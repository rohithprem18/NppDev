// Run panel state: the active file's test cases, limits and results. Tests are saved per file
// in the app's data folder (never in the user's project).
import { create } from "zustand";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { useApp } from "./store";
import * as ws from "./lib/workspace";
import { RUNNABLE, fileType } from "./lib/fileTypes";

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

export interface Test {
  id: number;
  input: string;
  expected: string;
  result?: TestResult;
  pending?: boolean;
}

export interface Problem {
  name: string;
  url?: string;
  group?: string;
}

interface Saved {
  tests: { input: string; expected: string }[];
  timeLimitMs: number;
  memoryLimitMb: number;
  problem?: Problem | null;
}

interface RunState {
  open: boolean;
  width: number;
  path: string | null;
  tests: Test[];
  timeLimitMs: number;
  memoryLimitMb: number;
  problem: Problem | null;
  running: boolean;
  phase: "idle" | "compile" | "test";
  compile: CompileResult | null;
  missing: string | null;
  error: string | null;

  toggle(open?: boolean): void;
  setWidth(w: number): void;
  load(path: string | null): Promise<void>;
  addTest(input?: string, expected?: string): void;
  updateTest(id: number, patch: Partial<Pick<Test, "input" | "expected">>): void;
  removeTest(id: number): void;
  setLimits(timeLimitMs: number, memoryLimitMb: number): void;
  setImported(problem: Problem, tests: { input: string; expected: string }[], timeLimitMs?: number, memoryLimitMb?: number): void;
  run(onlyId?: number): Promise<void>;
  stop(): void;
}

export const TOOL_NAMES: Record<string, string> = {
  gpp: "GCC (g++)", gcc: "GCC", javac: "a Java JDK", python: "Python", node: "Node.js", go: "Go", rustc: "Rust", kotlinc: "Kotlin", csc: "a C# compiler",
};

let nextId = 1;
let saveTimer = 0;
const DEFAULT_TL = 2000;
const DEFAULT_ML = 256;

export const canRun = (path: string | null) => !!path && !ws.isUntitled(path) && RUNNABLE.has(fileType(path).lang);

export const useRun = create<RunState>((set, get) => {
  const persist = () => {
    clearTimeout(saveTimer);
    const { path, tests, timeLimitMs, memoryLimitMb, problem } = get();
    if (!path) return;
    saveTimer = window.setTimeout(() => {
      const empty = tests.every((t) => !t.input.trim() && !t.expected.trim()) && !problem && timeLimitMs === DEFAULT_TL && memoryLimitMb === DEFAULT_ML;
      const data: Saved | null = empty ? null : { tests: tests.map(({ input, expected }) => ({ input, expected })), timeLimitMs, memoryLimitMb, problem };
      invoke("save_tests", { path, data }).catch(() => {});
    }, 400);
  };

  // live results while a run is going
  listen<{ phase: "compile" | "test" | "result"; index?: number; result?: TestResult }>("run-progress", ({ payload }) => {
    if (payload.phase === "compile") set({ phase: "compile" });
    else if (payload.phase === "test") set({ phase: "test" });
    else if (payload.result) {
      const r = payload.result;
      set((s) => ({ tests: s.tests.map((t, i) => (i === r.index ? { ...t, result: r, pending: false } : t)) }));
    }
  });

  return {
    open: false,
    width: 400,
    path: null,
    tests: [],
    timeLimitMs: DEFAULT_TL,
    memoryLimitMb: DEFAULT_ML,
    problem: null,
    running: false,
    phase: "idle",
    compile: null,
    missing: null,
    error: null,

    toggle: (open) => set((s) => ({ open: open ?? !s.open })),
    setWidth: (w) => set({ width: Math.max(300, Math.min(900, w)) }),

    async load(path) {
      if (path === get().path) return;
      clearTimeout(saveTimer);
      if (get().path) persist(); // flush the previous file's edits
      set({ path, tests: [], compile: null, missing: null, error: null, problem: null, timeLimitMs: DEFAULT_TL, memoryLimitMb: DEFAULT_ML });
      if (!canRun(path)) return;
      const saved = (await invoke<Saved | null>("load_tests", { path }).catch(() => null)) ?? null;
      if (get().path !== path) return;
      const tests = (saved?.tests?.length ? saved.tests : [{ input: "", expected: "" }]).map((t) => ({ id: nextId++, input: t.input ?? "", expected: t.expected ?? "" }));
      set({ tests, timeLimitMs: saved?.timeLimitMs ?? DEFAULT_TL, memoryLimitMb: saved?.memoryLimitMb ?? DEFAULT_ML, problem: saved?.problem ?? null });
    },

    addTest(input = "", expected = "") {
      set((s) => ({ tests: [...s.tests, { id: nextId++, input, expected }] }));
      persist();
    },
    updateTest(id, patch) {
      set((s) => ({ tests: s.tests.map((t) => (t.id === id ? { ...t, ...patch } : t)) }));
      persist();
    },
    removeTest(id) {
      set((s) => {
        const tests = s.tests.filter((t) => t.id !== id);
        return { tests: tests.length ? tests : [{ id: nextId++, input: "", expected: "" }] };
      });
      persist();
    },
    setLimits(timeLimitMs, memoryLimitMb) {
      set({ timeLimitMs: Math.max(100, Math.min(60000, timeLimitMs || DEFAULT_TL)), memoryLimitMb: Math.max(16, Math.min(8192, memoryLimitMb || DEFAULT_ML)) });
      persist();
    },
    setImported(problem, tests, timeLimitMs, memoryLimitMb) {
      set({ problem, tests: tests.map((t) => ({ id: nextId++, ...t })), timeLimitMs: timeLimitMs ?? DEFAULT_TL, memoryLimitMb: memoryLimitMb ?? DEFAULT_ML, compile: null, missing: null, error: null });
      persist();
    },

    async run(onlyId) {
      const app = useApp.getState();
      const path = app.active;
      if (!path) return;
      if (ws.isUntitled(path)) {
        await app.saveAs(path);
        return app.notify("Saved. Press Ctrl+Enter again to run.");
      }
      if (!canRun(path)) return app.notify("NppDev can run C, C++, Java, Python, JavaScript, Go, Rust, Kotlin and C# files.", "error");
      if (get().running) return;
      if (ws.isDirty(path)) await app.save(path);
      await get().load(path);
      const s = get();
      const only = onlyId === undefined ? [] : [s.tests.findIndex((t) => t.id === onlyId)].filter((i) => i >= 0);
      set({
        open: true, running: true, phase: "compile", compile: null, missing: null, error: null,
        tests: s.tests.map((t, i) => (only.length === 0 || only.includes(i) ? { ...t, result: undefined, pending: true } : t)),
      });
      try {
        const res = await invoke<RunResult>("run_tests", {
          req: { path, tests: s.tests.map((t) => ({ input: t.input, expected: t.expected || null })), timeLimitMs: s.timeLimitMs, memoryLimitMb: s.memoryLimitMb, only },
        });
        if (get().path !== path) return;
        set((st) => ({
          compile: res.compile, missing: res.missing ?? null, error: res.error ?? null,
          tests: st.tests.map((t, i) => ({ ...t, pending: false, result: res.tests.find((r) => r.index === i) ?? t.result })),
        }));
      } catch (e) {
        set({ error: String(e) });
      } finally {
        set((st) => ({ running: false, phase: "idle", tests: st.tests.map((t) => ({ ...t, pending: false })) }));
      }
    },

    stop() {
      invoke("stop_run").catch(() => {});
    },
  };
});

// the panel follows the active tab
useApp.subscribe((s, prev) => { if (s.active !== prev.active) useRun.getState().load(s.active); });
