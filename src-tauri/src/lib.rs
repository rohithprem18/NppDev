mod fs;
mod settings;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_window_state::Builder::default().build())
        .invoke_handler(tauri::generate_handler![
            fs::list_dir,
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
        ])
        .run(tauri::generate_context!())
        .expect("error while running NppDev");
}
