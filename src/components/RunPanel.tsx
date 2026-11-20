// The Run panel, laid out like HackerRank's: a result line, a row of test-case tabs with a
// pass/fail mark, and the selected case's input, your output and the expected output.
import { useEffect, useRef, useState } from "react";
import { useApp } from "../store";
import { useRun, canRun, TOOL_NAMES, type Test, type Verdict } from "../run";
import { baseName } from "../lib/api";
import { Icon } from "./Icons";

const VERDICT_TEXT: Record<Verdict, string> = {
  AC: "Accepted", WA: "Wrong Answer", RE: "Runtime Error", TLE: "Time Limit Exceeded", MLE: "Memory Limit Exceeded", OK: "Ran", STOPPED: "Stopped",
};

const lines = (s: string) => s.replace(/\r\n/g, "\n").replace(/\s+$/, "").split("\n").map((l) => l.trimEnd());
const memory = (kb: number) => (kb >= 1024 ? (kb / 1024).toFixed(1) + " MB" : kb + " KB");

/** An editable box that grows with its content (3 to 16 lines); Tab types a tab. */
function Box({ label, value, onChange, placeholder }: { label: string; value: string; onChange: (v: string) => void; placeholder?: string }) {
  const rows = Math.max(3, Math.min(16, value.split("\n").length + 1));
  return (
    <section className="case-section">
      <h4>{label}</h4>
      <textarea
        className="case-box"
        spellCheck={false}
        rows={rows}
        value={value}
        placeholder={placeholder}
        onChange={(e) => onChange(e.target.value)}
        onKeyDown={(e) => {
          if (e.key === "Tab") {
            e.preventDefault();
            const t = e.currentTarget, a = t.selectionStart, b = t.selectionEnd;
            onChange(t.value.slice(0, a) + "\t" + t.value.slice(b));
            requestAnimationFrame(() => t.setSelectionRange(a + 1, a + 1));
          }
        }}
      />
    </section>
  );
}

/** Your output; on Wrong Answer the lines that differ from the expected output are marked. */
function YourOutput({ test }: { test: Test }) {
  const r = test.result!;
  const out = lines(r.output);
  const exp = lines(test.expected);
  const wa = r.verdict === "WA";
  return (
    <section className="case-section">
      <h4>Your output (stdout)</h4>
      <pre className="case-box output">
        {r.output.trim() === "" ? <span className="muted">No output</span> : out.map((l, i) => {
          const differs = wa && l !== (exp[i] ?? null);
          return <div key={i} className={differs ? "diff" : undefined} title={differs ? `Expected: ${exp[i] ?? "nothing"}` : undefined}>{l || " "}</div>;
        })}
        {wa && exp.length > out.length && <div className="diff missing">{exp.length - out.length} more line{exp.length - out.length > 1 ? "s" : ""} expected</div>}
      </pre>
    </section>
  );
}

function Mark({ test }: { test: Test }) {
  if (test.pending) return <span className="mark"><span className="spinner" /></span>;
  const v = test.result?.verdict;
  if (!v) return <span className="mark dot" />;
  if (v === "AC") return <span className="mark ok">{Icon.check(12)}</span>;
  if (v === "OK" || v === "STOPPED") return <span className="mark dot done" />;
  return <span className={"mark " + (v === "TLE" || v === "MLE" ? "warn" : "bad")}>{Icon.cross(11)}</span>;
}

function Result() {
  const { tests, running, phase, compile, missing, error } = useRun();
  if (running) return <div className="result busy"><span className="spinner" />{phase === "compile" ? "Compiling" : "Running test cases"}</div>;
  if (missing) return <div className="result bad">{TOOL_NAMES[missing] ?? missing} isn't installed</div>;
  if (error) return <div className="result bad">Can't run this file</div>;
  if (compile && !compile.ok) return <div className="result bad">Compilation error</div>;
  const done = tests.filter((t) => t.result);
  if (!done.length) return null;
  const judged = done.filter((t) => t.result!.verdict !== "OK" && t.result!.verdict !== "STOPPED");
  const failed = judged.filter((t) => t.result!.verdict !== "AC").length;
  if (!judged.length) return <div className="result">Ran {done.length} test case{done.length > 1 ? "s" : ""}. Add expected outputs to check them.</div>;
  return failed === 0
    ? <div className="result ok">{judged.length === 1 ? "Test case passed" : `All ${judged.length} test cases passed`}</div>
    : <div className="result bad">{failed} of {judged.length} test case{judged.length > 1 ? "s" : ""} failed</div>;
}

export function RunPanel() {
  const { active } = useApp();
  const st = useRun();
  const [selected, setSelected] = useState<number | null>(null);
  const wasRunning = useRef(false);

  // after a run, open the first failing case (like HackerRank)
  useEffect(() => {
    if (wasRunning.current && !st.running) {
      const bad = st.tests.find((t) => t.result && !["AC", "OK", "STOPPED"].includes(t.result.verdict));
      if (bad) setSelected(bad.id);
    }
    wasRunning.current = st.running;
  }, [st.running, st.tests]);

  if (!active || !canRun(active)) {
    return (
      <aside className="run-panel" style={{ width: st.width }}>
        <div className="panel-head"><span className="panel-title">Test cases</span><button className="icon-btn" title="Close" onClick={() => st.toggle(false)}>{Icon.close(14)}</button></div>
        <div className="run-empty">Open a C, C++, Java, Python, JavaScript, Go, Rust, Kotlin or C# file to run it against test cases.</div>
      </aside>
    );
  }

  const test = st.tests.find((t) => t.id === selected) ?? st.tests[0];
  const index = test ? st.tests.indexOf(test) : 0;
  const r = test?.result;
  const c = st.compile;

  return (
    <aside className="run-panel" style={{ width: st.width }}>
      <div className="panel-head">
        <span className="panel-title" title={active}>{st.problem?.name ?? baseName(active)}</span>
        <button className="icon-btn" title="Close (Ctrl+Shift+B)" onClick={() => st.toggle(false)}>{Icon.close(14)}</button>
      </div>

      <div className="run-toolbar">
        {st.running
          ? <button className="btn danger run-btn" onClick={st.stop}>{Icon.stop(12)} Stop</button>
          : <button className="btn primary run-btn" onClick={() => st.run()} title="Run all test cases (Ctrl+Enter)">{Icon.run(12)} Run</button>}
        <div className="limits">
          <label title="Time limit per test case"><input type="number" min={100} step={500} value={st.timeLimitMs} onChange={(e) => st.setLimits(+e.target.value, st.memoryLimitMb)} /> ms</label>
          <label title="Memory limit"><input type="number" min={16} step={64} value={st.memoryLimitMb} onChange={(e) => st.setLimits(st.timeLimitMs, +e.target.value)} /> MB</label>
        </div>
      </div>

      <Result />

      <div className="run-body">
        {st.missing && <p className="note">NppDev needs {TOOL_NAMES[st.missing] ?? st.missing} to run this file. Install it, then run again.</p>}
        {st.error && <p className="note">{st.error}</p>}
        {c && !c.ok && !st.missing && (
          <section className="case-section">
            <h4>Compiler message</h4>
            <pre className="case-box output stderr">{c.output || "The compiler failed without a message."}</pre>
          </section>
        )}

        <div className="case-tabs" role="tablist">
          {st.tests.map((t, i) => (
            <button key={t.id} role="tab" aria-selected={t === test} className={"case-tab" + (t === test ? " selected" : "")} onClick={() => setSelected(t.id)}>
              <Mark test={t} />Test case {i + 1}
            </button>
          ))}
          <button className="case-tab add" title="Add a test case" onClick={() => { st.addTest(); setTimeout(() => setSelected(useRun.getState().tests.slice(-1)[0]?.id ?? null), 0); }}>{Icon.plus(13)}</button>
        </div>

        {test && (
          <div className="case">
            <div className="case-head">
              {r && !test.pending
                ? <span className={"verdict v-" + r.verdict}>{VERDICT_TEXT[r.verdict]}</span>
                : <span className="verdict muted">{test.pending ? "Running" : "Not run yet"}</span>}
              {r && !test.pending && r.verdict !== "STOPPED" && <span className="stats">{r.timeMs} ms, {memory(r.memoryKb)}</span>}
              <div className="grow" />
              <button className="icon-btn" title="Run this test case" disabled={st.running} onClick={() => st.run(test.id)}>{Icon.run(12)}</button>
              <button className="icon-btn" title="Delete this test case" onClick={() => st.removeTest(test.id)}>{Icon.trash(14)}</button>
            </div>
            {r?.message && <p className={"note" + (r.verdict === "RE" ? " bad" : "")}>{r.message}</p>}
            <Box label="Input (stdin)" value={test.input} onChange={(v) => st.updateTest(test.id, { input: v })} placeholder={`What the program reads for test case ${index + 1}`} />
            {r && !test.pending && <YourOutput test={test} />}
            <Box label="Expected output" value={test.expected} onChange={(v) => st.updateTest(test.id, { expected: v })} placeholder="Optional: the answer to check against" />
            {r?.stderr.trim() && (
              <section className="case-section">
                <h4>Error output (stderr)</h4>
                <pre className="case-box output stderr">{r.stderr.trimEnd()}</pre>
              </section>
            )}
          </div>
        )}
      </div>
    </aside>
  );
}
