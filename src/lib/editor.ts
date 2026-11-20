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

// Colours follow docs/DESIGN.md: ink and gray surfaces; syntax uses the design's own hues
// (magenta, cyan, violet, link blue), nothing else.
monaco.editor.defineTheme("nppdev-dark", {
  base: "vs-dark",
  inherit: true,
  rules: [
    { token: "", foreground: "ededed" },
    { token: "comment", foreground: "8f8f8f", fontStyle: "italic" },
    { token: "keyword", foreground: "ff5c9a" },
    { token: "keyword.directive", foreground: "ff5c9a" },
    { token: "string", foreground: "50e3c2" },
    { token: "number", foreground: "52a8ff" },
    { token: "type", foreground: "c9a6ff" },
    { token: "type.identifier", foreground: "c9a6ff" },
    { token: "delimiter", foreground: "a1a1a1" },
    { token: "identifier", foreground: "ededed" },
  ],
  colors: {
    "editor.background": "#0a0a0a",
    "editorBracketHighlight.foreground1": "#a1a1a1",
    "editorBracketHighlight.foreground2": "#a1a1a1",
    "editorBracketHighlight.foreground3": "#a1a1a1",
    "editorBracketHighlight.foreground4": "#a1a1a1",
    "editorBracketHighlight.foreground5": "#a1a1a1",
    "editorBracketHighlight.foreground6": "#a1a1a1",
    "editor.foreground": "#ededed",
    "editorLineNumber.foreground": "#5e5e5e",
    "editorLineNumber.activeForeground": "#ededed",
    "editor.lineHighlightBackground": "#141414",
    "editor.lineHighlightBorder": "#00000000",
    "editor.selectionBackground": "#0070f355",
    "editor.inactiveSelectionBackground": "#0070f322",
    "editorCursor.foreground": "#ededed",
    "editorIndentGuide.background1": "#1f1f1f",
    "editorIndentGuide.activeBackground1": "#3d3d3d",
    "editorWidget.background": "#111111",
    "editorWidget.border": "#2e2e2e",
    "editorHoverWidget.background": "#111111",
    "editorHoverWidget.border": "#2e2e2e",
    "editorSuggestWidget.background": "#111111",
    "editorSuggestWidget.border": "#2e2e2e",
    "editorSuggestWidget.selectedBackground": "#1f1f1f",
    "editorSuggestWidget.highlightForeground": "#52a8ff",
    "editorBracketMatch.border": "#5e5e5e",
    "editorBracketMatch.background": "#00000000",
    "scrollbarSlider.background": "#ffffff14",
    "scrollbarSlider.hoverBackground": "#ffffff24",
    "editorError.foreground": "#ff4d4d",
    "editorWarning.foreground": "#f5a623",
  },
});

monaco.editor.defineTheme("nppdev-light", {
  base: "vs",
  inherit: true,
  rules: [
    { token: "", foreground: "171717" },
    { token: "comment", foreground: "6f6f6f", fontStyle: "italic" },
    { token: "keyword", foreground: "c4155c" },
    { token: "keyword.directive", foreground: "c4155c" },
    { token: "string", foreground: "067a5f" },
    { token: "number", foreground: "0761d1" },
    { token: "type", foreground: "7928ca" },
    { token: "type.identifier", foreground: "7928ca" },
    { token: "delimiter", foreground: "4d4d4d" },
    { token: "identifier", foreground: "171717" },
  ],
  colors: {
    "editor.background": "#ffffff",
    "editorBracketHighlight.foreground1": "#4d4d4d",
    "editorBracketHighlight.foreground2": "#4d4d4d",
    "editorBracketHighlight.foreground3": "#4d4d4d",
    "editorBracketHighlight.foreground4": "#4d4d4d",
    "editorBracketHighlight.foreground5": "#4d4d4d",
    "editorBracketHighlight.foreground6": "#4d4d4d",
    "editor.foreground": "#171717",
    "editorLineNumber.foreground": "#a1a1a1",
    "editorLineNumber.activeForeground": "#171717",
    "editor.lineHighlightBackground": "#fafafa",
    "editor.lineHighlightBorder": "#00000000",
    "editor.selectionBackground": "#d3e5ff",
    "editorCursor.foreground": "#171717",
    "editorIndentGuide.background1": "#ebebeb",
    "editorIndentGuide.activeBackground1": "#a1a1a1",
    "editorWidget.background": "#ffffff",
    "editorWidget.border": "#ebebeb",
    "editorSuggestWidget.background": "#ffffff",
    "editorSuggestWidget.border": "#ebebeb",
    "editorSuggestWidget.selectedBackground": "#f5f5f5",
    "editorSuggestWidget.highlightForeground": "#0070f3",
    "editorBracketMatch.border": "#a1a1a1",
    "editorBracketMatch.background": "#00000000",
    "scrollbarSlider.background": "#0000000f",
    "scrollbarSlider.hoverBackground": "#0000001f",
    "editorError.foreground": "#ee0000",
    "editorWarning.foreground": "#ab570a",
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
  bracketPairColorization: { enabled: false },
  guides: { bracketPairs: false, indentation: true },
  padding: { top: 8 },
  tabSize: 4,
  fixedOverflowWidgets: true,
};

export { monaco };
