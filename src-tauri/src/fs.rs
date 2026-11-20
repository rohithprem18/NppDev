//! Files and folders for the Explorer and the editor.

use serde::Serialize;
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Entry {
    name: String,
    path: String,
    is_dir: bool,
}

/// Folders that only slow the Explorer down and are never edited by hand.
const HIDDEN: &[&str] = &[".git", "node_modules", "__pycache__", ".idea", ".vs", "target", ".nppdev"];

/// Entries of a folder: folders first, then files, each alphabetically (case-insensitive).
#[tauri::command]
pub fn list_dir(path: String) -> Result<Vec<Entry>, String> {
    let mut out: Vec<Entry> = fs::read_dir(&path)
        .map_err(|e| format!("Can't open {path}: {e}"))?
        .filter_map(Result::ok)
        .filter_map(|d| {
            let name = d.file_name().to_string_lossy().into_owned();
            let is_dir = d.file_type().ok()?.is_dir();
            if is_dir && HIDDEN.contains(&name.as_str()) {
                return None;
            }
            Some(Entry { path: d.path().to_string_lossy().into_owned(), name, is_dir })
        })
        .collect();
    out.sort_by(|a, b| b.is_dir.cmp(&a.is_dir).then_with(|| a.name.to_lowercase().cmp(&b.name.to_lowercase())));
    Ok(out)
}

/// Text of a file. Binary and very large files are refused instead of being shown as garbage.
#[tauri::command]
pub fn read_text(path: String) -> Result<String, String> {
    let meta = fs::metadata(&path).map_err(|e| format!("Can't read {path}: {e}"))?;
    if meta.len() > 20 * 1024 * 1024 {
        return Err("This file is larger than 20 MB, too big to edit here.".into());
    }
    let bytes = fs::read(&path).map_err(|e| format!("Can't read {path}: {e}"))?;
    if bytes.iter().take(8000).any(|&b| b == 0) {
        return Err("This looks like a binary file, so it isn't opened as text.".into());
    }
    let text = match String::from_utf8(bytes) {
        Ok(t) => t,
        Err(e) => String::from_utf8_lossy(e.as_bytes()).into_owned(),
    };
    Ok(text.strip_prefix('\u{feff}').map(str::to_owned).unwrap_or(text))
}

#[tauri::command]
pub fn write_text(path: String, content: String) -> Result<(), String> {
    if let Some(parent) = Path::new(&path).parent() {
        fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    fs::write(&path, content).map_err(|e| format!("Can't save {path}: {e}"))
}

#[tauri::command]
pub fn create_file(path: String) -> Result<(), String> {
    if Path::new(&path).exists() {
        return Err("A file or folder with that name already exists.".into());
    }
    write_text(path, String::new())
}

#[tauri::command]
pub fn create_dir(path: String) -> Result<(), String> {
    if Path::new(&path).exists() {
        return Err("A file or folder with that name already exists.".into());
    }
    fs::create_dir_all(&path).map_err(|e| format!("Can't create {path}: {e}"))
}

#[tauri::command]
pub fn rename_path(from: String, to: String) -> Result<(), String> {
    if Path::new(&to).exists() {
        return Err("A file or folder with that name already exists.".into());
    }
    fs::rename(&from, &to).map_err(|e| format!("Can't rename: {e}"))
}

/// Moves to the Recycle Bin / Trash, so a slip can be undone there.
#[tauri::command]
pub fn delete_path(path: String) -> Result<(), String> {
    trash::delete(&path).map_err(|e| format!("Can't delete {path}: {e}"))
}

#[tauri::command]
pub fn path_exists(path: String) -> bool {
    Path::new(&path).exists()
}

/// Shows a file (or folder) in the system's file manager.
#[tauri::command]
pub fn reveal(path: String) -> Result<(), String> {
    let p = PathBuf::from(&path);
    #[cfg(target_os = "windows")]
    let r = std::process::Command::new("explorer").arg(format!("/select,{}", p.display())).spawn();
    #[cfg(not(target_os = "windows"))]
    let r = std::process::Command::new("xdg-open").arg(if p.is_dir() { p.clone() } else { p.parent().map(Path::to_path_buf).unwrap_or(p.clone()) }).spawn();
    r.map(|_| ()).map_err(|e| e.to_string())
}
