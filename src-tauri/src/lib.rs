mod fs;
mod import;
mod lsp;
mod runner;
mod settings;
mod tests;
mod tools;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_window_state::Builder::default().build())
        .setup(|app| {
            use tauri::Manager;
            // Competitive Companion sends problems here from the browser
            let port = import::listen(app.handle().clone());
            app.manage(import::CompanionPort(port));
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
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
