// The Run panel, like an online compiler: Input (stdin), Output, and an optional Expected
// output. The text boxes keep their own text while you type (the app only listens), so the
// cursor never jumps.
import { memo } from "react";
import { useRun, canRun, TOOL_NAMES, type Verdict } from "../run";
import { useApp } from "../store";
import { Icon } from "./Icons";

const VERDICT_TEXT: Partial<Record<Verdict, string>> = {
  AC: "Accepted", WA: "Wrong Answer", RE: "Runtime Error", TLE: "Time Limit Exceeded", MLE: "Memory Limit Exceeded", STOPPED: "Stopped",
};

const lines = (s: string) => s.replace(/\r\n/g, "\n").replace(/\s+$/, "").split("\n").map((l) => l.trimEnd());
const memory = (kb: number) => (kb >= 1024 ? (kb / 1024).toFixed(1) + " MB" : kb + " KB");

/** A plain text box that owns its text; `version` resets it when another file is shown. */
const TextPane = memo(function TextPane({ value, version, onText, placeholder }: { value: string; version: number; onText: (t: string) => void; placeholder: string }) {
  return (
    <textarea
      key={version}
      className="pane-text"
      defaultValue={value}
      spellCheck={false}
      autoCapitalize="off"
      autoCorrect="off"
      placeholder={placeholder}
      onInput={(e) => onText(e.currentTarget.value)}
      onKeyDown={(e) => {
        if (e.key === "Tab" && !e.shiftKey && !e.ctrlKey) { // Tab types a tab, like an editor
          e.preventDefault();
          document.execCommand("insertText", false, "\t"); // keeps undo working
        }
      }}
    />
  );
});

function OutputPane() {
  const r = useRun((s) => s.result);
  const running = useRun((s) => s.running);
  const c = useRun((s) => s.compile);
  const missing = useRun((s) => s.missing);
  const error = useRun((s) => s.error);
  const expected = useRun((s) => s.expected);
  if (running) return <div className="pane-body muted">Running…</div>;
  if (missing) return <div className="pane-body">NppDev needs {TOOL_NAMES[missing] ?? missing} to run this file. Install it, then run again.</div>;
  if (error) return <div className="pane-body bad">{error}</div>;
  if (c && !c.ok) return <pre className="pane-body pane-pre bad">{c.output || "The compiler failed without a message."}</pre>;
  if (!r) return <div className="pane-body muted">Press Ctrl+Enter to run.</div>;
  const out = lines(r.output);
  const exp = lines(expected);
  const wa = r.verdict === "WA";
  return (
    <pre className="pane-body pane-pre">
      {r.message && <div className={"pane-note" + (r.verdict === "RE" ? " bad" : "")}>{r.message}</div>}
      {r.output.trim() === "" && !r.stderr.trim() ? <span className="muted">No output</span> : r.output.trim() !== "" && out.map((l, i) => {
        const differs = wa && l !== (exp[i] ?? null);
        return <div key={i} className={differs ? "diff" : undefined} title={differs ? `Expected: ${exp[i] ?? "nothing"}` : undefined}>{l || " "}</div>;
      })}
      {wa && exp.length > out.length && <div className="diff missing">{exp.length - out.length} more line{exp.length - out.length > 1 ? "s" : ""} expected</div>}
      {r.stderr.trim() && <div className="bad pane-stderr">{r.stderr.trimEnd()}</div>}
    </pre>
  );
}

function OutputHead() {
  const r = useRun((s) => s.result);
  const running = useRun((s) => s.running);
  const label = r && !running ? VERDICT_TEXT[r.verdict] : undefined;
  return (
    <div className="pane-head">
      <span>Output</span>
      {label && <span className={"pane-verdict v-" + r!.verdict}>{label}</span>}
      {r && !running && r.verdict !== "STOPPED" && <span className="pane-meta">{r.timeMs} ms · {memory(r.memoryKb)}</span>}
    </div>
  );
}

export function RunPanel() {
  const active = useApp((s) => s.active);
  const width = useRun((s) => s.width);
  const version = useRun((s) => s.version);
  const running = useRun((s) => s.running); // the Run / Stop button is on the tab bar
  const tl = useRun((s) => s.timeLimitMs);
  const ml = useRun((s) => s.memoryLimitMb);
  const st = useRun.getState(); // input/expected are read here only when `version` changes

  if (!active || !canRun(active)) {
    return (
      <aside className="run-panel" style={{ width }}>
        <div className="panel-head"><span className="panel-title">Run</span><div className="grow" /><button className="icon-btn" title="Close" onClick={() => st.toggle(false)}>{Icon.close(14)}</button></div>
        <div className="run-empty">Open a C, C++, Java, Python, JavaScript, Go, Rust, Kotlin or C# file to run it.</div>
      </aside>
    );
  }

  return (
    <aside className="run-panel" style={{ width }}>
      <div className="panel-head">
        <span className="panel-title">{running ? "Running…" : "Run"}</span>
        <div className="grow" />
        <label className="limit" title="Time limit"><input type="number" min={100} step={500} value={tl} onChange={(e) => st.setLimits(+e.target.value, ml)} /> ms</label>
        <label className="limit" title="Memory limit"><input type="number" min={16} step={64} value={ml} onChange={(e) => st.setLimits(tl, +e.target.value)} /> MB</label>
        <button className="icon-btn" title="Close (Ctrl+Shift+B)" onClick={() => st.toggle(false)}>{Icon.close(14)}</button>
      </div>

      <section className="pane input">
        <div className="pane-head"><span>Input</span><span className="pane-meta">stdin</span></div>
        <TextPane value={st.input} version={version} onText={st.setInput} placeholder="What your program reads" />
      </section>

      <section className="pane output">
        <OutputHead />
        <OutputPane />
      </section>

      <section className="pane expected">
        <div className="pane-head"><span>Expected output</span><span className="pane-meta">optional</span></div>
        <TextPane value={st.expected} version={version} onText={st.setExpected} placeholder="The answer, to check your output against" />
      </section>
    </aside>
  );
}
