// The Explorer: the open folder as a tree. One click opens a file or a folder; right-click for more.
import { useEffect, useRef, useState, useCallback, type JSX } from "react";
import { api, baseName, dirName, joinPath, type Entry } from "../lib/api";
import { fileType } from "../lib/fileTypes";
import { useApp } from "../store";
import { Icon } from "./Icons";
import { FileIcon, FolderIcon } from "./FileIcon";

const SQL_STARTER = `-- Runs on NppDev's sample database: its tables are listed on the right (click one to see all of it).
-- Press Ctrl+Enter to run.
SELECT name, job_title, salary
FROM employees
ORDER BY salary DESC
LIMIT 5;
`;

type Pending = { kind: "file" | "folder"; dir: string } | { kind: "rename"; path: string } | null;
interface Menu { x: number; y: number; entry: Entry | null }

export function Explorer() {
  const { folder, treeVersion, active, openFile, openFolder, closeFolder, refreshTree, notify, ask, pathRenamed, pathDeleted } = useApp();
  const [children, setChildren] = useState<Record<string, Entry[]>>({});
  const [expanded, setExpanded] = useState<Set<string>>(new Set());
  const [selected, setSelected] = useState<string | null>(null);
  const [pending, setPending] = useState<Pending>(null);
  const [menu, setMenu] = useState<Menu | null>(null);

  const load = useCallback(async (dir: string) => {
    try {
      const list = await api.listDir(dir);
      setChildren((c) => ({ ...c, [dir]: list }));
    } catch (e) {
      notify(String(e), "error");
    }
  }, [notify]);

  // (re)load the root and every expanded folder
  useEffect(() => {
    if (!folder) return;
    load(folder);
    expanded.forEach((d) => load(d));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [folder, treeVersion]);

  // pick up changes made outside NppDev when the window gets focus again
  useEffect(() => {
    const onFocus = () => refreshTree();
    window.addEventListener("focus", onFocus);
    return () => window.removeEventListener("focus", onFocus);
  }, [refreshTree]);

  useEffect(() => { setExpanded(new Set()); setChildren({}); setSelected(null); }, [folder]);
  useEffect(() => { if (active) setSelected(active); }, [active]);

  if (!folder) {
    return (
      <div className="explorer-empty">
        <p>No folder is open.</p>
        <button className="btn primary" onClick={() => openFolder()}>Open Folder</button>
        <p className="hint">Your recent folders are on the Welcome page.</p>
      </div>
    );
  }

  const toggle = (dir: string) => {
    const next = new Set(expanded);
    if (next.has(dir)) next.delete(dir);
    else { next.add(dir); load(dir); }
    setExpanded(next);
  };

  /** Where "New File" goes: the selected folder, the selected file's folder, or the root. */
  const targetDir = () => {
    if (!selected || selected === folder) return folder;
    const entry = Object.values(children).flat().find((e) => e.path === selected);
    return entry?.isDir ? entry.path : dirName(selected);
  };

  const startNew = (kind: "file" | "folder", dir = targetDir()) => {
    if (dir !== folder && !expanded.has(dir)) toggle(dir);
    setPending({ kind, dir });
  };

  const commit = async (name: string) => {
    const p = pending;
    setPending(null);
    name = name.trim();
    if (!p || !name) return;
    if (/[<>:"|?*]/.test(name)) return notify("A name can't contain < > : \" | ? *", "error");
    try {
      if (p.kind === "rename") {
        const to = joinPath(dirName(p.path), name);
        if (to === p.path) return;
        await api.renamePath(p.path, to);
        pathRenamed(p.path, to);
        setSelected(to);
        await load(dirName(p.path));
      } else {
        const path = joinPath(p.dir, name);
        if (p.kind === "file") {
          await api.createFile(path);
          // a new .sql file starts with a query that runs straight away on the sample database
          if (/\.sql$/i.test(path)) await api.writeText(path, SQL_STARTER);
        }
        else await api.createDir(path);
        await load(p.dir);
        if (p.kind === "file") openFile(path);
        else setExpanded((s) => new Set(s).add(path));
        setSelected(path);
      }
    } catch (e) {
      notify(String(e), "error");
    }
  };

  const remove = async (entry: Entry) => {
    const choice = await ask({
      title: `Delete ${entry.isDir ? "folder" : "file"}?`,
      message: `"${entry.name}"${entry.isDir ? " and everything in it" : ""} will be moved to the ${navigator.userAgent.includes("Windows") ? "Recycle Bin" : "Trash"}.`,
      buttons: [{ label: "Delete", value: "yes", danger: true }, { label: "Cancel", value: "no" }],
    });
    if (choice !== "yes") return;
    try {
      await api.deletePath(entry.path);
      pathDeleted(entry.path);
      await load(dirName(entry.path));
    } catch (e) {
      notify(String(e), "error");
    }
  };

  const collapseAll = () => setExpanded(new Set());

  const rows: JSX.Element[] = [];
  const renderDir = (dir: string, depth: number) => {
    const list = children[dir];
    if (pending && pending.kind !== "rename" && pending.dir === dir) {
      rows.push(<InlineInput key="new" depth={depth} kind={pending.kind} initial="" onDone={commit} />);
    }
    if (!list) return;
    for (const e of list) {
      if (pending?.kind === "rename" && pending.path === e.path) {
        rows.push(<InlineInput key={e.path} depth={depth} kind={e.isDir ? "folder" : "file"} initial={e.name} onDone={commit} />);
        continue;
      }
      const open = e.isDir && expanded.has(e.path);
      rows.push(
        <div
          key={e.path}
          className={"tree-row" + (selected === e.path ? " selected" : "") + (active === e.path ? " active" : "")}
          style={{ paddingLeft: 8 + depth * 14 }}
          title={e.path}
          onClick={() => { setSelected(e.path); if (e.isDir) toggle(e.path); else openFile(e.path); }}
          onContextMenu={(ev) => { ev.preventDefault(); setSelected(e.path); setMenu({ x: ev.clientX, y: ev.clientY, entry: e }); }}
          onKeyDown={(ev) => { if (ev.key === "F2") setPending({ kind: "rename", path: e.path }); if (ev.key === "Delete") remove(e); }}
          tabIndex={0}
        >
          <span className="twisty">{e.isDir ? Icon.chevron(open) : null}</span>
          {e.isDir ? <FolderIcon open={open} /> : <FileIcon name={e.name} />}
          <span className="tree-name">{e.name}</span>
        </div>,
      );
      if (open) renderDir(e.path, depth + 1);
    }
  };
  renderDir(folder, 1);

  return (
    <div className="explorer" onContextMenu={(ev) => { if (ev.target === ev.currentTarget) { ev.preventDefault(); setMenu({ x: ev.clientX, y: ev.clientY, entry: null }); } }}>
      <div className="side-header">
        <span>EXPLORER</span>
        <div className="side-actions">
          <button title="New File" aria-label="New File" onClick={() => startNew("file")}>{Icon.newFile()}</button>
          <button title="New Folder" aria-label="New Folder" onClick={() => startNew("folder")}>{Icon.newFolder()}</button>
          <button title="Refresh" aria-label="Refresh" onClick={refreshTree}>{Icon.refresh()}</button>
          <button title="Collapse Folders" aria-label="Collapse Folders" onClick={collapseAll}>{Icon.collapse()}</button>
          <button title="Close Folder" aria-label="Close Folder" onClick={() => closeFolder()}>{Icon.close()}</button>
        </div>
      </div>
      <div className="tree-root" onClick={() => setSelected(folder)}>
        <span className="twisty">{Icon.chevron(true)}</span>
        <span className="tree-root-name">{baseName(folder).toUpperCase()}</span>
      </div>
      <div className="tree" onContextMenu={(ev) => { ev.preventDefault(); if (ev.target === ev.currentTarget) setMenu({ x: ev.clientX, y: ev.clientY, entry: null }); }}>
        {rows}
        {children[folder]?.length === 0 && !pending && <div className="tree-hint">This folder is empty. Use New File above.</div>}
      </div>
      {menu && (
        <ContextMenu menu={menu} onClose={() => setMenu(null)} items={[
          { label: "New File", run: () => startNew("file", menu.entry ? (menu.entry.isDir ? menu.entry.path : dirName(menu.entry.path)) : folder) },
          { label: "New Folder", run: () => startNew("folder", menu.entry ? (menu.entry.isDir ? menu.entry.path : dirName(menu.entry.path)) : folder) },
          ...(menu.entry ? [
            { label: "Rename", hint: "F2", run: () => setPending({ kind: "rename", path: menu.entry!.path }) },
            { label: "Delete", hint: "Del", danger: true, run: () => remove(menu.entry!) },
            { label: navigator.userAgent.includes("Windows") ? "Reveal in File Explorer" : "Show in Files", run: () => api.reveal(menu.entry!.path) },
          ] : [{ label: "Refresh", run: refreshTree }]),
        ]} />
      )}
    </div>
  );
}

function InlineInput({ depth, kind, initial, onDone }: { depth: number; kind: "file" | "folder"; initial: string; onDone: (v: string) => void }) {
  const ref = useRef<HTMLInputElement>(null);
  const done = useRef(false);
  useEffect(() => {
    const el = ref.current!;
    el.focus();
    const dot = initial.lastIndexOf(".");
    el.setSelectionRange(0, kind === "file" && dot > 0 ? dot : initial.length);
  }, [initial, kind]);
  const finish = (v: string) => { if (!done.current) { done.current = true; onDone(v); } };
  return (
    <div className="tree-row editing" style={{ paddingLeft: 8 + depth * 14 }}>
      <span className="twisty" />
      {kind === "folder" ? <FolderIcon open={false} /> : <FileIcon name="new" />}
      <input
        ref={ref}
        defaultValue={initial}
        placeholder={kind === "file" ? "name.cpp" : "folder name"}
        onKeyDown={(e) => { if (e.key === "Enter") finish(e.currentTarget.value); if (e.key === "Escape") finish(""); }}
        onBlur={(e) => finish(e.currentTarget.value)}
        spellCheck={false}
      />
    </div>
  );
}

export interface MenuItem { label: string; hint?: string; danger?: boolean; run: () => void }

export function ContextMenu({ menu, items, onClose }: { menu: { x: number; y: number }; items: MenuItem[]; onClose: () => void }) {
  useEffect(() => {
    const close = () => onClose();
    window.addEventListener("click", close);
    window.addEventListener("blur", close);
    return () => { window.removeEventListener("click", close); window.removeEventListener("blur", close); };
  }, [onClose]);
  const x = Math.min(menu.x, window.innerWidth - 220), y = Math.min(menu.y, window.innerHeight - items.length * 30 - 12);
  return (
    <div className="context-menu" style={{ left: x, top: y }} onContextMenu={(e) => e.preventDefault()}>
      {items.map((it) => (
        <button key={it.label} className={it.danger ? "danger" : ""} onClick={() => { onClose(); it.run(); }}>
          <span>{it.label}</span>{it.hint && <kbd>{it.hint}</kbd>}
        </button>
      ))}
    </div>
  );
}
