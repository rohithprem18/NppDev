// Everything the Command Palette (Ctrl+Shift+P) can do.
import { useApp } from "../store";
import { useRun } from "../run";
import type { monaco } from "./editor";
import { openUserSnippets } from "./snippets";
import { checkForUpdate } from "./update";

export interface Command {
  id: string;
  label: string;
  keys?: string;
  run(): void;
}

const editor = () => (window as unknown as { nppdevEditor?: monaco.editor.IStandaloneCodeEditor }).nppdevEditor;
/** Run one of Monaco's own editor actions (on the open file). */
const action = (id: string) => () => {
  const ed = editor();
  if (!ed?.getModel()) return useApp.getState().notify("Open a file first.");
  ed.focus();
  ed.getAction(id)?.run();
};
const app = () => useApp.getState();

export function allCommands(): Command[] {
  return [
    { id: "run", label: "Run", keys: "Ctrl+Enter", run: () => useRun.getState().run() },
    { id: "run.stop", label: "Run: Stop", run: () => useRun.getState().stop() },
    { id: "run.panel", label: "Run: Show / Hide Input and Output", keys: "Ctrl+Shift+B", run: () => useRun.getState().toggle() },
    { id: "file.new", label: "File: New File", keys: "Ctrl+N", run: () => app().newFile() },
    { id: "file.openFolder", label: "File: Open Folder…", keys: "Ctrl+Shift+O", run: () => app().openFolder() },
    { id: "file.closeFolder", label: "File: Close Folder", run: () => app().closeFolder() },
    { id: "file.save", label: "File: Save", keys: "Ctrl+S", run: () => app().save() },
    { id: "file.saveAs", label: "File: Save As…", keys: "Ctrl+Shift+S", run: () => app().saveAs() },
    { id: "file.saveAll", label: "File: Save All", run: () => app().saveAll() },
    { id: "file.close", label: "File: Close Editor", keys: "Ctrl+W", run: () => { const a = app().active; if (a) app().closeTab(a); } },
    { id: "view.explorer", label: "View: Show / Hide Explorer", keys: "Ctrl+B", run: () => app().toggleSidebar() },
    { id: "view.theme", label: "View: Toggle Dark / Light Theme", run: () => app().setTheme(app().theme === "dark" ? "light" : "dark") },
    { id: "view.zoomIn", label: "View: Bigger Font", keys: "Ctrl+=", run: () => app().setFontSize(app().fontSize + 1) },
    { id: "view.zoomOut", label: "View: Smaller Font", keys: "Ctrl+-", run: () => app().setFontSize(app().fontSize - 1) },
    { id: "view.zoomReset", label: "View: Reset Font Size", keys: "Ctrl+0", run: () => app().setFontSize(15) },
    { id: "update", label: "Check for Updates", run: () => checkForUpdate(true) },
    { id: "snippets.edit", label: "Preferences: Edit My Snippets", run: () => openUserSnippets() },
    { id: "go.line", label: "Go to Line…", keys: "Ctrl+G", run: action("editor.action.gotoLine") },
    { id: "go.def", label: "Go to Definition", keys: "F12", run: action("editor.action.revealDefinition") },
    { id: "go.refs", label: "Find All References", keys: "Shift+F12", run: action("editor.action.referenceSearch.trigger") },
    { id: "edit.rename", label: "Rename Symbol", keys: "F2", run: action("editor.action.rename") },
    { id: "edit.format", label: "Format Document", keys: "Shift+Alt+F", run: action("editor.action.formatDocument") },
    { id: "edit.comment", label: "Toggle Line Comment", keys: "Ctrl+/", run: action("editor.action.commentLine") },
    { id: "edit.find", label: "Find", keys: "Ctrl+F", run: action("actions.find") },
    { id: "edit.replace", label: "Replace", keys: "Ctrl+H", run: action("editor.action.startFindReplaceAction") },
    { id: "edit.fold", label: "Fold All", run: action("editor.foldAll") },
    { id: "edit.unfold", label: "Unfold All", run: action("editor.unfoldAll") },
    { id: "edit.suggest", label: "Trigger Suggestions", keys: "Ctrl+Space", run: action("editor.action.triggerSuggest") },
  ];
}

/** Fuzzy match: every query letter in order. Higher is better; -1 = no match. */
export function fuzzy(query: string, text: string): { score: number; hits: number[] } {
  const q = query.toLowerCase().replace(/\s+/g, "");
  if (!q) return { score: 0, hits: [] };
  const t = text.toLowerCase();
  const hits: number[] = [];
  let score = 0, ti = 0, prev = -2;
  for (const ch of q) {
    const i = t.indexOf(ch, ti);
    if (i < 0) return { score: -1, hits: [] };
    const start = i === 0 || /[\s/\\._:-]/.test(text[i - 1]) || (text[i] >= "A" && text[i] <= "Z" && text[i - 1] >= "a" && text[i - 1] <= "z");
    score += 1 + (i === prev + 1 ? 5 : 0) + (start ? 4 : 0);
    hits.push(i);
    prev = i;
    ti = i + 1;
  }
  return { score: score - t.length * 0.01, hits };
}
