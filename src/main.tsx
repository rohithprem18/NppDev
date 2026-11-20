import { createRoot } from "react-dom/client";
import App from "./App";
import "./styles.css";

// The context menu is NppDev's own (right-click in the Explorer); not the browser's.
window.addEventListener("contextmenu", (e) => {
  const t = e.target as HTMLElement;
  if (!t.closest("input, textarea, .monaco-editor")) e.preventDefault();
});

createRoot(document.getElementById("root")!).render(<App />);
