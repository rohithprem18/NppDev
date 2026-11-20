//! Settings > Languages: whether each compiler, runtime and language server works, and one-click
//! installs. jdtls (Java IntelliSense) is downloaded into NppDev's own tools folder; pylsp comes
//! from pip; compilers come from winget on Windows (in a visible window). On Linux NppDev shows the
//! apt command instead of running it, because it needs your password.

use serde::Serialize;
use std::path::Path;
use tauri::{AppHandle, Manager};

use crate::runner::quiet_command;
use crate::{lsp, tools};

const JDTLS_URL: &str = "https://download.eclipse.org/jdtls/milestones/1.56.0/jdt-language-server-1.56.0-202601291528.tar.gz";

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Status {
    key: &'static str,
    name: &'static str,
    /// What it's for, in a few words.
    role: &'static str,
    ok: bool,
    detail: Option<String>,
    /// How to install it: "auto" (NppDev does it), "winget", or the command to run ("apt ...").
    install: Option<String>,
}

/// (key, name, role, winget id, apt package)
const ROWS: &[(&str, &str, &str, &str, &str)] = &[
    ("gpp", "C / C++ (GCC)", "Run C and C++", "BrechtSanders.WinLibs.POSIX.UCRT", "build-essential"),
    ("javac", "Java (JDK 21)", "Run Java", "EclipseAdoptium.Temurin.21.JDK", "openjdk-21-jdk"),
    ("python", "Python", "Run Python", "Python.Python.3.13", "python3"),
    ("node", "Node.js", "Run JavaScript", "OpenJS.NodeJS.LTS", "nodejs"),
    ("go", "Go", "Run Go", "GoLang.Go", "golang-go"),
    ("rustc", "Rust", "Run Rust", "Rustlang.Rust.GNU", "rustc"),
    ("kotlinc", "Kotlin", "Run Kotlin", "", "kotlin"),
    ("csc", "C#", "Run C#", "", "mono-mcs"),
    ("clangd", "clangd", "C / C++ IntelliSense", "LLVM.LLVM", "clangd"),
    ("pylsp", "pylsp", "Python IntelliSense", "", "python3-pylsp"),
    ("jdtls", "jdtls", "Java IntelliSense", "", ""),
];

fn first_line(p: &Path, flag: &str) -> Option<String> {
    tools::version(p, flag).filter(|v| !v.is_empty())
}

#[tauri::command]
pub async fn language_status(app: AppHandle, refresh: bool) -> Vec<Status> {
    if refresh { tools::forget(); lsp::forget_checks(); }
    tauri::async_runtime::spawn_blocking(move || {
        ROWS.iter().map(|&(key, name, role, winget, apt)| {
            let (ok, detail) = match key {
                "pylsp" => match tools::find("python") {
                    Some(py) if lsp::has_pylsp(&py) => (true, Some("installed".into())),
                    Some(_) => (false, None),
                    None => (false, Some("needs Python".into())),
                },
                "jdtls" => match (lsp::find_jdtls(&app), lsp::java_for_jdtls()) {
                    (Some(_), Some(_)) => (true, Some("installed".into())),
                    (Some(_), None) => (false, Some("needs Java 21 or newer".into())),
                    (None, java) => (false, if java.is_some() { None } else { Some("needs Java 21 or newer".into()) }),
                },
                "csc" => (tools::find("csc").is_some(), tools::find("csc").map(|_| "installed".into())),
                _ => match tools::find(key) {
                    Some(p) => {
                        let flag = match key { "javac" | "kotlinc" => "-version", "go" => "version", _ => "--version" };
                        (true, first_line(&p, flag).or(Some("installed".into())))
                    }
                    None => (false, None),
                },
            };
            let install = if ok { None } else if matches!(key, "jdtls" | "pylsp" | "kotlinc") && (key != "pylsp" || tools::find("python").is_some()) {
                Some("auto".into())
            } else if cfg!(windows) {
                (!winget.is_empty()).then(|| "winget".into())
            } else {
                (!apt.is_empty()).then(|| format!("sudo apt install {apt}"))
            };
            Status { key, name, role, ok, detail, install }
        }).collect()
    }).await.unwrap_or_default()
}

/// Install one of the rows. jdtls and pylsp finish here; winget runs in its own window and
/// NppDev re-checks when the user presses Refresh.
#[tauri::command]
pub async fn install_language(app: AppHandle, key: String) -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(move || match key.as_str() {
        "jdtls" => {
            let root = app.path().app_local_data_dir().map_err(|e| e.to_string())?.join("tools");
            let dest = root.join("jdtls");
            std::fs::create_dir_all(&dest).map_err(|e| e.to_string())?;
            let archive = root.join("jdtls.tar.gz");
            // curl and tar come with Windows 10+ and Ubuntu. On Windows use its own copies: a
            // Git or MSYS tar earlier on PATH would read "C:\..." as a remote host.
            let sys = |exe: &str| -> std::path::PathBuf {
                if cfg!(windows) { Path::new(&std::env::var_os("SystemRoot").unwrap_or_else(|| "C:\\Windows".into())).join("System32").join(format!("{exe}.exe")) }
                else { exe.into() }
            };
            let ok = quiet_command(sys("curl")).args(["-sfL", "--retry", "2", "-o"]).arg(&archive).arg(JDTLS_URL).status().map(|s| s.success()).unwrap_or(false);
            if !ok { return Err("Couldn't download jdtls from eclipse.org. Check your internet connection.".into()); }
            let ok = quiet_command(sys("tar")).arg("-xzf").arg(&archive).arg("-C").arg(&dest).status().map(|s| s.success()).unwrap_or(false);
            let _ = std::fs::remove_file(&archive);
            if !ok { return Err("Couldn't unpack jdtls.".into()); }
            tools::forget();
            lsp::forget_checks();
            Ok("Java IntelliSense is installed. Open a .java file to use it.".into())
        }
        "kotlinc" => {
            // not on winget: the official compiler zip from JetBrains' GitHub releases
            if tools::find("java").is_none() { return Err("Kotlin runs on Java: install Java (JDK) first.".into()); }
            let root = app.path().app_local_data_dir().map_err(|e| e.to_string())?.join("tools");
            std::fs::create_dir_all(&root).map_err(|e| e.to_string())?;
            let sys = |exe: &str| -> std::path::PathBuf {
                if cfg!(windows) { Path::new(&std::env::var_os("SystemRoot").unwrap_or_else(|| "C:\\Windows".into())).join("System32").join(format!("{exe}.exe")) }
                else { exe.into() }
            };
            let api = quiet_command(sys("curl")).args(["-sfL", "-H", "Accept: application/vnd.github+json", "-A", "NppDev", "https://api.github.com/repos/JetBrains/kotlin/releases/latest"])
                .output().map_err(|e| e.to_string())?;
            let release: serde_json::Value = serde_json::from_slice(&api.stdout).map_err(|_| "Couldn't reach GitHub to find the Kotlin compiler.")?;
            let url = release["assets"].as_array().into_iter().flatten()
                .find(|a| a["name"].as_str().is_some_and(|n| n.starts_with("kotlin-compiler-") && n.ends_with(".zip")))
                .and_then(|a| a["browser_download_url"].as_str()).ok_or("The latest Kotlin release has no compiler zip.")?.to_string();
            let zip = root.join("kotlin-compiler.zip");
            let ok = quiet_command(sys("curl")).args(["-sfL", "--retry", "2", "-o"]).arg(&zip).arg(&url).status().map(|s| s.success()).unwrap_or(false);
            if !ok { return Err("Couldn't download the Kotlin compiler. Check your internet connection.".into()); }
            let _ = std::fs::remove_dir_all(root.join("kotlinc"));
            // the zip holds a kotlinc/ folder; Windows' tar reads zips, Linux has unzip
            let ok = if cfg!(windows) { quiet_command(sys("tar")).arg("-xf").arg(&zip).arg("-C").arg(&root).status() }
                else { quiet_command("unzip").arg("-q").arg("-o").arg(&zip).arg("-d").arg(&root).status() }
                .map(|s| s.success()).unwrap_or(false);
            let _ = std::fs::remove_file(&zip);
            if !ok { return Err("Couldn't unpack the Kotlin compiler.".into()); }
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                for f in ["kotlinc", "kotlin"] {
                    let p = root.join("kotlinc").join("bin").join(f);
                    if let Ok(m) = std::fs::metadata(&p) { let mut perm = m.permissions(); perm.set_mode(0o755); let _ = std::fs::set_permissions(&p, perm); }
                }
            }
            tools::forget();
            Ok("Kotlin is installed.".into())
        }
        "pylsp" => {
            let py = tools::find("python").ok_or("Install Python first.")?;
            let o = quiet_command(&py).args(["-m", "pip", "install", "--user", "--quiet", "python-lsp-server"]).output().map_err(|e| e.to_string())?;
            lsp::forget_checks();
            if o.status.success() { Ok("Python IntelliSense is installed.".into()) }
            else { Err(format!("pip couldn't install it: {}", String::from_utf8_lossy(&o.stderr).lines().last().unwrap_or(""))) }
        }
        other => {
            let id = ROWS.iter().find(|r| r.0 == other).map(|r| r.3).filter(|id| !id.is_empty()).ok_or("NppDev can't install this one for you.")?;
            #[cfg(windows)]
            {
                use std::os::windows::process::CommandExt;
                // its own window, so the user sees it and can approve prompts; NppDev waits for it
                let status = std::process::Command::new("winget")
                    .args(["install", "--id", id, "--exact", "--source", "winget"])
                    .creation_flags(0x0000_0010)
                    .status()
                    .map_err(|_| "winget isn't available on this PC. Install \"App Installer\" from the Microsoft Store, then try again.".to_string())?;
                tools::forget(); // also re-reads PATH, so the new compiler is found without restarting
                lsp::forget_checks();
                let name = ROWS.iter().find(|r| r.0 == other).map(|r| r.1).unwrap_or(other);
                if status.success() { Ok(format!("{name} is installed.")) }
                else { Err(format!("The {name} installer didn't finish (code {}).", status.code().unwrap_or(-1))) }
            }
            #[cfg(not(windows))]
            { let _ = id; Err("Run the command shown, then press Refresh.".into()) }
        }
    }).await.map_err(|e| e.to_string())?
}
