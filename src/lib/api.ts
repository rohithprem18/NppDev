// Typed wrappers around NppDev's Rust commands (src-tauri/src).
import { invoke } from "@tauri-apps/api/core";

export interface Entry {
  name: string;
  path: string;
  isDir: boolean;
}

export const api = {
  listDir: (path: string) => invoke<Entry[]>("list_dir", { path }),
  readText: (path: string) => invoke<string>("read_text", { path }),
  writeText: (path: string, content: string) => invoke<void>("write_text", { path, content }),
  createFile: (path: string) => invoke<void>("create_file", { path }),
  createDir: (path: string) => invoke<void>("create_dir", { path }),
  renamePath: (from: string, to: string) => invoke<void>("rename_path", { from, to }),
  deletePath: (path: string) => invoke<void>("delete_path", { path }),
  pathExists: (path: string) => invoke<boolean>("path_exists", { path }),
  reveal: (path: string) => invoke<void>("reveal", { path }),
  loadSettings: () => invoke<string>("load_settings"),
  saveSettings: (json: string) => invoke<void>("save_settings", { json }),
};

/** "C:\a\b.cpp" -> "b.cpp" (either slash). */
export const baseName = (p: string) => p.replace(/[\\/]+$/, "").split(/[\\/]/).pop() ?? p;
/** "C:\a\b.cpp" -> "C:\a". */
export const dirName = (p: string) => p.replace(/[\\/][^\\/]*$/, "");
/** Joins with the separator the folder already uses. */
export const joinPath = (dir: string, name: string) => {
  const sep = dir.includes("\\") ? "\\" : "/";
  return dir.replace(/[\\/]+$/, "") + sep + name;
};
/** "C:\Users\me\code" -> "~\code" for display. */
export const shortPath = (p: string, home?: string) => (home && p.toLowerCase().startsWith(home.toLowerCase()) ? "~" + p.slice(home.length) : p);
