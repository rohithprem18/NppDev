// Tabs and the code editor (one Monaco editor; each open file is a model). Shows Welcome when empty.
// Monaco is a separate chunk loaded in the background (see lib/workspace.ts), so the window draws
// first and the editor appears a moment later.
import { useEffect, useRef, useState } from "react";
import type { monaco as Monaco } from "../lib/editor";
import * as ws from "../lib/workspace";
import { useApp } from "../store";
import { useCursor } from "../lib/cursor";
import { useRun, canRun } from "../run";
import { Icon } from "./Icons";
import { FileIcon } from "./FileIcon";
import { Welcome } from "./Welcome";
import { DatabaseView } from "./DatabaseView";
import { isDbTab } from "../lib/sqlSchema";

type EditorModule = typeof import("../lib/editor");

export function EditorArea() {
  const tabs = useApp((s) => s.tabs);
  const active = useApp((s) => s.active);
  const dirty = useApp((s) => s.dirty);
  const theme = useApp((s) => s.theme);
  const fontSize = useApp((s) => s.fontSize);
  const editorFont = useApp((s) => s.editorFont);
  const { activate, closeTab } = useApp.getState();
  const { run } = useRun.getState();
  const host = useRef<HTMLDivElement>(null);
  const editor = useRef<Monaco.editor.IStandaloneCodeEditor | null>(null);
  const mod = useRef<EditorModule | null>(null);
  const shown = useRef<string | null>(null);
  const [ready, setReady] = useState(false);
  const family = `"${editorFont}", Hack, Consolas, "Ubuntu Mono", monospace`;

  // the editor is created once (when Monaco has loaded) and kept; switching tabs swaps its model
  useEffect(() => {
    let disposed = false;
    let cleanup = () => {};
    ws.monacoReady.then(async (m) => {
      // Monaco measures character widths when the editor is created; with the code font not yet
      // loaded it would measure a fallback font, and the caret, underlines and highlights would
      // then drift from the letters. The window is already on screen, so waiting costs nothing.
      const fonts = Promise.all([document.fonts.load("15px Hack"), document.fonts.load(`15px "${useApp.getState().editorFont}"`)]).catch(() => {});
      await Promise.race([fonts, new Promise((done) => setTimeout(done, 1500))]);
      if (disposed || !host.current) return;
      const { monaco, EDITOR_OPTIONS } = m;
      mod.current = m;
      const s = useApp.getState();
      const ed = monaco.editor.create(host.current, { ...EDITOR_OPTIONS, theme: s.theme === "dark" ? "nppdev-dark" : "nppdev-light", fontSize: s.fontSize, fontFamily: family, model: null });
      editor.current = ed;
      ed.onDidChangeCursorPosition((e) => useCursor.setState({ line: e.position.lineNumber, col: e.position.column }));
      // Ctrl+S inside the editor (Monaco takes the key before the window sees it)
      ed.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, () => useApp.getState().save());
      ed.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyMod.Shift | monaco.KeyCode.KeyS, () => useApp.getState().saveAs());
      ed.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, () => useRun.getState().run());
      (window as unknown as { nppdevEditor: unknown }).nppdevEditor = ed;
      // measure again once every font is in, so the caret lines up with the letters exactly
      const remeasure = () => monaco.editor.remeasureFonts();
      document.fonts.ready.then(remeasure);
      document.fonts.addEventListener("loadingdone", remeasure);
      cleanup = () => { document.fonts.removeEventListener("loadingdone", remeasure); ed.dispose(); };
      setReady(true);
    });
    return () => { disposed = true; cleanup(); };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => { mod.current?.monaco.editor.setTheme(theme === "dark" ? "nppdev-dark" : "nppdev-light"); }, [theme, ready]);
  useEffect(() => { editor.current?.updateOptions({ fontSize }); }, [fontSize, ready]);
  useEffect(() => {
    document.documentElement.style.setProperty("--code", family);
    editor.current?.updateOptions({ fontFamily: family });
    document.fonts.load(`15px "${editorFont}"`).catch(() => {}).finally(() => mod.current?.monaco.editor.remeasureFonts());
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [editorFont, ready]);

  // show the active tab's document, keeping each one's cursor and scroll position
  useEffect(() => {
    const ed = editor.current;
    if (!ed) return;
    const prev = shown.current && ws.getDoc(shown.current);
    if (prev) prev.view = ed.saveViewState();
    const doc = active ? ws.getDoc(active) : undefined;
    ed.setModel(doc?.model ?? null);
    shown.current = active;
    if (doc) {
      if (doc.view) ed.restoreViewState(doc.view);
      requestAnimationFrame(() => ed.focus());
    }
  }, [active, tabs, ready]);

  return (
    <div className="editor-area">
      {tabs.length > 0 && (
        <div className="tab-bar">
        <div className="tabs" onWheel={(e) => { e.currentTarget.scrollLeft += e.deltaY; }}>
          {tabs.map((t) => (
            <div
              key={t.path}
              className={"tab" + (t.path === active ? " active" : "") + (dirty[t.path] ? " dirty" : "")}
              onClick={() => activate(t.path)}
              onMouseDown={(e) => { if (e.button === 1) { e.preventDefault(); closeTab(t.path); } }}
              title={isDbTab(t.path) ? "The sample database" : ws.isUntitled(t.path) ? t.name : t.path}
            >
              <FileIcon name={t.name} />
              <span className="tab-name">{t.name}</span>
              <button className="tab-close" title="Close (Ctrl+W)" onClick={(e) => { e.stopPropagation(); closeTab(t.path); }}>
                <span className="dot" />
                {Icon.close(14)}
              </button>
            </div>
          ))}
        </div>
        {canRun(active) && (
          // a plain Run button: it never changes while a program runs
          <button className="tab-run" title="Run (Ctrl+Enter)" onClick={() => run()}>{Icon.run(12)}<span>Run</span></button>
        )}
        </div>
      )}
      <div className="editor-host" ref={host} style={{ display: active && !isDbTab(active) ? "block" : "none" }} />
      {isDbTab(active) && <DatabaseView />}
      {!active && <Welcome />}
    </div>
  );
}
