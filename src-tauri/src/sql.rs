//! .sql files run on SQLite (compiled into NppDev, nothing to install), against a fresh copy of a
//! sample database every time, so changes made by one run never leak into the next. Every query
//! that returns rows is printed as a table, like a database console.

use fallible_iterator::FallibleIterator;
use rusqlite::{types::ValueRef, Batch, Connection};
use serde::Serialize;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

const SAMPLE: &str = include_str!("sql/sample.sql");
const MAX_ROWS: usize = 1000;

pub struct Outcome {
    pub output: String,
    /// A statement failed: "Statement 2: no such table: emp".
    pub error: Option<String>,
    pub timed_out: bool,
}

fn sample() -> rusqlite::Result<Connection> {
    let conn = Connection::open_in_memory()?;
    conn.execute_batch(SAMPLE)?;
    Ok(conn)
}

fn cell(v: ValueRef<'_>) -> (String, bool) {
    match v {
        ValueRef::Null => ("NULL".into(), false),
        ValueRef::Integer(i) => (i.to_string(), true),
        ValueRef::Real(f) => (f.to_string(), true),
        ValueRef::Text(t) => (String::from_utf8_lossy(t).into_owned(), false),
        ValueRef::Blob(b) => (format!("<{} bytes>", b.len()), false),
    }
}

/// A result set as a console table; numbers right-aligned.
fn table(cols: &[String], rows: &[Vec<(String, bool)>], more: bool) -> String {
    let width = |i: usize| rows.iter().map(|r| r[i].0.chars().count()).chain([cols[i].chars().count()]).max().unwrap_or(0);
    let w: Vec<usize> = (0..cols.len()).map(width).collect();
    let rule = format!("+{}+", w.iter().map(|n| "-".repeat(n + 2)).collect::<Vec<_>>().join("+"));
    let mut out = vec![rule.clone()];
    out.push(format!("| {} |", cols.iter().enumerate().map(|(i, c)| format!("{c:<w$}", w = w[i])).collect::<Vec<_>>().join(" | ")));
    out.push(rule.clone());
    for r in rows {
        out.push(format!("| {} |", r.iter().enumerate().map(|(i, (v, num))| if *num { format!("{v:>w$}", w = w[i]) } else { format!("{v:<w$}", w = w[i]) }).collect::<Vec<_>>().join(" | ")));
    }
    if !rows.is_empty() { out.push(rule); }
    out.push(match (rows.len(), more) {
        (_, true) => format!("first {MAX_ROWS} rows shown"),
        (1, _) => "1 row".into(),
        (n, _) => format!("{n} rows"),
    });
    out.join("\n")
}

/// Run every statement of `src` on a fresh sample database.
pub fn run(src: &str, limit: Duration, cancel: &'static AtomicBool) -> Outcome {
    let mut out = Outcome { output: String::new(), error: None, timed_out: false };
    let mut conn = match sample() {
        Ok(c) => c,
        Err(e) => { out.error = Some(format!("The sample database couldn't be created: {e}")); return out; }
    };
    let start = Instant::now();
    // stop a runaway query (an endless recursive CTE, a huge cross join) at the time limit
    let _ = conn.progress_handler(10_000, Some(move || start.elapsed() > limit || cancel.load(Ordering::Relaxed)));
    let mut parts: Vec<String> = vec![];
    let mut batch = Batch::new(&conn, src);
    let mut n = 0;
    loop {
        let mut stmt = match batch.next() {
            Ok(Some(s)) => s,
            Ok(None) => break,
            Err(e) => { out.error = Some(explain(n + 1, &e)); break; }
        };
        n += 1;
        let cols: Vec<String> = stmt.column_names().into_iter().map(String::from).collect();
        if cols.is_empty() {
            if let Err(e) = stmt.raw_execute() { out.timed_out = interrupted(&e); out.error = Some(explain(n, &e)); break; }
            continue;
        }
        let mut rows: Vec<Vec<(String, bool)>> = vec![];
        let mut more = false;
        let mut q = stmt.raw_query();
        loop {
            match q.next() {
                Ok(Some(r)) => {
                    if rows.len() == MAX_ROWS { more = true; break; }
                    rows.push((0..cols.len()).map(|i| r.get_ref(i).map(cell).unwrap_or(("?".into(), false))).collect());
                }
                Ok(None) => break,
                Err(e) => { out.timed_out = interrupted(&e); out.error = Some(explain(n, &e)); break; }
            }
        }
        if out.error.is_some() { break; }
        parts.push(table(&cols, &rows, more));
    }
    out.output = parts.join("\n\n");
    if !out.output.is_empty() { out.output.push('\n'); }
    out
}

fn interrupted(e: &rusqlite::Error) -> bool {
    matches!(e, rusqlite::Error::SqliteFailure(f, _) if f.code == rusqlite::ErrorCode::OperationInterrupted)
}

fn explain(n: usize, e: &rusqlite::Error) -> String {
    let msg = match e {
        rusqlite::Error::SqliteFailure(_, Some(m)) => m.clone(),
        other => other.to_string(),
    };
    if interrupted(e) { "Stopped: the query ran past the time limit.".into() } else { format!("Statement {n}: {msg}") }
}

#[derive(Serialize)]
pub struct Table {
    name: String,
    columns: Vec<String>,
    rows: i64,
}

/// The sample database's tables and columns (the Run panel lists them; the editor suggests them).
#[tauri::command]
pub fn sql_schema() -> Vec<Table> {
    let Ok(conn) = sample() else { return vec![] };
    let names: Vec<String> = conn
        .prepare("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY rowid")
        .and_then(|mut s| s.query_map([], |r| r.get(0))?.collect())
        .unwrap_or_default();
    names.into_iter().map(|name| {
        let columns = conn.prepare(&format!("PRAGMA table_info(\"{name}\")"))
            .and_then(|mut s| s.query_map([], |r| r.get::<_, String>(1))?.collect())
            .unwrap_or_default();
        let rows = conn.query_row(&format!("SELECT COUNT(*) FROM \"{name}\""), [], |r| r.get(0)).unwrap_or(0);
        Table { name, columns, rows }
    }).collect()
}

#[cfg(test)]
mod t {
    use super::*;
    #[test]
    fn queries() {
        static STOP: AtomicBool = AtomicBool::new(false);
        let stop = &STOP;
        let o = run("SELECT d.name, COUNT(*) AS staff, MAX(e.salary) AS top_salary FROM employees e JOIN departments d USING (dept_id) GROUP BY d.name ORDER BY staff DESC, d.name LIMIT 2;", Duration::from_secs(2), stop);
        println!("{}", o.output);
        assert!(o.error.is_none());
        assert!(o.output.contains("| Engineering |     5 |     250000 |"));
        assert!(o.output.contains("2 rows"));
        // changes stay in that run only
        let o = run("DELETE FROM employees; SELECT COUNT(*) AS n FROM employees;", Duration::from_secs(2), stop);
        assert!(o.output.contains("| 0 |"));
        let o = run("SELECT COUNT(*) AS n FROM employees;", Duration::from_secs(2), stop);
        assert!(o.output.contains("| 15 |"));
        // errors name the statement
        let o = run("SELECT 1;\nSELECT * FROM emp;", Duration::from_secs(2), stop);
        assert_eq!(o.error.as_deref(), Some("Statement 2: no such table: emp"));
        // a runaway query stops at the limit
        let o = run("WITH RECURSIVE r(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM r) SELECT COUNT(*) FROM r;", Duration::from_millis(300), stop);
        assert!(o.timed_out, "{:?}", o.error);
        assert_eq!(sql_schema().len(), 9);
    }
}
