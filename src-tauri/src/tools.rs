//! Finds compilers and runtimes on this PC: PATH first, then the usual install folders.
//! Results are cached for the session; `forget()` clears them after installing something.

use serde::Serialize;
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

#[derive(Serialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct Tool {
    pub key: &'static str,
    pub name: &'static str,
    pub path: Option<String>,
    pub version: Option<String>,
}

static CACHE: Mutex<Option<HashMap<&'static str, Option<PathBuf>>>> = Mutex::new(None);

#[cfg(windows)]
const EXE: &str = ".exe";
#[cfg(not(windows))]
const EXE: &str = "";

fn env(name: &str) -> Option<PathBuf> {
    std::env::var_os(name).filter(|v| !v.is_empty()).map(PathBuf::from)
}

/// `name` (without .exe) in each PATH folder; Windows Store stubs skipped.
/// Links are NOT followed: ~/.cargo/bin/rustc is a link to rustup, which only works under its own name.
fn on_path(name: &str) -> Vec<PathBuf> {
    let file = format!("{name}{EXE}");
    let Some(path) = std::env::var_os("PATH") else { return vec![] };
    std::env::split_paths(&path)
        .filter(|d| !d.to_string_lossy().to_lowercase().contains("windowsapps"))
        .map(|d| d.join(&file))
        .filter(|p| p.is_file())
        .collect()
}

/// Subfolders of `dir` matching a prefix, newest name last (e.g. jdk-17, jdk-21).
#[cfg_attr(not(windows), allow(dead_code))]
fn subdirs(dir: &Path, prefix: &str) -> Vec<PathBuf> {
    let mut v: Vec<PathBuf> = std::fs::read_dir(dir)
        .map(|r| r.filter_map(Result::ok).map(|e| e.path()).filter(|p| p.is_dir() && p.file_name().is_some_and(|n| n.to_string_lossy().to_lowercase().starts_with(&prefix.to_lowercase()))).collect())
        .unwrap_or_default();
    v.sort();
    v
}

/// Extra folders where a tool is usually installed (newest first) when it isn't on PATH.
fn known_dirs(key: &str) -> Vec<PathBuf> {
    let mut v = Vec::new();
    #[cfg(windows)]
    {
        let pf: Vec<PathBuf> = ["ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"].iter().filter_map(|n| env(n)).collect();
        let local = env("LOCALAPPDATA").unwrap_or_default();
        match key {
            "gpp" | "gcc" => {
                for r in ["C:\\msys64", "D:\\msys64"] {
                    for e in ["ucrt64", "mingw64", "clang64"] { v.push(PathBuf::from(r).join(e).join("bin")); }
                }
                v.extend(["C:\\MinGW\\bin", "C:\\mingw64\\bin", "C:\\TDM-GCC-64\\bin", "C:\\Strawberry\\c\\bin"].map(PathBuf::from));
                for d in subdirs(&local.join("Microsoft\\WinGet\\Packages"), "BrechtSanders.WinLibs").into_iter().rev() { v.push(d.join("mingw64\\bin")); }
            }
            "javac" | "java" => {
                if let Some(h) = env("JAVA_HOME") { v.push(h.join("bin")); }
                for p in &pf {
                    for vendor in ["Eclipse Adoptium", "Java", "Microsoft", "Zulu", "Amazon Corretto", "BellSoft"] {
                        for d in subdirs(&p.join(vendor), "").into_iter().rev() { v.push(d.join("bin")); }
                    }
                }
            }
            "python" => {
                for d in subdirs(&local.join("Programs\\Python"), "Python3").into_iter().rev() { v.push(d); }
                for d in subdirs(Path::new("C:\\"), "Python3").into_iter().rev() { v.push(d); }
                for p in &pf { for d in subdirs(p, "Python3").into_iter().rev() { v.push(d); } }
            }
            "node" => { for p in &pf { v.push(p.join("nodejs")); } v.push(PathBuf::from("C:\\nvm4w\\nodejs")); }
            "go" => { for p in &pf { v.push(p.join("Go\\bin")); } }
            "rustc" | "cargo" => {
                if let Some(h) = env("USERPROFILE") { v.push(h.join(".cargo\\bin")); }
                // the standalone GNU build (winget Rustlang.Rust.GNU): "Rust stable GNU 1.xx"
                for p in &pf { for d in subdirs(p, "Rust ").into_iter().rev() { v.push(d.join("bin")); } }
            }
            "kotlinc" => {
                for p in &pf { v.push(p.join("kotlinc\\bin")); }
                for d in subdirs(&local.join("Microsoft\\WinGet\\Packages"), "JetBrains.Kotlin").into_iter().rev() { v.push(d.join("kotlinc\\bin")); }
            }
            // LLVM's installer doesn't put itself on PATH
            "clangd" => { for p in &pf { v.push(p.join("LLVM\\bin")); } }
            "csc" => { v.push(PathBuf::from("C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319")); }
            _ => {}
        }
        // command links winget makes for portable packages
        v.push(local.join("Microsoft\\WinGet\\Links"));
        // NppDev's own tools folder (Settings > Languages installs Kotlin there)
        if key == "kotlinc" { v.push(local.join("dev.rohithprem.nppdev\\tools\\kotlinc\\bin")); }
    }
    #[cfg(not(windows))]
    {
        let home = env("HOME").unwrap_or_default();
        match key {
            "rustc" | "cargo" => v.push(home.join(".cargo/bin")),
            "go" => v.push(PathBuf::from("/usr/local/go/bin")),
            "kotlinc" => {
                v.push(home.join(".local/share/dev.rohithprem.nppdev/tools/kotlinc/bin"));
                v.push(home.join(".sdkman/candidates/kotlin/current/bin"));
            }
            _ => {}
        }
        v.extend(["/usr/bin", "/usr/local/bin", "/snap/bin"].map(PathBuf::from));
    }
    v
}

/// The file names a tool goes by.
fn exe_names(key: &str) -> &'static [&'static str] {
    match key {
        "gpp" => &["g++"],
        "gcc" => &["gcc"],
        "javac" => &["javac"],
        "java" => &["java"],
        #[cfg(windows)]
        "python" => &["python"],
        #[cfg(not(windows))]
        "python" => &["python3", "python"],
        "node" => &["node"],
        "go" => &["go"],
        "rustc" => &["rustc"],
        "kotlinc" => &["kotlinc.bat", "kotlinc"],
        #[cfg(windows)]
        "csc" => &["csc"],
        #[cfg(not(windows))]
        "csc" => &["mcs", "csc"],
        "clangd" => &["clangd"],
        _ => &[],
    }
}

fn locate(key: &'static str) -> Option<PathBuf> {
    for name in exe_names(key) {
        let (stem, ext) = match name.strip_suffix(".bat") { Some(s) => (s, ".bat"), None => (*name, EXE) };
        if ext == EXE { if let Some(p) = on_path(stem).into_iter().next() { return Some(p); } }
        for d in known_dirs(key) {
            let p = d.join(format!("{stem}{ext}"));
            if p.is_file() { return Some(p); }
        }
    }
    // javac and java come from the same JDK
    if key == "java" { return find("javac").map(|j| j.with_file_name(format!("java{EXE}"))).filter(|p| p.is_file()); }
    None
}

/// Full path of a tool, or None.
pub fn find(key: &'static str) -> Option<PathBuf> {
    let mut cache = CACHE.lock().unwrap();
    let map = cache.get_or_insert_with(HashMap::new);
    if let Some(v) = map.get(key) { return v.clone(); }
    drop(cache);
    let found = locate(key);
    CACHE.lock().unwrap().get_or_insert_with(HashMap::new).insert(key, found.clone());
    found
}

/// Forget what was found (after installing a tool).
pub fn forget() {
    *CACHE.lock().unwrap() = None;
    refresh_path();
}

/// A running program keeps the PATH it started with, so something installed since (winget, an
/// MSI) wouldn't be found until NppDev restarts. Read PATH again from the registry (machine, then
/// user, as Windows builds it) and keep anything else this process had.
#[cfg(windows)]
fn refresh_path() {
    use windows_sys::Win32::System::Registry::{RegGetValueW, HKEY, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, RRF_RT_REG_EXPAND_SZ, RRF_RT_REG_SZ};
    fn read(root: HKEY, key: &str) -> Option<String> {
        let wide = |s: &str| s.encode_utf16().chain(Some(0)).collect::<Vec<u16>>();
        let (k, name) = (wide(key), wide("Path"));
        let mut size: u32 = 0;
        unsafe {
            // RRF_RT_REG_EXPAND_SZ without RRF_NOEXPAND: %VARS% come back expanded
            if RegGetValueW(root, k.as_ptr(), name.as_ptr(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, std::ptr::null_mut(), std::ptr::null_mut(), &mut size) != 0 { return None; }
            let mut buf = vec![0u16; size as usize / 2 + 1];
            if RegGetValueW(root, k.as_ptr(), name.as_ptr(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, std::ptr::null_mut(), buf.as_mut_ptr().cast(), &mut size) != 0 { return None; }
            Some(String::from_utf16_lossy(&buf[..buf.iter().position(|&c| c == 0).unwrap_or(buf.len())]))
        }
    }
    let machine = read(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment").unwrap_or_default();
    let user = read(HKEY_CURRENT_USER, "Environment").unwrap_or_default();
    let mut dirs: Vec<PathBuf> = std::env::split_paths(&format!("{machine};{user}")).filter(|p| !p.as_os_str().is_empty()).collect();
    if let Some(old) = std::env::var_os("PATH") {
        for p in std::env::split_paths(&old) { if !dirs.contains(&p) { dirs.push(p); } }
    }
    if let Ok(joined) = std::env::join_paths(dirs) {
        // NppDev only reads PATH on its own threads at these moments; nothing else is changing it
        unsafe { std::env::set_var("PATH", joined); }
    }
}

#[cfg(not(windows))]
fn refresh_path() {}

/// First line of `<tool> --version` (or -version), without the noise.
pub fn version(path: &Path, flag: &str) -> Option<String> {
    let out = crate::runner::quiet_command(path).arg(flag).output().ok()?;
    let text = String::from_utf8_lossy(if out.stdout.is_empty() { &out.stderr } else { &out.stdout }).into_owned();
    let re = text.lines().next()?.to_string();
    Some(re.trim().chars().take(80).collect())
}

pub const ALL: &[(&str, &str, &str)] = &[
    ("gpp", "C / C++ (GCC)", "--version"),
    ("javac", "Java (JDK)", "-version"),
    ("python", "Python", "--version"),
    ("node", "JavaScript (Node.js)", "--version"),
    ("go", "Go", "version"),
    ("rustc", "Rust", "--version"),
    ("kotlinc", "Kotlin", "-version"),
    ("csc", "C#", "-help"),
    ("clangd", "C / C++ IntelliSense (clangd)", "--version"),
];

/// Every tool NppDev uses, with where it is and its version (Tools panel).
#[tauri::command]
pub async fn list_tools(refresh: bool) -> Vec<Tool> {
    if refresh { forget(); }
    tauri::async_runtime::spawn_blocking(|| {
        ALL.iter()
            .map(|&(key, name, flag)| {
                let path = find(key);
                let version = path.as_deref().and_then(|p| if key == "csc" { Some("installed".into()) } else { version(p, flag) });
                Tool { key, name, path: path.map(|p| p.to_string_lossy().into_owned()), version }
            })
            .collect()
    })
    .await
    .unwrap_or_default()
}
