// App state (zustand): folder, tabs, theme, recent folders, dialogs. Persisted to settings.json.
import { create } from "zustand";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { open as openDialog, save as saveDialog } from "@tauri-apps/plugin-dialog";
import { api, baseName } from "./lib/api";
import * as ws from "./lib/workspace";
import { fileType } from "./lib/fileTypes";
// IntelliSense and snippets need Monaco: loaded on first use, not at startup
const lsp = () => import("./lib/lsp");
const snippets = () => import("./lib/snippets");

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
  /** A release the user chose to skip in the update box. */
  skippedVersion: string | null;
  editorFont: string;
  /** Save files by themselves a moment after typing stops (on by default). */
  autoSave: boolean;
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
  /** What the sidebar shows. */
  sideView: "explorer" | "settings";
  treeVersion: number;                 // bump to make the Explorer reload
  toast: { text: string; kind: "info" | "error" } | null;
  confirm: Confirm | null;

  init(): Promise<void>;
  /** Files and folders from "Open with NppDev": folders open as the workspace, files as tabs. */
  openPaths(paths: string[]): Promise<void>;
  setTheme(t: Theme): void;
  openFolder(path?: string): Promise<void>;
  closeFolder(): Promise<void>;
  openFile(path: string): Promise<void>;
  newFile(): Promise<void>;
  activate(path: string): void;
  closeTab(path: string): Promise<boolean>;
  save(path?: string, quiet?: boolean): Promise<void>;
  saveAs(path?: string): Promise<void>;
  saveAll(): Promise<void>;
  pathRenamed(from: string, to: string): void;
  pathDeleted(path: string): void;
  refreshTree(): void;
  notify(text: string, kind?: "info" | "error"): void;
  ask(c: Omit<Confirm, "resolve">): Promise<string>;
  toggleSidebar(): void;
  /** Show this sidebar view; clicking the one already shown hides the sidebar. */
  showSide(view: "explorer" | "settings"): void;
  setEditorFont(font: string): void;
  setAutoSave(on: boolean): void;
  setSidebarWidth(w: number): void;
  setDefaultLang(id: string): void;
  setFontSize(size: number): void;
  setSkippedVersion(v: string | null): void;
}

const DEFAULTS: Persisted = { theme: "dark", recent: [], lastFolder: null, sessions: {}, sidebarWidth: 260, fontSize: 15, defaultLang: "cpp", skippedVersion: null, editorFont: "Hack", autoSave: true };
let saveTimer = 0;

export const useApp = create<State>((set, get) => {
  /** Settings are written half a second after the last change. */
  const persist = () => {
    clearTimeout(saveTimer);
    saveTimer = window.setTimeout(() => {
      const s = get();
      const out: Persisted = { theme: s.theme, recent: s.recent, lastFolder: s.folder, sessions: s.sessions, sidebarWidth: s.sidebarWidth, fontSize: s.fontSize, defaultLang: s.defaultLang, skippedVersion: s.skippedVersion, editorFont: s.editorFont, autoSave: s.autoSave };
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
    sideView: "explorer",
    treeVersion: 0,
    toast: null,
    confirm: null,

    async init() {
      let saved: Partial<Persisted> = {};
      try { saved = JSON.parse(await api.loadSettings()); } catch { /* first start */ }
      set({ ...DEFAULTS, ...saved, ready: true });
      lsp().then((m) => m.onLspProblem((text) => get().notify(text)));
      // auto save: a second after typing stops, and when leaving the window (named files only)
      const timers = new Map<string, number>();
      const saveSoon = (path: string) => {
        if (!get().autoSave || ws.isUntitled(path)) return;
        clearTimeout(timers.get(path));
        timers.set(path, window.setTimeout(() => { timers.delete(path); if (ws.isDirty(path)) get().save(path, true); }, 1000));
      };
      ws.onEdit(saveSoon);
      window.addEventListener("blur", () => {
        if (!get().autoSave) return;
        for (const t of get().tabs) if (!ws.isUntitled(t.path) && ws.isDirty(t.path)) get().save(t.path, true);
      });
      snippets().then((m) => { m.registerSnippets(); m.loadUserSnippets(); });
      document.documentElement.dataset.theme = get().theme;
      const asked = await invoke<string[]>("startup_paths").catch(() => [] as string[]);
      const isDir = async (p: string) => api.listDir(p).then(() => true, () => false);
      const askedFolder = (await Promise.all(asked.map(isDir))).some(Boolean);
      if (!askedFolder && saved.lastFolder && (await api.pathExists(saved.lastFolder))) await get().openFolder(saved.lastFolder);
      if (asked.length) await get().openPaths(asked);
      listen<string[]>("open-paths", ({ payload }) => { get().openPaths(payload); });
    },

    async openPaths(paths) {
      for (const p of paths) {
        const dir = await api.listDir(p).then(() => true, () => false);
        if (dir) await get().openFolder(p);
        else await get().openFile(p);
      }
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
      if (path !== get().folder) lsp().then((m) => m.stopAllLsp());
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
        const folder = get().folder;
        lsp().then((m) => m.ensureLsp(fileType(baseName(path)).lang, folder));
      }
      get().activate(path);
    },

    async newFile() {
      const path = await ws.newUntitled();
      set((s) => ({ tabs: [...s.tabs, { path, name: path.slice("untitled:".length) }], active: path }));
    },

    activate(path) {
      const prev = get().active;
      if (get().autoSave && prev && prev !== path && !ws.isUntitled(prev) && ws.isDirty(prev)) get().save(prev, true);
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

    async save(path, quiet) {
      path = path ?? get().active ?? undefined;
      if (!path) return;
      if (ws.isUntitled(path)) return quiet ? undefined : get().saveAs(path);
      try { await ws.saveDoc(path); } catch (e) { get().notify(String(e), "error"); return; }
      const sn = await snippets();
      if (path === sn.userSnippetsPath()) {
        const err = await sn.loadUserSnippets();
        if (err || !quiet) get().notify(err ?? "Snippets reloaded.", err ? "error" : "info");
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
    showSide: (view) => set((s) => (s.sidebarVisible && s.sideView === view ? { sidebarVisible: false } : { sidebarVisible: true, sideView: view })),
    setAutoSave(autoSave) {
      set({ autoSave });
      persist();
    },
    setEditorFont(editorFont) {
      set({ editorFont });
      persist();
    },
    setSkippedVersion(skippedVersion) {
      set({ skippedVersion });
      persist();
    },
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
