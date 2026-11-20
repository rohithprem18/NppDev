import { useEffect, useRef } from "react";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { useApp } from "./store";
import * as ws from "./lib/workspace";
import { baseName } from "./lib/api";
import { Explorer } from "./components/Explorer";
import { EditorArea } from "./components/EditorArea";
import { ActivityBar, StatusBar, ConfirmDialog, Toast } from "./components/Chrome";

export default function App() {
  const { ready, init, sidebarVisible, sidebarWidth, setSidebarWidth, folder, active, dirty } = useApp();
  const dragging = useRef(false);

  useEffect(() => { init(); }, [init]);

  // window title: "file.cpp ● - folder - NppDev"
  useEffect(() => {
    const parts = [active ? baseName(active.replace(/^untitled:/, "")) + (dirty[active] ? " ●" : "") : null, folder ? baseName(folder) : null, "NppDev"];
    getCurrentWindow().setTitle(parts.filter(Boolean).join(" - ")).catch(() => {});
  }, [active, folder, dirty]);

  // keyboard shortcuts outside the editor (inside it, Monaco handles its own)
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const s = useApp.getState();
      const ctrl = e.ctrlKey || e.metaKey;
      if (!ctrl) return;
      const k = e.key.toLowerCase();
      if (k === "s" && !e.shiftKey) { e.preventDefault(); s.save(); }
      else if (k === "s" && e.shiftKey) { e.preventDefault(); s.saveAs(); }
      else if (k === "n" && !e.shiftKey) { e.preventDefault(); s.newFile(); }
      else if (k === "o" && e.shiftKey) { e.preventDefault(); s.openFolder(); }
      else if (k === "b") { e.preventDefault(); s.toggleSidebar(); }
      else if (k === "w") { e.preventDefault(); if (s.active) s.closeTab(s.active); }
      else if (k === "tab") {
        e.preventDefault();
        const i = s.tabs.findIndex((t) => t.path === s.active);
        const next = s.tabs[(i + (e.shiftKey ? -1 : 1) + s.tabs.length) % s.tabs.length];
        if (next) s.activate(next.path);
      }
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, []);

  // closing the window with unsaved files asks first
  useEffect(() => {
    const un = getCurrentWindow().onCloseRequested(async (e) => {
      const s = useApp.getState();
      const unsaved = s.tabs.filter((t) => ws.isDirty(t.path));
      if (!unsaved.length) return;
      e.preventDefault();
      const choice = await s.ask({
        title: "Save changes before closing?",
        message: unsaved.length === 1 ? `${unsaved[0].name} has unsaved changes.` : `${unsaved.length} files have unsaved changes.`,
        buttons: [{ label: "Save All", value: "save", primary: true }, { label: "Don't Save", value: "discard", danger: true }, { label: "Cancel", value: "cancel" }],
      });
      if (choice === "cancel") return;
      if (choice === "save") await s.saveAll();
      if (choice === "discard" || !useApp.getState().tabs.some((t) => ws.isDirty(t.path))) await getCurrentWindow().destroy();
    });
    return () => { un.then((f) => f()); };
  }, []);

  // drag the Explorer's edge to resize it
  useEffect(() => {
    const move = (e: MouseEvent) => { if (dragging.current) setSidebarWidth(e.clientX - 48); };
    const up = () => { dragging.current = false; document.body.classList.remove("resizing"); };
    window.addEventListener("mousemove", move);
    window.addEventListener("mouseup", up);
    return () => { window.removeEventListener("mousemove", move); window.removeEventListener("mouseup", up); };
  }, [setSidebarWidth]);

  if (!ready) return <div className="boot" />;

  return (
    <div className="app">
      <div className="workbench">
        <ActivityBar />
        {sidebarVisible && (
          <>
            <aside className="sidebar" style={{ width: sidebarWidth }}><Explorer /></aside>
            <div className="sash" onMouseDown={() => { dragging.current = true; document.body.classList.add("resizing"); }} />
          </>
        )}
        <main className="main"><EditorArea /></main>
      </div>
      <StatusBar />
      <ConfirmDialog />
      <Toast />
    </div>
  );
}
