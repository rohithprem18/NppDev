mod fs;
mod lsp;
mod runner;
mod settings;
mod sql;
mod setup;
mod tests;
mod tools;

use std::path::{Path, PathBuf};
use std::sync::Mutex;
use tauri::{Emitter, Manager};

/// Files and folders named on the command line ("Open with NppDev"), made absolute.
fn paths_from_args(args: &[String], cwd: &Path) -> Vec<String> {
    args.iter()
        .skip(1)
        .filter(|a| !a.starts_with('-'))
        .map(|a| { let p = PathBuf::from(a); if p.is_absolute() { p } else { cwd.join(p) } })
        .filter(|p| p.exists())
        .map(|p| p.to_string_lossy().into_owned())
        .collect()
}

/// "Update now": Windows runs the one-line installer in its own window (it waits for NppDev to
/// close, installs the verified new version and opens it); elsewhere the release page opens.
#[tauri::command]
fn start_update(url: String) -> Result<(), String> {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        let _ = url;
        std::process::Command::new("powershell.exe")
            .args(["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                "irm https://raw.githubusercontent.com/rohithprem18/NppDev/main/install.ps1 | iex; Start-Sleep 3"])
            .creation_flags(0x0000_0010) // CREATE_NEW_CONSOLE: the user sees the progress
            .spawn()
            .map(|_| ())
            .map_err(|e| e.to_string())
    }
    #[cfg(not(windows))]
    {
        // a terminal running the one-line installer (apt asks for the password there); the new
        // version is used the next time NppDev starts
        let script = "curl -fsSL https://raw.githubusercontent.com/rohithprem18/NppDev/main/install.sh | bash; echo; read -r -p 'Press Enter to close. Restart NppDev to use the new version.' _";
        let terminals: [(&str, &[&str]); 4] = [
            ("x-terminal-emulator", &["-e", "bash", "-c"]),
            ("gnome-terminal", &["--", "bash", "-c"]),
            ("konsole", &["-e", "bash", "-c"]),
            ("xterm", &["-e", "bash", "-c"]),
        ];
        for (term, args) in terminals {
            if std::process::Command::new(term).args(args).arg(script).spawn().is_ok() { return Ok(()); }
        }
        std::process::Command::new("xdg-open").arg(&url).spawn().map(|_| ()).map_err(|e| e.to_string())
    }
}

/// What this first window was asked to open; the UI takes it once at startup.
struct StartupPaths(Mutex<Vec<String>>);

#[tauri::command]
fn startup_paths(state: tauri::State<'_, StartupPaths>) -> Vec<String> {
    std::mem::take(&mut *state.0.lock().unwrap())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let args: Vec<String> = std::env::args().collect();
    let first = paths_from_args(&args, &std::env::current_dir().unwrap_or_default());
    tauri::Builder::default()
        // one NppDev: opening another file sends it to the window that is already open
        .plugin(tauri_plugin_single_instance::init(|app, args, cwd| {
            let paths = paths_from_args(&args, Path::new(&cwd));
            if let Some(w) = app.get_webview_window("main") {
                let _ = w.unminimize();
                let _ = w.set_focus();
            }
            if !paths.is_empty() { let _ = app.emit("open-paths", paths); }
        }))
        .plugin(tauri_plugin_dialog::init())
        // remembers size and position, but never the Windows frame: NppDev draws its own title bar
        .plugin(
            tauri_plugin_window_state::Builder::default()
                .with_state_flags(tauri_plugin_window_state::StateFlags::all() & !tauri_plugin_window_state::StateFlags::DECORATIONS)
                .build(),
        )
        .manage(StartupPaths(Mutex::new(first)))
        .setup(|app| {
            if let Some(w) = app.get_webview_window("main") { let _ = w.set_decorations(false); }
            if let Ok(dir) = app.path().app_local_data_dir() { sql::init(dir); }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            startup_paths,
            start_update,
            setup::language_status,
            setup::install_language,
            fs::list_dir,
            fs::list_files,
            lsp::lsp_start,
            lsp::lsp_send,
            lsp::lsp_stop,
            fs::read_text,
            fs::write_text,
            fs::create_file,
            fs::create_dir,
            fs::rename_path,
            fs::delete_path,
            fs::path_exists,
            fs::reveal,
            settings::load_settings,
            settings::save_settings,
            settings::user_snippets_path,
            runner::run_tests,
            sql::sql_schema,
            sql::sql_table,
            sql::sql_reset,
            runner::stop_run,
            runner::warm_up,
            tests::load_tests,
            tests::save_tests,
            tools::list_tools,
        ])
        .build(tauri::generate_context!())
        .expect("error while starting NppDev")
        .run(|_, event| {
            if let tauri::RunEvent::Exit = event { lsp::stop_all(); }
        });
}
