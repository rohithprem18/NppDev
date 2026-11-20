// NppDev's own title bar (the Windows one is turned off): logo, the open file and folder, a search
// box that opens Quick Open, and minimise / maximise / close. Drag it to move the window;
// double-click it to maximise.
import { isDbTab, DB_TAB_NAME } from "../lib/sqlSchema";
import { useEffect, useState } from "react";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { useApp } from "../store";
import { usePalette } from "./Palette";
import { baseName } from "../lib/api";
import { Icon } from "./Icons";
import logo from "../assets/logo.svg";

export function TitleBar() {
  const { folder, active, dirty, closeFolder } = useApp();
  const [maximized, setMaximized] = useState(false);
  const win = getCurrentWindow();

  useEffect(() => {
    win.isMaximized().then(setMaximized).catch(() => {});
    const un = win.onResized(() => { win.isMaximized().then(setMaximized).catch(() => {}); });
    return () => { un.then((f) => f()); };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const file = isDbTab(active) ? DB_TAB_NAME : active ? baseName(active.replace(/^untitled:/, "")) : null;

  return (
    <header className="titlebar" data-tauri-drag-region onDoubleClick={(e) => { if (e.target === e.currentTarget) win.toggleMaximize(); }}>
      <div className="tb-left" data-tauri-drag-region>
        <img src={logo} width={16} height={16} alt="" data-tauri-drag-region />
        <span className="tb-app" data-tauri-drag-region>NppDev</span>
        {folder && (
          <span className="tb-folder" title={folder}>
            {baseName(folder)}
            <button className="tb-close-folder" title="Close Folder" onClick={() => closeFolder()}>{Icon.close(12)}</button>
          </span>
        )}
      </div>

      <button className="tb-search" onClick={() => usePalette.getState().show("")} title="Search files (Ctrl+P), or type > for commands">
        {Icon.search(13)}
        <span>{file ? <>{file}{active && dirty[active] ? " •" : ""}</> : folder ? `Search ${baseName(folder)}` : "Search files and commands"}</span>
        <kbd>Ctrl+P</kbd>
      </button>

      <div className="tb-right" data-tauri-drag-region>
        <button className="tb-btn" title="Minimise" onClick={() => win.minimize()}>
          <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden="true"><path d="M0 5h10" stroke="currentColor" /></svg>
        </button>
        <button className="tb-btn" title={maximized ? "Restore" : "Maximise"} onClick={() => win.toggleMaximize()}>
          {maximized
            ? <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden="true"><path d="M2.5 2.5V.5h7v7h-2M.5 2.5h7v7h-7z" fill="none" stroke="currentColor" /></svg>
            : <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden="true"><rect x=".5" y=".5" width="9" height="9" fill="none" stroke="currentColor" /></svg>}
        </button>
        <button className="tb-btn close" title="Close" onClick={() => win.close()}>
          <svg width="10" height="10" viewBox="0 0 10 10" aria-hidden="true"><path d="M0 0l10 10M10 0L0 10" stroke="currentColor" /></svg>
        </button>
      </div>
    </header>
  );
}
