//! Settings, recent folders and per-folder sessions: one JSON file in the app's config folder
//! (%APPDATA%\dev.rohithprem.nppdev on Windows, ~/.config/dev.rohithprem.nppdev on Ubuntu).
//! The UI owns the format; this only stores it.

use std::fs;
use tauri::{AppHandle, Manager};

fn file(app: &AppHandle) -> Result<std::path::PathBuf, String> {
    let dir = app.path().app_config_dir().map_err(|e| e.to_string())?;
    fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    Ok(dir.join("settings.json"))
}

#[tauri::command]
pub fn load_settings(app: AppHandle) -> Result<String, String> {
    let f = file(&app)?;
    Ok(fs::read_to_string(f).unwrap_or_else(|_| "{}".into()))
}

/// Written to a temporary file first, so a crash never leaves half a settings file.
#[tauri::command]
pub fn save_settings(app: AppHandle, json: String) -> Result<(), String> {
    let f = file(&app)?;
    let tmp = f.with_extension("json.tmp");
    fs::write(&tmp, json).map_err(|e| e.to_string())?;
    fs::rename(&tmp, &f).map_err(|e| e.to_string())
}
