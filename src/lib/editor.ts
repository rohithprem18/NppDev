// Monaco setup: web workers, NppDev's dark and light themes, and editor defaults.
import * as monaco from "./monaco";
import EditorWorker from "@monaco/editor/editor.worker.js?worker";
import TsWorker from "@monaco/languages/features/typescript/ts.worker.js?worker";
import JsonWorker from "@monaco/languages/features/json/json.worker.js?worker";

(self as unknown as { MonacoEnvironment: unknown }).MonacoEnvironment = {
  getWorker(_id: string, label: string) {
    if (label === "typescript" || label === "javascript") return new TsWorker();
    if (label === "json") return new JsonWorker();
    return new EditorWorker();
  },
};

monaco.editor.defineTheme("nppdev-dark", {
  base: "vs-dark",
  inherit: true,
  rules: [
    { token: "comment", foreground: "8b949e", fontStyle: "italic" },
    { token: "keyword", foreground: "ff7b72" },
    { token: "string", foreground: "a5d6ff" },
    { token: "number", foreground: "79c0ff" },
    { token: "type", foreground: "ffa657" },
    { token: "type.identifier", foreground: "ffa657" },
    { token: "delimiter", foreground: "c9d1d9" },
    { token: "identifier", foreground: "c9d1d9" },
    { token: "keyword.directive", foreground: "ff7b72" },
  ],
  colors: {
    "editor.background": "#0d1117",
    "editor.foreground": "#c9d1d9",
    "editorLineNumber.foreground": "#484f58",
    "editorLineNumber.activeForeground": "#c9d1d9",
    "editor.lineHighlightBackground": "#161b22",
    "editor.selectionBackground": "#264f78",
    "editorCursor.foreground": "#58a6ff",
    "editorIndentGuide.background1": "#21262d",
    "editorWidget.background": "#161b22",
    "editorWidget.border": "#30363d",
    "editorSuggestWidget.background": "#161b22",
    "editorSuggestWidget.selectedBackground": "#1f6feb44",
    "scrollbarSlider.background": "#484f5833",
  },
});

monaco.editor.defineTheme("nppdev-light", {
  base: "vs",
  inherit: true,
  rules: [
    { token: "comment", foreground: "6e7781", fontStyle: "italic" },
    { token: "keyword", foreground: "cf222e" },
    { token: "string", foreground: "0a3069" },
    { token: "number", foreground: "0550ae" },
    { token: "type", foreground: "953800" },
  ],
  colors: {
    "editor.background": "#ffffff",
    "editor.lineHighlightBackground": "#f6f8fa",
    "editorLineNumber.foreground": "#8c959f",
    "editorCursor.foreground": "#0969da",
  },
});

export const EDITOR_OPTIONS: monaco.editor.IStandaloneEditorConstructionOptions = {
  fontFamily: "Hack, 'Cascadia Code', Consolas, 'Ubuntu Mono', monospace",
  fontSize: 15,
  lineHeight: 22,
  automaticLayout: true,
  minimap: { enabled: false },
  smoothScrolling: true,
  cursorBlinking: "smooth",
  cursorSmoothCaretAnimation: "on",
  renderLineHighlight: "all",
  scrollBeyondLastLine: false,
  bracketPairColorization: { enabled: true },
  guides: { bracketPairs: true, indentation: true },
  padding: { top: 8 },
  tabSize: 4,
  fixedOverflowWidgets: true,
};

export { monaco };
