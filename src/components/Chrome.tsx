// Activity bar, status bar, confirm dialog and toast: the frame around the editor.
import { useEffect, useRef } from "react";
import { useApp } from "../store";
import { useRun } from "../run";
import { baseName } from "../lib/api";
import { fileType, LANGUAGE_NAMES } from "../lib/fileTypes";
import { Icon } from "./Icons";

export function ActivityBar() {
  const { sidebarVisible, toggleSidebar, theme, setTheme } = useApp();
  const { open: runOpen, toggle: toggleRun } = useRun();
  return (
    <nav className="activity-bar">
      <button className={sidebarVisible ? "active" : ""} title="Explorer (Ctrl+B)" onClick={toggleSidebar}>{Icon.files(22)}</button>
      <button className={runOpen ? "active" : ""} title="Run & test cases (Ctrl+Shift+B)" onClick={() => toggleRun()}>{Icon.beaker(22)}</button>
      <div className="grow" />
      <button title={theme === "dark" ? "Light theme" : "Dark theme"} onClick={() => setTheme(theme === "dark" ? "light" : "dark")}>
        {theme === "dark" ? Icon.sun(20) : Icon.moon(20)}
      </button>
    </nav>
  );
}

export function StatusBar() {
  const { folder, active, cursor, tabs } = useApp();
  const tab = tabs.find((t) => t.path === active);
  const lang = tab ? LANGUAGE_NAMES[fileType(tab.name).lang] ?? "Plain Text" : null;
  return (
    <footer className="status-bar">
      <span className="status-item">{folder ? baseName(folder) : "No folder"}</span>
      <div className="grow" />
      {tab && <span className="status-item">Ln {cursor.line}, Col {cursor.col}</span>}
      {tab && <span className="status-item">UTF-8</span>}
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
  return <div className={"toast " + toast.kind} role="status">{toast.text}</div>;
}
