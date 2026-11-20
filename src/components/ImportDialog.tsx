// "Import Problem": paste a problem link, pick a language. Also explains Competitive Companion.
import { useEffect, useRef, useState } from "react";
import { useApp } from "../store";
import { useImport, SITES } from "../import";
import { LANGS } from "../lib/templates";

export function ImportDialog() {
  const { open, busy, error, port, show, fromUrl } = useImport();
  const defaultLang = useApp((s) => s.defaultLang);
  const setDefaultLang = useApp((s) => s.setDefaultLang);
  const [url, setUrl] = useState("");
  const [lang, setLang] = useState(defaultLang);
  const input = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (!open) return;
    setLang(defaultLang);
    requestAnimationFrame(() => input.current?.select());
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open]);

  if (!open) return null;
  const submit = () => { if (url.trim() && !busy) fromUrl(url.trim(), lang); };

  return (
    <div className="modal-backdrop" onMouseDown={() => !busy && show(false)} onKeyDown={(e) => { if (e.key === "Escape" && !busy) show(false); }}>
      <div className="modal import" role="dialog" aria-modal="true" aria-label="Import problem" onMouseDown={(e) => e.stopPropagation()}>
        <h3>Import a problem</h3>
        <p>Paste a problem link. NppDev creates the solution file in your folder and adds the sample tests.</p>
        <form onSubmit={(e) => { e.preventDefault(); submit(); }}>
          <input
            ref={input}
            className="text-input"
            placeholder="https://codeforces.com/contest/4/problem/A"
            value={url}
            onChange={(e) => setUrl(e.target.value)}
            disabled={busy}
            spellCheck={false}
            autoFocus
          />
          <div className="import-row">
            <label className="muted">Language
              <select className="select" value={lang} onChange={(e) => { setLang(e.target.value); setDefaultLang(e.target.value); }} disabled={busy}>
                {LANGS.map((l) => <option key={l.id} value={l.id}>{l.name}</option>)}
              </select>
            </label>
            <div className="grow" />
            <button type="button" className="btn" onClick={() => show(false)} disabled={busy}>Cancel</button>
            <button type="submit" className="btn primary" disabled={busy || !url.trim()}>{busy ? <><span className="spinner" /> Importing…</> : "Import"}</button>
          </div>
        </form>
        {error && <div className="import-error">{error}</div>}
        <div className="sites">{SITES.map((s) => <span key={s}>{s}</span>)}</div>
        <div className="companion">
          <b>One click from the browser:</b> install the free <i>Competitive Companion</i> extension, open any problem and press its green <b>+</b> button.
          {" "}{port ? <span className="ok">NppDev is listening (port {port}).</span> : <span className="bad">Another app is using its ports, so NppDev can't receive problems from it.</span>}
          {" "}Works with 100+ judges, including all the sites above.
        </div>
      </div>
    </div>
  );
}
