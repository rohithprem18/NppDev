import { createRoot } from "react-dom/client";
import App from "./App";
import "./styles.css";
import { blockBrowserShortcuts } from "./lib/browserKeys";

blockBrowserShortcuts();

// The context menu is NppDev's own (right-click in the Explorer); not the browser's.
window.addEventListener("contextmenu", (e) => {
  const t = e.target as HTMLElement;
  if (!t.closest("input, textarea, .monaco-editor")) e.preventDefault();
});

// Draw at once. The editor re-measures its font the moment Hack finishes loading
// (see EditorArea), so the caret still lines up exactly.
createRoot(document.getElementById("root")!).render(<App />);
