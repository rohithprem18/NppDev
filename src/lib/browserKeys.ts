// NppDev runs in a web view, and some browser shortcuts still reach it: F5 / Ctrl+R would
// reload the whole app, Ctrl+F outside the editor opens a find bar, F7 asks about caret
// browsing, Alt+Left goes "back". This checks keys after the editor and NppDev have handled
// theirs, and cancels the browser's action for any of these that are left.
const BLOCKED = new Set(["f3", "f5", "f7", "browserback", "browserforward", "browserrefresh"]);
const BLOCKED_CTRL = new Set(["r", "f", "g", "h", "j", "u", "p", "s", "o", "n", "w", "t", "+", "=", "-", "0"]);

export function blockBrowserShortcuts() {
  window.addEventListener("keydown", (e) => {
    const k = e.key.toLowerCase();
    const ctrl = e.ctrlKey || e.metaKey;
    if (BLOCKED.has(k) || (ctrl && BLOCKED_CTRL.has(k)) || (ctrl && e.shiftKey && ["i", "j", "c", "r", "delete"].includes(k)) || (e.altKey && (k === "arrowleft" || k === "arrowright") && !(e.target as HTMLElement)?.closest?.(".monaco-editor"))) {
      e.preventDefault();
    }
  });
}
