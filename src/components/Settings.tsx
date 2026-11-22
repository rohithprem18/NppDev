// Settings (the gear in the left bar): theme, editor font, updates, and whether each language's
// compiler and IntelliSense work, with one-click installs.
import { useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { getVersion } from "@tauri-apps/api/app";
import { useApp } from "../store";
import { checkForUpdate } from "../lib/update";
import { Icon } from "./Icons";

export const EDITOR_FONTS = ["Hack", "Cascadia Code", "Consolas", "JetBrains Mono", "Fira Code", "Source Code Pro", "Ubuntu Mono", "DejaVu Sans Mono"];

interface Lang {
  key: string;
  name: string;
  role: string;
  ok: boolean;
  detail?: string | null;
  install?: string | null;
}

function Languages() {
  const [rows, setRows] = useState<Lang[] | null>(null);
  const [busy, setBusy] = useState<string | null>(null);
  const notify = useApp((s) => s.notify);
  const load = (refresh: boolean) => { setRows(null); invoke<Lang[]>("language_status", { refresh }).then(setRows).catch(() => setRows([])); };
  useEffect(() => load(false), []);

  const install = async (key: string) => {
    setBusy(key);
    try {
      notify(await invoke<string>("install_language", { key }));
      load(true);
    } catch (e) {
      notify(String(e), "error");
    } finally {
      setBusy(null);
    }
  };

  return (
    <section className="set-section">
      <div className="set-title">
        <h3>Languages</h3>
        <button className="icon-btn" title="Check again" aria-label="Check again" onClick={() => load(true)}>{Icon.refresh(14)}</button>
      </div>
      {rows === null && <p className="set-hint">Checking…</p>}
      {rows?.map((r) => (
        <div key={r.key} className="lang-row">
          <span className={"lang-mark " + (r.ok ? "ok" : "no")}>{r.ok ? Icon.check(12) : Icon.cross(11)}</span>
          <div className="lang-text">
            <div className="lang-name">{r.name} <span className="muted">· {r.role}</span></div>
            <div className="lang-detail" title={r.detail ?? undefined}>{r.ok ? r.detail : r.detail ?? "Not installed"}</div>
            {!r.ok && r.install && r.install.startsWith("sudo") && <code className="lang-cmd">{r.install}</code>}
          </div>
          {!r.ok && (r.install === "auto" || r.install === "winget") && (
            <button className="btn small" disabled={busy !== null} onClick={() => install(r.key)}>
              {busy === r.key ? <span className="spinner" /> : "Install"}
            </button>
          )}
        </div>
      ))}
    </section>
  );
}

export function Settings() {
  const theme = useApp((s) => s.theme);
  const fontSize = useApp((s) => s.fontSize);
  const editorFont = useApp((s) => s.editorFont);
  const autoSave = useApp((s) => s.autoSave);
  const { setTheme, setFontSize, setEditorFont, setAutoSave } = useApp.getState();
  const [version, setVersion] = useState("");
  const [checking, setChecking] = useState(false);
  useEffect(() => { getVersion().then(setVersion).catch(() => {}); }, []);

  return (
    <div className="settings">
      <div className="side-header"><span>Settings</span></div>
      <div className="settings-body">
        <section className="set-section">
          <h3>Appearance</h3>
          <div className="set-row">
            <span>Theme</span>
            <div className="segmented">
              <button className={theme === "dark" ? "on" : ""} aria-pressed={theme === "dark"} onClick={() => setTheme("dark")}>{Icon.moon(13)} Dark</button>
              <button className={theme === "light" ? "on" : ""} aria-pressed={theme === "light"} onClick={() => setTheme("light")}>{Icon.sun(13)} Light</button>
            </div>
          </div>
          <div className="set-row">
            <span>Editor font</span>
            <select className="select" value={editorFont} onChange={(e) => setEditorFont(e.target.value)}>
              {EDITOR_FONTS.map((f) => <option key={f} value={f}>{f}{f === "Hack" ? " (built in)" : ""}</option>)}
            </select>
          </div>
          <div className="set-row">
            <span>Font size</span>
            <div className="stepper">
              <button title="Smaller (Ctrl+-)" aria-label="Smaller (Ctrl+-)" onClick={() => setFontSize(fontSize - 1)}>−</button>
              <span>{fontSize}</span>
              <button title="Bigger (Ctrl+=)" aria-label="Bigger (Ctrl+=)" onClick={() => setFontSize(fontSize + 1)}>+</button>
            </div>
          </div>
          <p className="set-hint">Fonts other than Hack are used if they're installed on this computer.</p>
        </section>

        <section className="set-section">
          <h3>Files</h3>
          <div className="set-row">
            <span>Auto save</span>
            <div className="segmented">
              <button className={autoSave ? "on" : ""} aria-pressed={autoSave} onClick={() => setAutoSave(true)}>On</button>
              <button className={!autoSave ? "on" : ""} aria-pressed={!autoSave} onClick={() => setAutoSave(false)}>Off</button>
            </div>
          </div>
          <p className="set-hint">Saves a second after you stop typing, and when you switch files or windows. New files are saved once you name them (Ctrl+S).</p>
        </section>

        <section className="set-section">
          <h3>Updates</h3>
          <div className="set-row">
            <span>NppDev {version}</span>
            <button className="btn small" disabled={checking} onClick={async () => { setChecking(true); await checkForUpdate(true); setChecking(false); }}>
              {checking ? <span className="spinner" /> : "Check for updates"}
            </button>
          </div>
          <p className="set-hint">NppDev also checks when it starts, and asks before updating.</p>
        </section>

        <Languages />
      </div>
    </div>
  );
}
