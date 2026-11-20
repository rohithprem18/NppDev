// App state (zustand): folder, tabs, theme, recent folders, dialogs. Persisted to settings.json.
import { create } from "zustand";
import { open as openDialog, save as saveDialog } from "@tauri-apps/plugin-dialog";
import { api, baseName } from "./lib/api";
import * as ws from "./lib/workspace";
import { fileType } from "./lib/fileTypes";
import { ensureLsp, stopAllLsp, onLspProblem } from "./lib/lsp";
import { loadUserSnippets, registerSnippets, userSnippetsPath } from "./lib/snippets";

export type Theme = "dark" | "light";

export interface Tab {
  path: string;
  name: string;
}

interface Session {
  tabs: string[];
  active: string | null;
}

interface Persisted {
  theme: Theme;
  recent: string[];
  lastFolder: string | null;
  sessions: Record<string, Session>;
  sidebarWidth: number;
  fontSize: number;
  defaultLang: string;
}

export interface Confirm {
  title: string;
  message: string;
  buttons: { label: string; value: string; primary?: boolean; danger?: boolean }[];
  resolve: (value: string) => void;
}

interface State extends Persisted {
  ready: boolean;
  folder: string | null;
  tabs: Tab[];
  active: string | null;
  dirty: Record<string, boolean>;
  sidebarVisible: boolean;
  treeVersion: number;                 // bump to make the Explorer reload
  toast: { text: string; kind: "info" | "error" } | null;
  confirm: Confirm | null;
  cursor: { line: number; col: number };

  init(): Promise<void>;
  setTheme(t: Theme): void;
  openFolder(path?: string): Promise<void>;
  closeFolder(): Promise<void>;
  openFile(path: string): Promise<void>;
  newFile(): void;
  activate(path: string): void;
  closeTab(path: string): Promise<boolean>;
  save(path?: string): Promise<void>;
  saveAs(path?: string): Promise<void>;
  saveAll(): Promise<void>;
  pathRenamed(from: string, to: string): void;
  pathDeleted(path: string): void;
  refreshTree(): void;
  notify(text: string, kind?: "info" | "error"): void;
  ask(c: Omit<Confirm, "resolve">): Promise<string>;
  toggleSidebar(): void;
  setCursor(line: number, col: number): void;
  setSidebarWidth(w: number): void;
  setDefaultLang(id: string): void;
  setFontSize(size: number): void;
}

const DEFAULTS: Persisted = { theme: "dark", recent: [], lastFolder: null, sessions: {}, sidebarWidth: 260, fontSize: 15, defaultLang: "cpp" };
let saveTimer = 0;

export const useApp = create<State>((set, get) => {
  /** Settings are written half a second after the last change. */
  const persist = () => {
    clearTimeout(saveTimer);
    saveTimer = window.setTimeout(() => {
      const s = get();
      const out: Persisted = { theme: s.theme, recent: s.recent, lastFolder: s.folder, sessions: s.sessions, sidebarWidth: s.sidebarWidth, fontSize: s.fontSize, defaultLang: s.defaultLang };
      if (s.folder) out.sessions = { ...s.sessions, [s.folder]: { tabs: s.tabs.map((t) => t.path).filter((p) => !ws.isUntitled(p)), active: s.active && !ws.isUntitled(s.active) ? s.active : null } };
      api.saveSettings(JSON.stringify(out, null, 1)).catch(() => {});
    }, 500);
  };

  ws.onDirtyChange((path, d) => set((s) => ({ dirty: { ...s.dirty, [path]: d } })));

  const rememberSession = () => {
    const s = get();
    if (!s.folder) return;
    set({ sessions: { ...s.sessions, [s.folder]: { tabs: s.tabs.map((t) => t.path).filter((p) => !ws.isUntitled(p)), active: s.active } } });
  };

  return {
    ...DEFAULTS,
    ready: false,
    folder: null,
    tabs: [],
    active: null,
    dirty: {},
    sidebarVisible: true,
    treeVersion: 0,
    toast: null,
    confirm: null,
    cursor: { line: 1, col: 1 },

    async init() {
      let saved: Partial<Persisted> = {};
      try { saved = JSON.parse(await api.loadSettings()); } catch { /* first start */ }
      set({ ...DEFAULTS, ...saved, ready: true });
      onLspProblem((text) => get().notify(text));
      registerSnippets();
      loadUserSnippets();
      document.documentElement.dataset.theme = get().theme;
      if (saved.lastFolder && (await api.pathExists(saved.lastFolder))) await get().openFolder(saved.lastFolder);
    },

    setTheme(theme) {
      set({ theme });
      document.documentElement.dataset.theme = theme;
      persist();
    },

    async openFolder(path) {
      if (!path) {
        const picked = await openDialog({ directory: true, title: "Open Folder" });
        if (typeof picked !== "string") return;
        path = picked;
      }
      if (!(await api.pathExists(path))) {
        set((s) => ({ recent: s.recent.filter((r) => r !== path) }));
        get().notify(`This folder no longer exists: ${path}`, "error");
        persist();
        return;
      }
      rememberSession();
      if (path !== get().folder) stopAllLsp();
      // close the old folder's saved tabs (unsaved ones stay open)
      for (const t of get().tabs) if (!ws.isDirty(t.path)) ws.closeDoc(t.path);
      const keep = get().tabs.filter((t) => ws.isDirty(t.path));
      set((s) => ({ folder: path!, tabs: keep, active: keep[0]?.path ?? null, recent: [path!, ...s.recent.filter((r) => r !== path)].slice(0, 12), treeVersion: s.treeVersion + 1 }));
      const session = get().sessions[path];
      if (session) {
        for (const p of session.tabs) if (await api.pathExists(p)) await get().openFile(p).catch(() => {});
        if (session.active && get().tabs.some((t) => t.path === session.active)) get().activate(session.active);
      }
      persist();
    },

    async closeFolder() {
      for (const t of [...get().tabs]) if (!(await get().closeTab(t.path))) return;
      rememberSession();
      set({ folder: null });
      persist();
    },

    async openFile(path) {
      if (!get().tabs.some((t) => t.path === path)) {
        try { await ws.openDoc(path); } catch (e) { get().notify(String(e), "error"); return; }
        set((s) => ({ tabs: [...s.tabs, { path, name: baseName(path) }] }));
        ensureLsp(fileType(baseName(path)).lang, get().folder);
      }
      get().activate(path);
    },

    newFile() {
      const path = ws.newUntitled();
      set((s) => ({ tabs: [...s.tabs, { path, name: path.slice("untitled:".length) }], active: path }));
    },

    activate(path) {
      set({ active: path });
      persist();
    },

    async closeTab(path) {
      if (ws.isDirty(path)) {
        const name = get().tabs.find((t) => t.path === path)?.name ?? path;
        const choice = await get().ask({
          title: "Save changes?",
          message: `${name} has unsaved changes.`,
          buttons: [{ label: "Save", value: "save", primary: true }, { label: "Don't Save", value: "discard", danger: true }, { label: "Cancel", value: "cancel" }],
        });
        if (choice === "cancel") return false;
        if (choice === "save") {
          await get().save(path);
          if (ws.isDirty(path)) return false;
        }
      }
      ws.closeDoc(path);
      set((s) => {
        const i = s.tabs.findIndex((t) => t.path === path);
        const tabs = s.tabs.filter((t) => t.path !== path);
        const dirty = { ...s.dirty };
        delete dirty[path];
        const active = s.active === path ? (tabs[i] ?? tabs[i - 1] ?? null)?.path ?? null : s.active;
        return { tabs, active, dirty };
      });
      persist();
      return true;
    },

    async save(path) {
      path = path ?? get().active ?? undefined;
      if (!path) return;
      if (ws.isUntitled(path)) return get().saveAs(path);
      try { await ws.saveDoc(path); } catch (e) { get().notify(String(e), "error"); return; }
      if (path === userSnippetsPath()) {
        const err = await loadUserSnippets();
        get().notify(err ?? "Snippets reloaded.", err ? "error" : "info");
      }
    },

    async saveAs(path) {
      path = path ?? get().active ?? undefined;
      if (!path) return;
      const target = await saveDialog({ title: "Save As", defaultPath: get().folder ?? undefined });
      if (!target) return;
      try {
        const now = await ws.saveDoc(path, target);
        set((s) => ({
          tabs: s.tabs.map((t) => (t.path === path ? { path: now, name: baseName(now) } : t)),
          active: s.active === path ? now : s.active,
          dirty: { ...s.dirty, [path!]: false, [now]: false },
          treeVersion: s.treeVersion + 1,
        }));
        persist();
      } catch (e) { get().notify(String(e), "error"); }
    },

    async saveAll() {
      for (const t of get().tabs) if (ws.isDirty(t.path)) await get().save(t.path);
    },

    pathRenamed(from, to) {
      for (const p of ws.docsUnder(from)) {
        const np = to + p.slice(from.length);
        ws.renameDoc(p, np);
      }
      set((s) => ({
        tabs: s.tabs.map((t) => (t.path === from || t.path.startsWith(from + "\\") || t.path.startsWith(from + "/") ? { path: to + t.path.slice(from.length), name: baseName(to + t.path.slice(from.length)) } : t)),
        active: s.active && (s.active === from || s.active.startsWith(from + "\\") || s.active.startsWith(from + "/")) ? to + s.active.slice(from.length) : s.active,
      }));
      persist();
    },

    pathDeleted(path) {
      for (const p of ws.docsUnder(path)) ws.closeDoc(p);
      set((s) => {
        const gone = (p: string) => p === path || p.startsWith(path + "\\") || p.startsWith(path + "/");
        const tabs = s.tabs.filter((t) => !gone(t.path));
        return { tabs, active: s.active && gone(s.active) ? tabs[0]?.path ?? null : s.active };
      });
      persist();
    },

    refreshTree: () => set((s) => ({ treeVersion: s.treeVersion + 1 })),

    notify(text, kind = "info") {
      set({ toast: { text, kind } });
      setTimeout(() => { if (get().toast?.text === text) set({ toast: null }); }, kind === "error" ? 6000 : 3000);
    },

    ask(c) {
      return new Promise((resolve) => set({ confirm: { ...c, resolve: (v) => { set({ confirm: null }); resolve(v); } } }));
    },

    toggleSidebar: () => set((s) => ({ sidebarVisible: !s.sidebarVisible })),
    setCursor: (line, col) => set({ cursor: { line, col } }),
    setFontSize(size) {
      set({ fontSize: Math.max(9, Math.min(32, Math.round(size))) });
      persist();
    },
    setDefaultLang(defaultLang) {
      set({ defaultLang });
      persist();
    },
    setSidebarWidth(w) {
      set({ sidebarWidth: Math.max(180, Math.min(600, w)) });
      persist();
    },
  };
});
