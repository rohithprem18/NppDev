// The built-in sample database's tables and columns (from the app itself, so they always match
// what a run sees). Used by the Run panel's table list and by SQL suggestions in the editor.
import { invoke } from "@tauri-apps/api/core";

export interface SqlTable {
  name: string;
  columns: string[];
  rows: number;
}

let cached: Promise<SqlTable[]> | null = null;

export function sqlSchema(): Promise<SqlTable[]> {
  cached ??= invoke<SqlTable[]>("sql_schema").catch(() => []);
  return cached;
}
