//! Language servers (clangd, pylsp, jdtls) over stdio. The editor's LSP client talks to them
//! through `lsp_send` and the "lsp-message" event; this side only frames and forwards messages.

use serde::Serialize;
use std::collections::HashMap;
use std::io::{BufRead, BufReader, Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Stdio};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Mutex;
use tauri::{AppHandle, Emitter, Manager};

use crate::runner::quiet_command;
use crate::tools;

struct Server {
    child: Child,
    stdin: ChildStdin,
}

static SERVERS: Mutex<Option<HashMap<u32, Server>>> = Mutex::new(None);
static NEXT: AtomicU32 = AtomicU32::new(1);

#[derive(Serialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct Started {
    id: u32,
    /// For clangd: the g++/gcc it should treat as the compiler (system headers come from it).
    compiler: Option<String>,
}

#[derive(Serialize, Clone)]
struct Msg {
    id: u32,
    msg: String,
}

static PYLSP: Mutex<Option<bool>> = Mutex::new(None);

/// Check again next time (after installing something).
pub fn forget_checks() { *PYLSP.lock().unwrap() = None; }

pub fn has_pylsp(python: &Path) -> bool {
    let mut c = PYLSP.lock().unwrap();
    *c.get_or_insert_with(|| quiet_command(python).args(["-c", "import pylsp"]).stdout(Stdio::null()).stderr(Stdio::null()).status().map(|s| s.success()).unwrap_or(false))
}

/// jdtls folder (with plugins/org.eclipse.equinox.launcher_*.jar): NppDev's tools folder first.
pub fn find_jdtls(app: &AppHandle) -> Option<(PathBuf, PathBuf)> {
    let mut dirs = vec![];
    if let Ok(d) = app.path().app_local_data_dir() { dirs.push(d.join("tools").join("jdtls")); }
    if let Some(l) = std::env::var_os("LOCALAPPDATA") { dirs.push(PathBuf::from(&l).join("NppDev").join("jdtls")); }
    for d in dirs {
        let Ok(rd) = std::fs::read_dir(d.join("plugins")) else { continue };
        let mut jars: Vec<PathBuf> = rd.filter_map(Result::ok).map(|e| e.path()).filter(|p| p.file_name().is_some_and(|n| { let n = n.to_string_lossy(); n.starts_with("org.eclipse.equinox.launcher_") && n.ends_with(".jar") })).collect();
        jars.sort();
        if let Some(j) = jars.pop() { return Some((d, j)); }
    }
    None
}

/// The java to run jdtls with: it needs Java 21 or newer.
pub fn java_for_jdtls() -> Option<PathBuf> {
    let java = tools::find("java")?;
    let v = tools::version(&java, "-version").unwrap_or_default();
    let major: u32 = v.split('"').nth(1).and_then(|s| s.split('.').next()).and_then(|s| s.parse().ok()).unwrap_or(0);
    (major >= 21).then_some(java)
}

#[tauri::command]
pub async fn lsp_start(app: AppHandle, lang: String) -> Result<Started, String> {
    tauri::async_runtime::spawn_blocking(move || start(app, &lang)).await.map_err(|e| e.to_string())?
}

fn start(app: AppHandle, lang: &str) -> Result<Started, String> {
    let mut compiler = None;
    let mut cmd = match lang {
        "c" | "cpp" => {
            let clangd = tools::find("clangd").ok_or("clangd")?;
            let mut c = quiet_command(clangd);
            c.args(["--offset-encoding=utf-16", "--header-insertion=never", "--pch-storage=memory", "--background-index-priority=low", "--log=error"]);
            if let Some(gpp) = tools::find(if lang == "c" { "gcc" } else { "gpp" }).or_else(|| tools::find("gpp")) {
                let glob = gpp.parent().unwrap().join("*").to_string_lossy().replace('\\', "/");
                c.arg(format!("--query-driver={glob}"));
                compiler = Some(gpp.to_string_lossy().into_owned());
            }
            c
        }
        "python" => {
            let py = tools::find("python").ok_or("python")?;
            if !has_pylsp(&py) { return Err("pylsp".into()); }
            let mut c = quiet_command(py);
            c.args(["-m", "pylsp"]);
            c
        }
        "java" => {
            let java = java_for_jdtls().ok_or("java21")?;
            let (dir, launcher) = find_jdtls(&app).ok_or("jdtls")?;
            let root = app.path().app_local_data_dir().map_err(|e| e.to_string())?;
            let _ = std::fs::create_dir_all(&root);
            let config = dir.join(if cfg!(windows) { "config_win" } else { "config_linux" });
            let mut c = quiet_command(java);
            c.args(["-Declipse.application=org.eclipse.jdt.ls.core.id1", "-Dosgi.bundles.defaultStartLevel=4", "-Declipse.product=org.eclipse.jdt.ls.core.product",
                "-XX:+UseParallelGC", "-XX:GCTimeRatio=4", "-XX:AdaptiveSizePolicyWeight=90", "-Dsun.zip.disableMemoryMapping=true", "-Xms100m", "-Xmx1G",
                "-XX:+AutoCreateSharedArchive", "-Xlog:disable", "--add-modules=ALL-SYSTEM", "--add-opens", "java.base/java.util=ALL-UNNAMED", "--add-opens", "java.base/java.lang=ALL-UNNAMED"]);
            c.arg(format!("-XX:SharedArchiveFile={}", root.join("jdtls.jsa").display()));
            c.arg("-jar").arg(launcher).arg("-configuration").arg(config).arg("-data").arg(root.join("jdtls-workspace"));
            c
        }
        _ => return Err("none".into()),
    };
    let mut child = cmd.stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null()).spawn().map_err(|e| e.to_string())?;
    let stdin = child.stdin.take().unwrap();
    let stdout = child.stdout.take().unwrap();
    let id = NEXT.fetch_add(1, Ordering::Relaxed);
    SERVERS.lock().unwrap().get_or_insert_with(HashMap::new).insert(id, Server { child, stdin });

    std::thread::spawn(move || {
        let mut r = BufReader::new(stdout);
        loop {
            let mut len = 0usize;
            let mut line = String::new();
            loop {
                line.clear();
                if r.read_line(&mut line).unwrap_or(0) == 0 { let _ = app.emit("lsp-exit", id); forget(id); return; }
                let l = line.trim_end();
                if l.is_empty() { break; }
                if let Some(v) = l.strip_prefix("Content-Length:") { len = v.trim().parse().unwrap_or(0); }
            }
            let mut buf = vec![0u8; len];
            if r.read_exact(&mut buf).is_err() { let _ = app.emit("lsp-exit", id); forget(id); return; }
            let _ = app.emit("lsp-message", Msg { id, msg: String::from_utf8_lossy(&buf).into_owned() });
        }
    });
    Ok(Started { id, compiler })
}

fn forget(id: u32) {
    if let Some(m) = SERVERS.lock().unwrap().as_mut() { m.remove(&id); }
}

#[tauri::command]
pub fn lsp_send(id: u32, msg: String) -> Result<(), String> {
    let mut g = SERVERS.lock().unwrap();
    let s = g.as_mut().and_then(|m| m.get_mut(&id)).ok_or("The language server has stopped.")?;
    let framed = format!("Content-Length: {}\r\n\r\n{}", msg.len(), msg);
    s.stdin.write_all(framed.as_bytes()).and_then(|_| s.stdin.flush()).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn lsp_stop(id: u32) {
    if let Some(mut s) = SERVERS.lock().unwrap().as_mut().and_then(|m| m.remove(&id)) {
        let _ = s.child.kill();
        let _ = s.child.wait();
    }
}

/// Stop every server (when NppDev closes).
pub fn stop_all() {
    if let Some(m) = SERVERS.lock().unwrap().as_mut() {
        for (_, mut s) in m.drain() { let _ = s.child.kill(); }
    }
}
