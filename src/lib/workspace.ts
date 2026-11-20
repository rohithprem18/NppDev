// Open documents: one Monaco model per file, with its saved version and scroll/cursor state.
// Monaco (4 MB) is its own chunk: it starts loading here, at startup, without holding up the
// first paint. Every document is made after it has loaded, so `M()` is safe wherever a Doc exists.
import type { monaco } from "./editor";

export const monacoReady = import("./editor");
let loaded: typeof import("./editor") | null = null;
monacoReady.then((m) => { loaded = m; });
const M = () => loaded!.monaco;
import { api, baseName } from "./api";
import { fileType } from "./fileTypes";

interface Doc {
  model: monaco.editor.ITextModel;
  savedVersion: number;
  view: monaco.editor.ICodeEditorViewState | null;
}

const docs = new Map<string, Doc>();
const listeners = new Set<(path: string, dirty: boolean) => void>();
let untitledCount = 0;

export const isUntitled = (path: string) => path.startsWith("untitled:");

const editListeners = new Set<(path: string) => void>();

/** Called on every edit (auto save waits for a pause in typing). */
export function onEdit(fn: (path: string) => void) {
  editListeners.add(fn);
  return () => editListeners.delete(fn);
}

function track(path: string, doc: Doc) {
  docs.set(path, doc);
  let wasDirty = false;
  doc.model.onDidChangeContent(() => {
    editListeners.forEach((l) => l(path));
    const dirty = doc.model.getAlternativeVersionId() !== doc.savedVersion;
    if (dirty !== wasDirty) {
      wasDirty = dirty;
      listeners.forEach((l) => l(path, dirty));
    }
  });
}

/** Called when a file becomes changed / saved (the tabs show a dot). */
export function onDirtyChange(fn: (path: string, dirty: boolean) => void) {
  listeners.add(fn);
  return () => listeners.delete(fn);
}

export async function openDoc(path: string): Promise<Doc> {
  const open = docs.get(path);
  if (open) return open;
  const [text] = await Promise.all([api.readText(path), monacoReady]);
  const model = M().editor.createModel(text, fileType(baseName(path)).lang, M().Uri.file(path));
  const doc: Doc = { model, savedVersion: model.getAlternativeVersionId(), view: null };
  track(path, doc);
  return doc;
}

export async function newUntitled(): Promise<string> {
  await monacoReady;
  const path = `untitled:Untitled-${++untitledCount}`;
  const model = M().editor.createModel("", "plaintext", M().Uri.parse(`untitled:///Untitled-${untitledCount}`));
  track(path, { model, savedVersion: model.getAlternativeVersionId(), view: null });
  return path;
}

export const getDoc = (path: string) => docs.get(path);
export const isDirty = (path: string) => {
  const d = docs.get(path);
  return !!d && d.model.getAlternativeVersionId() !== d.savedVersion;
};

export async function saveDoc(path: string, asPath?: string): Promise<string> {
  const d = docs.get(path);
  if (!d) return path;
  const target = asPath ?? path;
  await api.writeText(target, d.model.getValue());
  if (target !== path) {                           // Save As: the document moves to its new name
    const model = M().editor.createModel(d.model.getValue(), fileType(baseName(target)).lang, M().Uri.file(target));
    d.model.dispose();
    docs.delete(path);
    const moved: Doc = { model, savedVersion: model.getAlternativeVersionId(), view: d.view };
    track(target, moved);
    return target;
  }
  d.savedVersion = d.model.getAlternativeVersionId();
  listeners.forEach((l) => l(path, false));
  return path;
}

export function closeDoc(path: string) {
  docs.get(path)?.model.dispose();
  docs.delete(path);
}

/** A file was renamed or moved in the Explorer: keep its open document. */
export function renameDoc(from: string, to: string) {
  const d = docs.get(from);
  if (!d) return;
  const model = M().editor.createModel(d.model.getValue(), fileType(baseName(to)).lang, M().Uri.file(to));
  const dirty = isDirty(from);
  d.model.dispose();
  docs.delete(from);
  const doc: Doc = { model, savedVersion: dirty ? -1 : model.getAlternativeVersionId(), view: d.view };
  track(to, doc);
}

/** Paths of all open documents under a folder (for closing them when it's deleted). */
export const docsUnder = (dir: string) => [...docs.keys()].filter((p) => p === dir || p.startsWith(dir + "\\") || p.startsWith(dir + "/"));
