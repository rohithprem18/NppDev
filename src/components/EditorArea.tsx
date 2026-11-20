// Tabs and the code editor (one Monaco editor; each open file is a model). Shows Welcome when empty.
import { useEffect, useRef } from "react";
import { monaco, EDITOR_OPTIONS } from "../lib/editor";
import * as ws from "../lib/workspace";
import { fileType } from "../lib/fileTypes";
import { useApp } from "../store";
import { Icon } from "./Icons";
import { Welcome } from "./Welcome";

export function EditorArea() {
  const { tabs, active, dirty, theme, fontSize, activate, closeTab, setCursor } = useApp();
  const host = useRef<HTMLDivElement>(null);
  const editor = useRef<monaco.editor.IStandaloneCodeEditor | null>(null);
  const shown = useRef<string | null>(null);

  // the editor is created once and kept; switching tabs swaps its model
  useEffect(() => {
    if (!host.current) return;
    const ed = monaco.editor.create(host.current, { ...EDITOR_OPTIONS, theme: theme === "dark" ? "nppdev-dark" : "nppdev-light", fontSize, model: null });
    editor.current = ed;
    ed.onDidChangeCursorPosition((e) => setCursor(e.position.lineNumber, e.position.column));
    // Ctrl+S inside the editor (Monaco takes the key before the window sees it)
    ed.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, () => useApp.getState().save());
    ed.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyMod.Shift | monaco.KeyCode.KeyS, () => useApp.getState().saveAs());
    (window as unknown as { nppdevEditor: unknown }).nppdevEditor = ed;
    return () => ed.dispose();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => { monaco.editor.setTheme(theme === "dark" ? "nppdev-dark" : "nppdev-light"); }, [theme]);
  useEffect(() => { editor.current?.updateOptions({ fontSize }); }, [fontSize]);

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
  }, [active, tabs]);

  return (
    <div className="editor-area">
      {tabs.length > 0 && (
        <div className="tabs" onWheel={(e) => { e.currentTarget.scrollLeft += e.deltaY; }}>
          {tabs.map((t) => {
            const ft = fileType(t.name);
            return (
              <div
                key={t.path}
                className={"tab" + (t.path === active ? " active" : "") + (dirty[t.path] ? " dirty" : "")}
                onClick={() => activate(t.path)}
                onMouseDown={(e) => { if (e.button === 1) { e.preventDefault(); closeTab(t.path); } }}
                title={ws.isUntitled(t.path) ? t.name : t.path}
              >
                <span className="file-glyph" style={{ color: ft.color }}>{ft.glyph || <span className="page" />}</span>
                <span className="tab-name">{t.name}</span>
                <button className="tab-close" title="Close (Ctrl+W)" onClick={(e) => { e.stopPropagation(); closeTab(t.path); }}>
                  <span className="dot" />
                  {Icon.close(14)}
                </button>
              </div>
            );
          })}
        </div>
      )}
      <div className="editor-host" ref={host} style={{ display: active ? "block" : "none" }} />
      {!active && <Welcome />}
    </div>
  );
}
