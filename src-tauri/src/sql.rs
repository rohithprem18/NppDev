//! .sql files run on SQLite (compiled into NppDev, nothing to install), on NppDev's own sample
//! database: one file, sample.db in the app's data folder, made from sample.sql the first time.
//! Changes a query makes stay for the next run, like a real database; Reset puts the original data
//! back. Every query that returns rows is printed as a table, like a database console.

use fallible_iterator::FallibleIterator;
use rusqlite::{types::ValueRef, Batch, Connection};
use serde::Serialize;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Mutex, OnceLock};
use std::time::{Duration, Instant};

const SAMPLE: &str = include_str!("sql/sample.sql");
const MAX_ROWS: usize = 1000;

pub struct Outcome {
    pub output: String,
    /// A statement failed: "Statement 2: no such table: emp".
    pub error: Option<String>,
    pub timed_out: bool,
}

static DB_PATH: OnceLock<PathBuf> = OnceLock::new();
/** Reset deletes the file: no run may have it open meanwhile. */
static DB_LOCK: Mutex<()> = Mutex::new(());

/// Where the database lives: called once at startup with the app's data folder.
pub fn init(dir: PathBuf) {
    let _ = DB_PATH.set(dir.join("sample.db"));
}

fn db_path() -> PathBuf {
    DB_PATH.get().cloned().unwrap_or_else(|| std::env::temp_dir().join("nppdev-sample.db"))
}

/// Open the sample database, filling it from sample.sql the first time (user_version marks it done,
/// so a database whose tables you dropped stays the way you left it).
fn sample() -> rusqlite::Result<Connection> {
    let path = db_path();
    if let Some(dir) = path.parent() { let _ = std::fs::create_dir_all(dir); }
    let conn = Connection::open(&path)?;
    conn.busy_timeout(Duration::from_secs(5))?;
    let seeded: i64 = conn.query_row("PRAGMA user_version", [], |r| r.get(0))?;
    if seeded == 0 {
        conn.execute_batch(&format!("BEGIN;\n{SAMPLE}\nPRAGMA user_version = 1;\nCOMMIT;"))?;
    }
    Ok(conn)
}

/// Put the original data back: delete the file and fill a new one.
pub fn reset() -> Result<(), String> {
    let _guard = DB_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let path = db_path();
    for ext in ["", "-journal", "-wal", "-shm"] {
        let p = PathBuf::from(format!("{}{ext}", path.display()));
        if p.exists() { std::fs::remove_file(&p).map_err(|e| format!("Couldn't reset the sample database: {e}"))?; }
    }
    sample().map(|_| ()).map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn sql_reset() -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(reset).await.map_err(|e| e.to_string())?
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

/// Run every statement of `src` on the sample database; what it changes stays changed.
pub fn run(src: &str, limit: Duration, cancel: &'static AtomicBool) -> Outcome {
    let mut out = Outcome { output: String::new(), error: None, timed_out: false };
    let _guard = DB_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let mut conn = match sample() {
        Ok(c) => c,
        Err(e) => { out.error = Some(format!("The sample database couldn't be opened: {e}. Reset it from the sample.db tab.")); return out; }
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

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Column {
    name: String,
    /// The declared type: INTEGER, TEXT, REAL, DATE, ...
    ty: String,
    not_null: bool,
    primary_key: bool,
    unique: bool,
    default: Option<String>,
    /// "departments.dept_id" for a foreign key.
    references: Option<String>,
}

#[derive(Serialize)]
pub struct TableData {
    name: String,
    columns: Vec<Column>,
    /// Every row; each value is null, a number or a string.
    rows: Vec<Vec<serde_json::Value>>,
    /// The CREATE TABLE statement.
    sql: String,
}

/// One table of the sample database in full: its columns with their types and keys, and all its rows.
#[tauri::command]
pub fn sql_table(name: String) -> Result<TableData, String> {
    let conn = sample().map_err(|e| e.to_string())?;
    let sql: String = conn
        .query_row("SELECT sql FROM sqlite_master WHERE type = 'table' AND name = ?1", [&name], |r| r.get(0))
        .map_err(|_| format!("The sample database has no table named {name}."))?;
    let q = name.replace('"', "\"\"");
    let mut fks = std::collections::HashMap::new();
    if let Ok(mut s) = conn.prepare(&format!("PRAGMA foreign_key_list(\"{q}\")")) {
        let _ = s.query_map([], |r| Ok((r.get::<_, String>(3)?, format!("{}.{}", r.get::<_, String>(2)?, r.get::<_, String>(4)?))))
            .map(|it| it.flatten().for_each(|(from, to)| { fks.insert(from, to); }));
    }
    // single-column UNIQUE constraints and indexes
    let mut unique = std::collections::HashSet::new();
    let idx: Vec<(String, bool)> = conn.prepare(&format!("PRAGMA index_list(\"{q}\")"))
        .and_then(|mut s| s.query_map([], |r| Ok((r.get::<_, String>(1)?, r.get::<_, i64>(2)? != 0)))?.collect())
        .unwrap_or_default();
    for (index, is_unique) in idx {
        if !is_unique { continue; }
        let cols: Vec<String> = conn.prepare(&format!("PRAGMA index_info(\"{}\")", index.replace('"', "\"\"")))
            .and_then(|mut s| s.query_map([], |r| r.get(2))?.collect())
            .unwrap_or_default();
        if let [c] = cols.as_slice() { unique.insert(c.clone()); }
    }
    let columns = conn.prepare(&format!("PRAGMA table_info(\"{q}\")"))
        .and_then(|mut s| s.query_map([], |r| {
            let name: String = r.get(1)?;
            Ok(Column { references: fks.get(&name).cloned(), unique: unique.contains(&name), ty: r.get(2)?, not_null: r.get::<_, i64>(3)? != 0, default: r.get(4)?, primary_key: r.get::<_, i64>(5)? != 0, name })
        })?.collect::<rusqlite::Result<Vec<_>>>())
        .map_err(|e| e.to_string())?;
    let mut stmt = conn.prepare(&format!("SELECT * FROM \"{q}\"")).map_err(|e| e.to_string())?;
    let n = columns.len();
    let rows = stmt.query_map([], |r| {
        Ok((0..n).map(|i| match r.get_ref(i) {
            Ok(ValueRef::Integer(v)) => v.into(),
            Ok(ValueRef::Real(v)) => v.into(),
            Ok(ValueRef::Text(t)) => String::from_utf8_lossy(t).into_owned().into(),
            Ok(ValueRef::Blob(b)) => format!("<{} bytes>", b.len()).into(),
            _ => serde_json::Value::Null,
        }).collect::<Vec<_>>())
    }).and_then(|it| it.collect::<rusqlite::Result<Vec<_>>>()).map_err(|e| e.to_string())?;
    Ok(TableData { name, columns, rows, sql })
}

#[cfg(test)]
mod t {
    use super::*;
    #[test]
    fn queries() {
        static STOP: AtomicBool = AtomicBool::new(false);
        let stop = &STOP;
        init(std::env::temp_dir().join(format!("nppdev-sql-test-{}", std::process::id())));
        reset().unwrap();
        let o = run("SELECT d.name, COUNT(*) AS staff, MAX(e.salary) AS top_salary FROM employees e JOIN departments d USING (dept_id) GROUP BY d.name ORDER BY staff DESC, d.name LIMIT 2;", Duration::from_secs(2), stop);
        println!("{}", o.output);
        assert!(o.error.is_none());
        assert!(o.output.contains("| Engineering |     5 |     250000 |"));
        assert!(o.output.contains("2 rows"));
        // changes stay for the next run; reset brings the original data back
        let o = run("DELETE FROM employees WHERE emp_id > 10; SELECT COUNT(*) AS n FROM employees;", Duration::from_secs(2), stop);
        assert!(o.output.contains("| 10 |"), "{}", o.output);
        let o = run("SELECT COUNT(*) AS n FROM employees;", Duration::from_secs(2), stop);
        assert!(o.output.contains("| 10 |"));
        let o = run("CREATE TABLE notes (id INTEGER PRIMARY KEY, body TEXT); INSERT INTO notes (body) VALUES ('hi');", Duration::from_secs(2), stop);
        assert!(o.error.is_none());
        assert_eq!(sql_schema().len(), 10);
        reset().unwrap();
        let o = run("SELECT COUNT(*) AS n FROM employees;", Duration::from_secs(2), stop);
        assert!(o.output.contains("| 15 |"));
        // errors name the statement
        let o = run("SELECT 1;\nSELECT * FROM emp;", Duration::from_secs(2), stop);
        assert_eq!(o.error.as_deref(), Some("Statement 2: no such table: emp"));
        // a runaway query stops at the limit
        let o = run("WITH RECURSIVE r(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM r) SELECT COUNT(*) FROM r;", Duration::from_millis(300), stop);
        assert!(o.timed_out, "{:?}", o.error);
        assert_eq!(sql_schema().len(), 9);
        let t = sql_table("employees".into()).unwrap();
        assert_eq!(t.rows.len(), 15);
        assert!(t.columns.iter().any(|c| c.name == "dept_id" && c.references.as_deref() == Some("departments.dept_id")));
        assert!(t.columns[0].primary_key);
        assert!(t.columns.iter().any(|c| c.name == "email" && c.unique));
        assert!(sql_table("nope\"; DROP TABLE x; --".into()).is_err());
    }
}
