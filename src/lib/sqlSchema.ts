// The built-in sample database: its tables and columns (from the app itself, so they always match
// what a run sees), each table in full for the database tab, and the "sample.db" tab's own state.
// Used by the Run panel's table list, the database tab and SQL suggestions in the editor.
import { invoke } from "@tauri-apps/api/core";
import { create } from "zustand";

export interface SqlTable {
  name: string;
  columns: string[];
  rows: number;
}

export interface SqlColumn {
  name: string;
  ty: string;
  notNull: boolean;
  primaryKey: boolean;
  unique: boolean;
  default: string | null;
  /** "departments.dept_id" for a foreign key. */
  references: string | null;
}

export interface TableData {
  name: string;
  columns: SqlColumn[];
  rows: (string | number | null)[][];
  /** The CREATE TABLE statement. */
  sql: string;
}

/** The database tab is a tab like any file's, under this name. */
export const DB_TAB = "db:sample";
export const DB_TAB_NAME = "sample.db";
export const isDbTab = (path: string | null | undefined) => path === DB_TAB;

let schema: Promise<SqlTable[]> | null = null;
let schemaNow: SqlTable[] | null = null;

export function sqlSchema(): Promise<SqlTable[]> {
  schema ??= invoke<SqlTable[]>("sql_schema").catch(() => []).then((t) => (schemaNow = t));
  return schema;
}

/** The schema if it has loaded already, so lists can draw it on their first frame. */
export const schemaLoaded = () => schemaNow;

const tables = new Map<string, Promise<TableData | null>>();
const tablesNow = new Map<string, TableData>();

export function sqlTable(name: string): Promise<TableData | null> {
  let p = tables.get(name);
  if (!p) {
    p = invoke<TableData>("sql_table", { name }).then((d) => { tablesNow.set(name, d); return d; }, () => null);
    tables.set(name, p);
  }
  return p;
}

export const tableLoaded = (name: string) => tablesNow.get(name) ?? null;

/** What the database tab shows, the .sql file it was opened from ("Query in practice.sql"), and a
 * version that goes up whenever the data may have changed (after a run, after Reset). */
export const useDbView = create<{ table: string; view: "data" | "structure"; from: string | null; version: number }>(() => ({ table: "employees", view: "data", from: null, version: 0 }));

/** A query may have changed the database: load it again. The last copy stays on screen until then. */
export function refreshDb() {
  schema = null;
  tables.clear();
  tablesNow.clear();
  useDbView.setState((v) => ({ version: v.version + 1 }));
}

/** Put the original data back. */
export async function resetDb() {
  await invoke("sql_reset");
  refreshDb();
}

type Pos = { lineNumber: number; column: number };
type Editor = {
  getModel(): { getLineContent(n: number): string; getLineCount(): number } | null;
  getPosition(): Pos | null;
  executeEdits(source: string, edits: { range: { startLineNumber: number; startColumn: number; endLineNumber: number; endColumn: number }; text: string }[]): void;
  setPosition(p: Pos): void;
  revealPositionInCenterIfOutsideViewport(p: Pos): void;
  focus(): void;
};

/** Put a query into the editor: on the cursor's line when it's empty, else on a new line below it,
 * so it never splits the code you were writing. `atEnd` adds it after the last line instead. */
export function insertSql(text: string, atEnd = false) {
  const ed = (window as unknown as { nppdevEditor?: Editor }).nppdevEditor;
  const model = ed?.getModel();
  const at = ed?.getPosition();
  if (!ed || !model || !at) return;
  const pos = atEnd ? { lineNumber: model.getLineCount(), column: 1 } : at;
  const line = model.getLineContent(pos.lineNumber);
  const empty = !line.trim();
  const range = empty
    ? { startLineNumber: pos.lineNumber, startColumn: 1, endLineNumber: pos.lineNumber, endColumn: line.length + 1 }
    : { startLineNumber: pos.lineNumber, startColumn: line.length + 1, endLineNumber: pos.lineNumber, endColumn: line.length + 1 };
  ed.executeEdits("sample-db", [{ range, text: empty ? text : "\n" + text }]);
  const end = { lineNumber: pos.lineNumber + (empty ? 0 : 1), column: text.length + 1 };
  ed.setPosition(end);
  ed.revealPositionInCenterIfOutsideViewport(end);
  ed.focus();
}
