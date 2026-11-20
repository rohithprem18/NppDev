// IntelliSense from real language servers: clangd (C/C++), pylsp (Python), jdtls (Java).
// Monaco's own LSP client does the editor side; this file connects it to the Rust bridge
// (lsp.rs) and keeps each server to its own files. JavaScript/TypeScript use Monaco's
// built-in TypeScript worker instead.
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { monaco } from "./editor";

type Json = Record<string, unknown>;
type Listener = (m: Json) => void;

interface ServerDef {
  key: string;
  files: RegExp;
  /** What to tell the user when it's missing, by the error the bridge returns. */
  missing: Record<string, string>;
}

const SERVERS: Record<string, ServerDef> = {
  cpp: { key: "cpp", files: /\.(c|cc|cpp|cxx|c\+\+|h|hh|hpp|hxx)$/i, missing: { clangd: "clangd isn't installed, so C/C++ has no IntelliSense." } },
  python: { key: "python", files: /\.pyw?$/i, missing: { python: "", pylsp: "pylsp isn't installed, so Python has no IntelliSense." } },
  java: { key: "java", files: /\.java$/i, missing: { java21: "Java IntelliSense needs JDK 21 or newer.", jdtls: "The Java language server (jdtls) isn't installed." } },
};

const LANG_TO_SERVER: Record<string, string> = { c: "cpp", cpp: "cpp", python: "python", java: "java" };

/** Same file, any spelling: file:///c%3A/A/b.cpp == file:///C:/a/B.cpp */
const uriKey = (u: string) => { try { return decodeURIComponent(u).replace(/\\/g, "/").toLowerCase(); } catch { return u.toLowerCase(); } };
const uriPath = (u: string) => monaco.Uri.parse(u).fsPath;

/** The message transport Monaco's LSP client expects, over Tauri commands and events. */
class TauriTransport {
  private listener: Listener | undefined;
  private queue: Json[] = [];
  private uris = new Map<string, string>(); // uriKey -> the editor's spelling
  readonly state = {
    value: { state: "open" } as { state: "open" | "closed"; error?: Error },
    onChange: (_l: unknown) => ({ dispose() {} }),
  };

  constructor(private id: number, private def: ServerDef, private root: string | null, private compiler: string | null) {
    listen<{ id: number; msg: string }>("lsp-message", ({ payload }) => {
      if (payload.id !== this.id) return;
      let m: Json;
      try { m = JSON.parse(payload.msg); } catch { return; }
      this.respell(m);
      if (this.listener) this.listener(m); else this.queue.push(m);
    });
  }

  setListener(l: Listener | undefined) {
    this.listener = l;
    if (l) { const q = this.queue; this.queue = []; q.forEach(l); }
  }

  toString() { return `lsp:${this.def.key}`; }

  private post(m: Json) {
    return invoke("lsp_send", { id: this.id, msg: JSON.stringify(m) }).catch(() => {});
  }

  /** Put the editor's URI spelling back into a server message. */
  private respell(v: unknown): void {
    if (Array.isArray(v)) { v.forEach((x) => this.respell(x)); return; }
    if (!v || typeof v !== "object") return;
    const o = v as Json;
    for (const k of Object.keys(o)) {
      const x = o[k];
      if (typeof x === "string" && (k === "uri" || k === "targetUri") && x.startsWith("file:")) o[k] = this.uris.get(uriKey(x)) ?? x;
      else if (x && typeof x === "object") this.respell(x);
    }
  }

  async send(m: Json) {
    const method = m.method as string | undefined;
    const params = (m.params ?? {}) as Json;
    if (method === "initialize") {
      const rootUri = this.root ? monaco.Uri.file(this.root).toString() : null;
      params.rootUri = rootUri;
      params.rootPath = this.root;
      params.workspaceFolders = rootUri ? [{ uri: rootUri, name: this.root!.split(/[\\/]/).pop() }] : null;
      if (this.def.key === "cpp") params.initializationOptions = { fallbackFlags: ["-std=gnu++20"] };
    }
    // each server sees only its own files; requests about other files get an empty answer
    const uri = (params.textDocument as Json | undefined)?.uri as string | undefined;
    if (uri) {
      if (!uri.startsWith("file:") || !this.def.files.test(uriPath(uri))) {
        if (m.id !== undefined && this.listener) {
          const id = m.id;
          queueMicrotask(() => this.listener?.({ jsonrpc: "2.0", id, result: null }));
        }
        return;
      }
      this.uris.set(uriKey(uri), uri);
      // clangd: compile each file the way NppDev's Run does, so g++'s headers (bits/stdc++.h) resolve
      if (method === "textDocument/didOpen" && this.def.key === "cpp" && this.compiler) {
        const file = uriPath(uri);
        const isC = /\.c$/i.test(file);
        const command = [this.compiler, isC ? "-std=gnu17" : "-std=gnu++20", "-DONLINE_JUDGE", "-x", isC ? "c" : "c++", file];
        const dir = file.replace(/[\\/][^\\/]*$/, "");
        await this.post({ jsonrpc: "2.0", method: "workspace/didChangeConfiguration", params: { settings: { compilationDatabaseChanges: { [file]: { workingDirectory: dir, compilationCommand: command } } } } });
      }
    }
    await this.post(m);
  }
}

interface Running { id: number; dispose(): void }
const running = new Map<string, Promise<Running | null>>();
let warned = new Set<string>();
let onProblem: (text: string) => void = () => {};

/** Show "X isn't installed" messages through the app's toasts. */
export function onLspProblem(fn: (text: string) => void) { onProblem = fn; }

/** Start the server for this language (once per folder) the first time a file of it is opened. */
export function ensureLsp(lang: string, root: string | null): void {
  const key = LANG_TO_SERVER[lang];
  if (!key || running.has(key)) return;
  const def = SERVERS[key];
  running.set(key, (async () => {
    let started: { id: number; compiler: string | null };
    try {
      started = await invoke("lsp_start", { lang });
    } catch (e) {
      const why = def.missing[String(e)];
      if (why && !warned.has(key)) { warned.add(key); onProblem(why); }
      return null;
    }
    const { MonacoLspClient } = await import("@monaco-lsp");
    const transport = new TauriTransport(started.id, def, root, started.compiler);
    new MonacoLspClient(transport as never);
    return { id: started.id, dispose: () => invoke("lsp_stop", { id: started.id }) };
  })());
}

/** Stop all servers (another folder was opened: servers are per folder). */
export async function stopAllLsp() {
  const all = [...running.values()];
  running.clear();
  warned = new Set();
  for (const p of all) (await p)?.dispose();
}
