// Quick Open (Ctrl+P: files in the folder) and the Command Palette (Ctrl+Shift+P, or type ">").
import { useEffect, useMemo, useRef, useState, type JSX } from "react";
import { create } from "zustand";
import { invoke } from "@tauri-apps/api/core";
import { useApp } from "../store";
import { allCommands, fuzzy, type Command } from "../lib/commands";
import { FileIcon } from "./FileIcon";
import { joinPath } from "../lib/api";

export const usePalette = create<{ open: boolean; text: string; show(text: string): void; hide(): void }>((set) => ({
  open: false,
  text: "",
  show: (text) => set({ open: true, text }),
  hide: () => {
    set({ open: false });
    // back to the editor, like VS Code
    requestAnimationFrame(() => (window as unknown as { nppdevEditor?: { focus(): void; getModel(): unknown } }).nppdevEditor?.getModel() && (window as unknown as { nppdevEditor: { focus(): void } }).nppdevEditor.focus());
  },
}));

interface Item { key: string; label: string; detail?: string; keys?: string; hits: number[]; icon?: JSX.Element; run(): void }

const recentCommands: string[] = [];

function Highlight({ text, hits }: { text: string; hits: number[] }) {
  if (!hits.length) return <>{text}</>;
  const set = new Set(hits);
  return <>{[...text].map((c, i) => (set.has(i) ? <b key={i}>{c}</b> : c))}</>;
}

export function Palette() {
  const { open, text, hide } = usePalette();
  const folder = useApp((s) => s.folder);
  const [query, setQuery] = useState(text);
  const [files, setFiles] = useState<string[] | null>(null);
  const [sel, setSel] = useState(0);
  const input = useRef<HTMLInputElement>(null);
  const list = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    setQuery(text);
    setSel(0);
    requestAnimationFrame(() => { const el = input.current; if (el) { el.focus(); el.setSelectionRange(el.value.length, el.value.length); } });
    if (!text.startsWith(">") && folder) invoke<string[]>("list_files", { root: folder }).then(setFiles).catch(() => setFiles([]));
  }, [open, text, folder]);

  const commandMode = query.startsWith(">");
  const items: Item[] = useMemo(() => {
    if (!open) return [];
    if (commandMode) {
      const q = query.slice(1).trim();
      const cmds = allCommands();
      const scored = cmds.map((c: Command) => ({ c, m: fuzzy(q, c.label) })).filter((x) => x.m.score >= 0);
      if (q) scored.sort((a, b) => b.m.score - a.m.score);
      else scored.sort((a, b) => (recentCommands.indexOf(a.c.id) + 1 || 99) - (recentCommands.indexOf(b.c.id) + 1 || 99));
      return scored.map(({ c, m }) => ({
        key: c.id, label: c.label, keys: c.keys, hits: m.hits,
        detail: !q && recentCommands.includes(c.id) ? "recently used" : undefined,
        run: () => { recentCommands.splice(recentCommands.indexOf(c.id) >>> 0, recentCommands.includes(c.id) ? 1 : 0); recentCommands.unshift(c.id); c.run(); },
      }));
    }
    if (!folder) return [];
    const q = query.trim();
    const openTabs = useApp.getState().tabs.map((t) => t.path);
    const all = (files ?? []).map((rel) => {
      const name = rel.split("/").pop()!;
      const m = fuzzy(q, name);
      const mPath = m.score < 0 ? fuzzy(q, rel) : m;
      return { rel, name, score: m.score >= 0 ? m.score + 10 : mPath.score, hits: m.score >= 0 ? m.hits : [] };
    }).filter((x) => x.score >= 0);
    if (q) all.sort((a, b) => b.score - a.score);
    return all.slice(0, 60).map((f) => {
      const full = joinPath(folder, f.rel.replace(/\//g, folder.includes("\\") ? "\\" : "/"));
      return {
        key: f.rel, label: f.name, hits: f.hits,
        detail: f.rel.includes("/") ? f.rel.slice(0, f.rel.lastIndexOf("/")) : undefined,
        keys: openTabs.includes(full) ? "open" : undefined,
        icon: <FileIcon name={f.name} />,
        run: () => { useApp.getState().openFile(full); },
      };
    });
  }, [open, commandMode, query, files, folder]);

  useEffect(() => { setSel(0); }, [query]);
  useEffect(() => { list.current?.children[sel]?.scrollIntoView({ block: "nearest" }); }, [sel]);

  if (!open) return null;
  const choose = (i: number) => { const it = items[i]; hide(); if (it) setTimeout(() => it.run(), 0); };

  return (
    <div className="palette-backdrop" onMouseDown={hide}>
      <div className="palette" onMouseDown={(e) => e.stopPropagation()} role="dialog" aria-label={commandMode ? "Command Palette" : "Quick Open"}>
        <input
          ref={input}
          className="palette-input"
          value={query}
          spellCheck={false}
          placeholder={commandMode ? "Type a command" : folder ? "Search files by name (type > for commands)" : "Type > for commands"}
          onChange={(e) => setQuery(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Escape") { e.preventDefault(); hide(); }
            else if (e.key === "ArrowDown") { e.preventDefault(); setSel((s) => Math.min(items.length - 1, s + 1)); }
            else if (e.key === "ArrowUp") { e.preventDefault(); setSel((s) => Math.max(0, s - 1)); }
            else if (e.key === "PageDown") { e.preventDefault(); setSel((s) => Math.min(items.length - 1, s + 10)); }
            else if (e.key === "PageUp") { e.preventDefault(); setSel((s) => Math.max(0, s - 10)); }
            else if (e.key === "Enter") { e.preventDefault(); choose(sel); }
          }}
        />
        <div className="palette-list" ref={list}>
          {items.map((it, i) => (
            <div key={it.key} className={"palette-item" + (i === sel ? " selected" : "")} onMouseMove={() => setSel(i)} onClick={() => choose(i)}>
              {it.icon}
              <span className="palette-label"><Highlight text={it.label} hits={it.hits} /></span>
              {it.detail && <span className="palette-detail">{it.detail}</span>}
              {it.keys && <kbd>{it.keys}</kbd>}
            </div>
          ))}
          {items.length === 0 && (
            <div className="palette-empty">
              {commandMode ? "No matching commands" : !folder ? "Open a folder to search its files." : files === null ? "Loading files…" : "No matching files"}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
