// Activity bar, status bar, confirm dialog and toast: the frame around the editor.
import { useEffect, useRef } from "react";
import { useApp } from "../store";
import { useCursor } from "../lib/cursor";
import { usePalette } from "./Palette";
import { baseName } from "../lib/api";
import { fileType, LANGUAGE_NAMES } from "../lib/fileTypes";
import { Icon } from "./Icons";
import { isDbTab } from "../lib/sqlSchema";

export function ActivityBar() {
  const sidebarVisible = useApp((s) => s.sidebarVisible);
  const sideView = useApp((s) => s.sideView);
  const { showSide } = useApp.getState();
  return (
    <nav className="activity-bar">
      <button className={sidebarVisible && sideView === "explorer" ? "active" : ""} title="Explorer (Ctrl+B)" onClick={() => showSide("explorer")}>{Icon.files(22)}</button>
      <div className="grow" />
      <button title="Command Palette (Ctrl+Shift+P)" onClick={() => usePalette.getState().show(">")}>{Icon.command(20)}</button>
      <button className={sidebarVisible && sideView === "settings" ? "active" : ""} title="Settings (Ctrl+,)" onClick={() => showSide("settings")}>{Icon.settings(21)}</button>
    </nav>
  );
}

export function StatusBar() {
  const folder = useApp((s) => s.folder);
  const active = useApp((s) => s.active);
  const tabs = useApp((s) => s.tabs);
  const cursor = useCursor();
  const tab = tabs.find((t) => t.path === active);
  const db = isDbTab(active);
  const lang = db ? "SQLite" : tab ? LANGUAGE_NAMES[fileType(tab.name).lang] ?? "Plain Text" : null;
  return (
    <footer className="status-bar">
      <span className="status-item">{folder ? baseName(folder) : "No folder"}</span>
      <div className="grow" />
      {tab && !db && <span className="status-item">Ln {cursor.line}, Col {cursor.col}</span>}
      {tab && !db && <span className="status-item">UTF-8</span>}
      {lang && <span className="status-item">{lang}</span>}
    </footer>
  );
}

export function ConfirmDialog() {
  const confirm = useApp((s) => s.confirm);
  const first = useRef<HTMLButtonElement>(null);
  useEffect(() => { first.current?.focus(); }, [confirm]);
  if (!confirm) return null;
  const cancel = confirm.buttons.find((b) => b.value === "cancel" || b.value === "no")?.value ?? confirm.buttons[confirm.buttons.length - 1].value;
  return (
    <div className="modal-backdrop" onMouseDown={() => confirm.resolve(cancel)} onKeyDown={(e) => { if (e.key === "Escape") confirm.resolve(cancel); }}>
      <div className="modal" role="dialog" aria-modal="true" onMouseDown={(e) => e.stopPropagation()}>
        <h3>{confirm.title}</h3>
        <p>{confirm.message}</p>
        <div className="modal-buttons">
          {confirm.buttons.map((b, i) => (
            <button key={b.value} ref={i === 0 ? first : undefined} className={"btn" + (b.primary ? " primary" : "") + (b.danger ? " danger" : "")} onClick={() => confirm.resolve(b.value)}>
              {b.label}
            </button>
          ))}
        </div>
      </div>
    </div>
  );
}

export function Toast() {
  const toast = useApp((s) => s.toast);
  if (!toast) return null;
  return (
    <div className={"toast " + toast.kind} role="status">
      <span className="toast-icon">{toast.kind === "error" ? Icon.alert(16) : Icon.info(16)}</span>
      <span>{toast.text}</span>
    </div>
  );
}
