// The sample database in its own tab ("sample.db"): every table, each with all of its rows, the
// type of every column, its keys, and the CREATE statement. It shows the database as your queries
// left it (it reloads after every run); Reset puts the original data back.
import { useEffect, useRef, useState } from "react";
import * as ws from "../lib/workspace";
import { baseName } from "../lib/api";
import { useApp } from "../store";
import { sqlSchema, schemaLoaded, sqlTable, tableLoaded, useDbView, insertSql, resetDb, type TableData, type SqlColumn } from "../lib/sqlSchema";

const isNum = (c: SqlColumn) => /INT|REAL|NUM|DEC|FLOAT|DOUBLE/i.test(c.ty);

export function DatabaseView() {
  const table = useDbView((s) => s.table);
  const view = useDbView((s) => s.view);
  const from = useDbView((s) => s.from);
  const version = useDbView((s) => s.version);
  const tabs = useApp((s) => s.tabs);
  const [tables, setTables] = useState(() => schemaLoaded() ?? []);
  const [, redraw] = useState(0);
  const last = useRef<TableData | null>(null);

  useEffect(() => {
    // load every table up front (they are small), so switching between them is instant
    sqlSchema().then((t) => {
      setTables(t);
      t.forEach((x) => sqlTable(x.name));
      // the table shown was dropped by a query: show the first one instead
      if (t.length && !t.some((x) => x.name === useDbView.getState().table)) useDbView.setState({ table: t[0].name });
    });
  }, [version]);
  useEffect(() => {
    if (!tableLoaded(table)) sqlTable(table).then(() => redraw((n) => n + 1));
  }, [table, version]);

  // until a table has loaded, keep showing the previous one rather than an empty page
  const data = tableLoaded(table) ?? last.current;
  last.current = data;
  const back = from && tabs.some((t) => t.path === from) ? from : null;

  const reset = async () => {
    const ok = await useApp.getState().ask({
      title: "Reset the sample database?",
      message: "Every change your queries made (added, changed or deleted rows and tables) is undone, and the original data comes back.",
      buttons: [{ label: "Reset", value: "reset", danger: true }, { label: "Cancel", value: "cancel" }],
    });
    if (ok !== "reset") return;
    try { await resetDb(); useApp.getState().notify("The sample database has its original data again."); }
    catch (e) { useApp.getState().notify(String(e), "error"); }
  };

  const query = () => {
    const text = `SELECT * FROM ${table};`;
    if (back) {
      useApp.getState().activate(back);
      // the editor shows the file a frame later
      requestAnimationFrame(() => requestAnimationFrame(() => insertSql(text, true)));
    } else {
      navigator.clipboard.writeText(text).then(() => useApp.getState().notify(`Copied: ${text}`), () => {});
    }
  };

  return (
    <div className="dbv">
      <nav className="dbv-list" aria-label="Tables">
        <div className="dbv-eyebrow">Tables</div>
        {tables.map((t) => (
          <button key={t.name} className={"dbv-item" + (t.name === table ? " on" : "")} onClick={() => useDbView.setState({ table: t.name })}>
            <span className="dbv-item-name">{t.name}</span>
            <span className="dbv-count">{t.rows}</span>
          </button>
        ))}
        <div className="dbv-foot">
          <p className="dbv-note">Your queries change this database and the changes stay. Reset brings back the original data.</p>
          <button className="btn" onClick={reset}>Reset database</button>
        </div>
      </nav>

      <section className="dbv-main">
        <header className="dbv-head">
          <div className="dbv-title">
            <h1>{table}</h1>
            {data && <span className="dbv-meta">{data.rows.length} rows · {data.columns.length} columns</span>}
          </div>
          <div className="seg" role="tablist">
            <button role="tab" aria-selected={view === "data"} className={view === "data" ? "on" : ""} onClick={() => useDbView.setState({ view: "data" })}>Data</button>
            <button role="tab" aria-selected={view === "structure"} className={view === "structure" ? "on" : ""} onClick={() => useDbView.setState({ view: "structure" })}>Structure</button>
          </div>
          <button className="btn" title={`SELECT * FROM ${table};`} onClick={query}>{back ? `Query in ${baseName(back)}` : "Copy SELECT"}</button>
        </header>

        <div className="dbv-scroll" key={table + view}>
          {!data ? null : view === "data" ? <DataGrid data={data} /> : <Structure data={data} />}
        </div>
      </section>
    </div>
  );
}

function Key({ c }: { c: SqlColumn }) {
  return (
    <>
      {c.primaryKey && <span className="key" title="Primary key">PK</span>}
      {c.references && <span className="key" title={`Foreign key: references ${c.references}`}>FK</span>}
      {c.unique && !c.primaryKey && <span className="key" title="Unique: no two rows have the same value">UQ</span>}
    </>
  );
}

function DataGrid({ data }: { data: TableData }) {
  const num = data.columns.map(isNum);
  return (
    <table className="dbv-grid">
      <thead>
        <tr>
          {data.columns.map((c, i) => (
            <th key={c.name} className={num[i] ? "num" : undefined}>
              <div className="th-name">{c.name}<Key c={c} /></div>
              <div className="th-type">{c.ty || "any"}</div>
            </th>
          ))}
        </tr>
      </thead>
      <tbody>
        {data.rows.map((r, i) => (
          <tr key={i}>
            {r.map((v, j) => (
              <td key={j} className={num[j] ? "num" : undefined}>{v === null ? <span className="null">NULL</span> : String(v)}</td>
            ))}
          </tr>
        ))}
      </tbody>
    </table>
  );
}

function Structure({ data }: { data: TableData }) {
  const [html, setHtml] = useState<string | null>(null);
  useEffect(() => {
    let live = true;
    ws.monacoReady.then((m) => m.monaco.editor.colorize(data.sql + ";", "sql", { tabSize: 2 })).then((h) => { if (live) setHtml(h); }, () => {});
    return () => { live = false; };
  }, [data.sql]);
  return (
    <div className="dbv-structure">
      <table className="dbv-grid">
        <thead>
          <tr><th>Column</th><th>Type</th><th>Key</th><th>Not null</th><th>Default</th><th>References</th></tr>
        </thead>
        <tbody>
          {data.columns.map((c) => (
            <tr key={c.name}>
              <td className="strong">{c.name}</td>
              <td>{c.ty || "any"}</td>
              <td><Key c={c} /></td>
              <td>{c.notNull || c.primaryKey ? "yes" : ""}</td>
              <td>{c.default ?? ""}</td>
              <td>{c.references ?? ""}</td>
            </tr>
          ))}
        </tbody>
      </table>
      <div className="dbv-eyebrow dbv-sql-label">CREATE statement</div>
      {html ? <pre className="dbv-sql" dangerouslySetInnerHTML={{ __html: html }} /> : <pre className="dbv-sql">{data.sql};</pre>}
    </div>
  );
}
