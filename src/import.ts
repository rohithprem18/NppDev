// Problem import: from a link (Rust fetches and parses the page) or from the Competitive Companion
// browser extension. Either way NppDev creates the solution file and fills the Run panel's tests.
import { create } from "zustand";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { useApp } from "./store";
import { useRun } from "./run";
import { api, joinPath } from "./lib/api";
import { LANGS, fileStem, solutionText } from "./lib/templates";

export interface Problem {
  name: string;
  group: string;
  url: string;
  tests: { input: string; output: string }[];
  timeLimit: number;
  memoryLimit: number;
  interactive: boolean;
  functionBased: boolean;
  starter: Record<string, string>;
}

export const SITES = ["Codeforces", "AtCoder", "CodeChef", "LeetCode", "NeetCode", "GeeksforGeeks", "HackerRank", "HackerEarth"];

interface ImportState {
  open: boolean;
  busy: boolean;
  error: string | null;
  port: number | null;
  show(open: boolean): void;
  fromUrl(url: string, lang: string): Promise<void>;
}

/** Create (or reuse) the file for a problem in the open folder, open it, and load its tests. */
export async function addProblem(p: Problem, langId: string): Promise<boolean> {
  const app = useApp.getState();
  if (!app.folder) {
    await app.openFolder();
    if (!useApp.getState().folder) {
      app.notify("Open a folder first: the solution file is created there.", "error");
      return false;
    }
  }
  const folder = useApp.getState().folder!;
  const lang = LANGS.find((l) => l.id === langId) ?? LANGS[0];
  const stem = fileStem(p.name, lang);
  const path = joinPath(folder, `${stem}.${lang.ext}`);
  const existed = await api.pathExists(path);
  if (!existed) await api.writeText(path, solutionText(lang, stem, p));
  // tests are saved first, so opening the file loads them
  await invoke("save_tests", {
    path,
    data: {
      tests: p.tests.map((t) => ({ input: t.input, expected: t.output })),
      timeLimitMs: p.timeLimit || 2000,
      memoryLimitMb: p.memoryLimit || 256,
      problem: { name: p.name, url: p.url, group: p.group },
    },
  });
  app.refreshTree();
  await app.openFile(path);
  await useRun.getState().load(path, true);
  useRun.getState().toggle(true);
  const n = p.tests.length;
  app.notify(`${existed ? "Updated" : "Imported"} ${p.name}: ${n} test${n === 1 ? "" : "s"}${p.interactive ? " (interactive problem: samples may not apply)" : ""}`);
  return true;
}

export const useImport = create<ImportState>((set) => ({
  open: false,
  busy: false,
  error: null,
  port: null,
  show: (open) => set({ open, error: null }),
  async fromUrl(url, lang) {
    set({ busy: true, error: null });
    try {
      const p = await invoke<Problem>("import_problem", { url });
      useApp.getState().setDefaultLang(lang);
      if (await addProblem(p, lang)) set({ open: false });
    } catch (e) {
      set({ error: String(e) });
    } finally {
      set({ busy: false });
    }
  },
}));

// Competitive Companion: problems arrive while NppDev runs, in the remembered language
invoke<number | null>("companion_port").then((port) => useImport.setState({ port })).catch(() => {});
listen<Problem>("problem-received", ({ payload }) => {
  useImport.setState({ open: false });
  addProblem(payload, useApp.getState().defaultLang).catch((e) => useApp.getState().notify(String(e), "error"));
});
