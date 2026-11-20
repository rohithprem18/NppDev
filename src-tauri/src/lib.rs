mod fs;
mod import;
mod lsp;
mod runner;
mod settings;
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
        .plugin(tauri_plugin_window_state::Builder::default().build())
        .manage(StartupPaths(Mutex::new(first)))
        .setup(|app| {
            // Competitive Companion sends problems here from the browser
            let port = import::listen(app.handle().clone());
            app.manage(import::CompanionPort(port));
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            startup_paths,
            import::import_problem,
            import::companion_port,
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
