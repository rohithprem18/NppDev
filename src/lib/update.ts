// Update check: on start (and from the Command Palette) NppDev asks GitHub for the latest release.
// When it is newer, a box offers Update now / Skip this version / Later. "Update now" runs the
// same one-line installer as a first install (verified download), then NppDev reopens updated.
import { getVersion } from "@tauri-apps/api/app";
import { invoke } from "@tauri-apps/api/core";
import { getCurrentWindow } from "@tauri-apps/api/window";
import { useApp } from "../store";

const REPO = "rohithprem18/NppDev";

/** Is version a newer than b? ("2.1.0" > "2.0.10" > "2.0.9"; a leading "v" is ignored) */
export function isNewer(a: string, b: string): boolean {
  const parts = (v: string) => v.replace(/^v/i, "").split(/[.-]/).map((x) => parseInt(x, 10) || 0);
  const x = parts(a), y = parts(b);
  for (let i = 0; i < Math.max(x.length, y.length); i++) {
    if ((x[i] ?? 0) !== (y[i] ?? 0)) return (x[i] ?? 0) > (y[i] ?? 0);
  }
  return false;
}

/** `manual`: from the Command Palette, so it also says when there is nothing new. */
export async function checkForUpdate(manual = false) {
  const app = useApp.getState();
  let current: string;
  let release: { tag_name?: string; body?: string; html_url?: string };
  try {
    current = await getVersion();
    const r = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, { headers: { Accept: "application/vnd.github+json" } });
    if (!r.ok) throw new Error(String(r.status));
    release = await r.json();
  } catch {
    if (manual) app.notify("Couldn't reach GitHub to check for updates.", "error");
    return;
  }
  const latest = String(release.tag_name ?? "").replace(/^v/i, "");
  if (!latest || !isNewer(latest, current)) {
    if (manual) app.notify(`You have the latest NppDev (${current}).`);
    return;
  }
  if (!manual && useApp.getState().skippedVersion === latest) return;

  // the first few lines of the release notes, as plain text
  const notes = (release.body ?? "").replace(/\r/g, "").split("\n")
    .map((l) => l.replace(/^[#>*\-\s]+/, "").replace(/[*_`]/g, "").trim()).filter(Boolean).slice(0, 3).join(" ");
  const choice = await app.ask({
    title: `NppDev ${latest} is available`,
    message: `You have ${current}.${notes ? " " + notes : ""} ${navigator.userAgent.includes("Windows") ? "Updating closes NppDev and opens the new version." : "The update installs in a terminal; restart NppDev afterwards."}`,
    // the last button is what Escape picks: Later, never Skip
    buttons: [{ label: "Update now", value: "update", primary: true }, { label: "Skip this version", value: "skip" }, { label: "Later", value: "cancel" }],
  });
  if (choice === "skip") app.setSkippedVersion(latest);
  if (choice === "update") {
    try {
      await invoke("start_update", { url: release.html_url ?? `https://github.com/${REPO}/releases/latest` });
      // Windows: the installer waits for NppDev to close (unsaved files are asked about first).
      // Ubuntu: apt updates it in place in a terminal; the new version starts next time.
      if (navigator.userAgent.includes("Windows")) await getCurrentWindow().close();
    } catch (e) {
      app.notify(`The update couldn't start: ${e}`, "error");
    }
  }
}
