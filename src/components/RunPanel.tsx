// The Run panel: test cases for the active file, run them all (Ctrl+Enter) or one at a time,
// and see each verdict with time and memory, like an online judge.
import { useRef, type JSX } from "react";
import { useApp } from "../store";
import { useRun, canRun, TOOL_NAMES, type Test, type Verdict } from "../run";
import { baseName } from "../lib/api";
import { Icon } from "./Icons";

const VERDICT_TEXT: Record<Verdict, string> = {
  AC: "Accepted", WA: "Wrong Answer", RE: "Runtime Error", TLE: "Time Limit Exceeded", MLE: "Memory Limit Exceeded", OK: "Ran", STOPPED: "Stopped",
};

const lines = (s: string) => s.replace(/\r\n/g, "\n").replace(/\s+$/, "").split("\n").map((l) => l.trimEnd());

/** A textarea that grows with its content (2 to 14 lines). */
function Field({ label, value, onChange, placeholder, readOnly, mark }: { label: string; value: string; onChange?: (v: string) => void; placeholder?: string; readOnly?: boolean; mark?: JSX.Element }) {
  const rows = Math.max(2, Math.min(14, value.split("\n").length));
  return (
    <label className="field">
      <span className="field-label">{label}{mark}</span>
      <textarea
        spellCheck={false}
        rows={rows}
        value={value}
        readOnly={readOnly}
        placeholder={placeholder}
        onChange={(e) => onChange?.(e.target.value)}
        onKeyDown={(e) => {
          if (e.key === "Tab" && !readOnly) { // Tab types a tab, like an editor
            e.preventDefault();
            const t = e.currentTarget, a = t.selectionStart, b = t.selectionEnd;
            onChange?.(t.value.slice(0, a) + "\t" + t.value.slice(b));
            requestAnimationFrame(() => t.setSelectionRange(a + 1, a + 1));
          }
        }}
      />
    </label>
  );
}

/** The program's output; on Wrong Answer, lines that differ from the expected output are marked. */
function Output({ test }: { test: Test }) {
  const r = test.result!;
  const out = lines(r.output);
  const exp = lines(test.expected);
  const wa = r.verdict === "WA";
  return (
    <div className="field">
      <span className="field-label">Output{wa && <span className="hint-wa">differences marked</span>}</span>
      <pre className="output">
        {r.output.trim() === "" ? <span className="muted">(no output)</span> : out.map((l, i) => (
          <div key={i} className={wa && l !== (exp[i] ?? null) ? "diff" : undefined} title={wa && l !== exp[i] ? `Expected: ${exp[i] ?? "(nothing)"}` : undefined}>{l || " "}</div>
        ))}
        {wa && exp.length > out.length && <div className="diff missing">(missing {exp.length - out.length} more line{exp.length - out.length > 1 ? "s" : ""})</div>}
      </pre>
      {r.stderr.trim() && (
        <>
          <span className="field-label">Error output</span>
          <pre className="output stderr">{r.stderr.trimEnd()}</pre>
        </>
      )}
    </div>
  );
}

function TestCard({ test, n }: { test: Test; n: number }) {
  const { updateTest, removeTest, run, running } = useRun();
  const r = test.result;
  return (
    <section className={"test-card" + (r ? " v-" + r.verdict : "") + (test.pending ? " pending" : "")}>
      <header>
        <span className="test-title">Test {n}</span>
        {test.pending && <span className="badge running"><span className="spinner" />Running</span>}
        {r && !test.pending && <span className={"badge v-" + r.verdict} title={VERDICT_TEXT[r.verdict]}>{r.verdict === "OK" ? "Done" : r.verdict}</span>}
        {r && !test.pending && r.verdict !== "STOPPED" && <span className="stats">{r.timeMs} ms · {r.memoryKb >= 1024 ? (r.memoryKb / 1024).toFixed(1) + " MB" : r.memoryKb + " KB"}</span>}
        <div className="grow" />
        <button className="icon-btn" title="Run this test" disabled={running} onClick={() => run(test.id)}>{Icon.run(13)}</button>
        <button className="icon-btn" title="Delete this test" onClick={() => removeTest(test.id)}>{Icon.trash(14)}</button>
      </header>
      {r?.message && <div className={"test-message" + (r.verdict === "RE" ? " error" : "")}>{r.message}</div>}
      <Field label="Input" value={test.input} onChange={(v) => updateTest(test.id, { input: v })} placeholder="What the program reads (stdin)" />
      <Field label="Expected output" value={test.expected} onChange={(v) => updateTest(test.id, { expected: v })} placeholder="Optional: to check the answer" />
      {r && <Output test={test} />}
    </section>
  );
}

function Summary() {
  const { tests, running, phase } = useRun();
  const done = tests.filter((t) => t.result && !t.pending);
  if (running) return <div className="run-summary busy"><span className="spinner" />{phase === "compile" ? "Compiling…" : `Running test ${done.length + 1} of ${tests.filter((t) => t.pending).length + done.length}…`}</div>;
  if (!done.length) return null;
  const judged = done.filter((t) => t.result!.verdict !== "OK");
  const passed = done.filter((t) => t.result!.verdict === "AC").length;
  if (!judged.length) return <div className="run-summary">Ran {done.length} test{done.length > 1 ? "s" : ""}. Add expected outputs to check answers.</div>;
  const all = passed === judged.length;
  return <div className={"run-summary " + (all ? "pass" : "fail")}>{all ? `All ${passed} passed` : `${passed} of ${judged.length} passed`}</div>;
}

export function RunPanel() {
  const { active } = useApp();
  const st = useRun();
  const list = useRef<HTMLDivElement>(null);

  if (!active || !canRun(active)) {
    return (
      <aside className="run-panel" style={{ width: st.width }}>
        <div className="side-header">RUN<div className="side-actions"><button title="Close" onClick={() => st.toggle(false)}>{Icon.close(14)}</button></div></div>
        <div className="run-empty">
          <div className="run-empty-icon">{Icon.run(28)}</div>
          <p>Open a C, C++, Java, Python, JavaScript, Go, Rust, Kotlin or C# file to run it against test cases.</p>
        </div>
      </aside>
    );
  }

  const c = st.compile;
  return (
    <aside className="run-panel" style={{ width: st.width }}>
      <div className="side-header">
        Run <span className="side-file">{baseName(active)}</span>
        <div className="side-actions"><button title="Close" onClick={() => st.toggle(false)}>{Icon.close(14)}</button></div>
      </div>

      <div className="run-toolbar">
        {st.running
          ? <button className="btn danger run-btn" onClick={st.stop}>{Icon.stop(12)} Stop</button>
          : <button className="btn primary run-btn" onClick={() => st.run()} title="Run all tests (Ctrl+Enter)">{Icon.run(12)} Run all</button>}
        <div className="limits">
          <label title="Time limit per test"><input type="number" min={100} step={500} value={st.timeLimitMs} onChange={(e) => st.setLimits(+e.target.value, st.memoryLimitMb)} />ms</label>
          <label title="Memory limit"><input type="number" min={16} step={64} value={st.memoryLimitMb} onChange={(e) => st.setLimits(st.timeLimitMs, +e.target.value)} />MB</label>
        </div>
      </div>

      {st.problem && (
        <div className="problem">
          <span className="problem-name">{st.problem.name}</span>
          {st.problem.group && <span className="muted"> · {st.problem.group}</span>}
        </div>
      )}

      <Summary />

      <div className="run-list" ref={list}>
        {st.missing && (
          <div className="run-alert">
            <b>{TOOL_NAMES[st.missing] ?? st.missing} isn't installed.</b>
            <p>NppDev needs it to run this file. Install it, then press Run again.</p>
          </div>
        )}
        {st.error && <div className="run-alert"><p>{st.error}</p></div>}
        {c && !c.ok && !st.missing && (
          <div className="compile-error">
            <div className="field-label">Compilation error</div>
            <pre className="output stderr">{c.output || "The compiler failed without a message."}</pre>
          </div>
        )}
        {c?.ok && !c.cached && c.ms > 0 && <div className="compile-ok">Compiled in {(c.ms / 1000).toFixed(1)} s</div>}

        {st.tests.map((t, i) => <TestCard key={t.id} test={t} n={i + 1} />)}

        <button className="add-test" onClick={() => { st.addTest(); requestAnimationFrame(() => list.current?.scrollTo({ top: list.current.scrollHeight, behavior: "smooth" })); }}>
          {Icon.plus(14)} Add test case
        </button>
      </div>
    </aside>
  );
}
