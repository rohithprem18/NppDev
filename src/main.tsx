import { createRoot } from "react-dom/client";
import App from "./App";
import "./styles.css";

// The context menu is NppDev's own (right-click in the Explorer); not the browser's.
window.addEventListener("contextmenu", (e) => {
  const t = e.target as HTMLElement;
  if (!t.closest("input, textarea, .monaco-editor")) e.preventDefault();
});

// The editor measures character widths when it starts. If Hack isn't loaded yet it measures the
// fallback font, and the caret and highlights then sit slightly beside the real letters. So the
// code font is loaded first (never waiting more than a second).
const codeFont = Promise.all(["400 15px Hack", "700 15px Hack"].map((f) => document.fonts.load(f)));
const timeout = new Promise((done) => setTimeout(done, 1000));
Promise.race([codeFont, timeout]).finally(() => createRoot(document.getElementById("root")!).render(<App />));
