//! Test cases for each source file, kept in the app's data folder (never inside the user's project).
//! File: <app data>/tests/<hash of the lower-cased source path>.json

use serde_json::Value;
use std::path::PathBuf;
use tauri::{AppHandle, Manager};

/// Same path (any slash, any case) -> same file name. FNV-1a: fixed forever, unlike DefaultHasher.
fn key(path: &str) -> String {
    let mut h: u64 = 0xcbf29ce484222325;
    for b in path.replace('\\', "/").to_lowercase().bytes() {
        h ^= b as u64;
        h = h.wrapping_mul(0x100000001b3);
    }
    format!("{h:016x}.json")
}

fn file_for(app: &AppHandle, path: &str) -> Option<PathBuf> {
    let dir = app.path().app_data_dir().ok()?.join("tests");
    std::fs::create_dir_all(&dir).ok()?;
    Some(dir.join(key(path)))
}

/// The saved tests for `path` ({ tests, timeLimitMs, memoryLimitMb, problem }), or null.
#[tauri::command]
pub fn load_tests(app: AppHandle, path: String) -> Value {
    file_for(&app, &path)
        .and_then(|f| std::fs::read_to_string(f).ok())
        .and_then(|s| serde_json::from_str(&s).ok())
        .unwrap_or(Value::Null)
}

/// Save (or, with null, delete) the tests for `path`.
#[tauri::command]
pub fn save_tests(app: AppHandle, path: String, data: Value) -> Result<(), String> {
    let f = file_for(&app, &path).ok_or("No app data folder")?;
    if data.is_null() {
        let _ = std::fs::remove_file(&f);
        return Ok(());
    }
    let tmp = f.with_extension("tmp");
    std::fs::write(&tmp, serde_json::to_vec_pretty(&data).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    std::fs::rename(&tmp, &f).map_err(|e| e.to_string())
}

#[cfg(test)]
#[test]
fn key_is_stable() {
    assert_eq!(key(r"C:\A\b.cpp"), key("c:/a/B.cpp"));
    if let Ok(p) = std::env::var("NPPDEV_KEY_PATH") { println!("KEY {}", key(&p)); }
}
