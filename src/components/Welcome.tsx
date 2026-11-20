// The start page: shown when no file is open.
import { useApp } from "../store";
import { useImport } from "../import";
import { baseName } from "../lib/api";
import { Icon } from "./Icons";
import logo from "../assets/logo.svg";

const KEYS: [string, string][] = [
  ["Run the current file", "Ctrl+Enter"],
  ["Import a problem", "Ctrl+I"],
  ["Quick Open a file", "Ctrl+P"],
  ["Command Palette", "Ctrl+Shift+P"],
  ["Open Folder", "Ctrl+Shift+O"],
  ["Show / hide the Explorer", "Ctrl+B"],
  ["Save", "Ctrl+S"],
  ["Toggle comment", "Ctrl+/"],
  ["Find / Replace", "Ctrl+F / Ctrl+H"],
];

export function Welcome() {
  const { recent, folder, openFolder, newFile } = useApp();
  return (
    <div className="welcome">
      <div className="welcome-inner">
        <div className="welcome-head">
          <img src={logo} width={64} height={64} alt="" />
          <div>
            <h1>NppDev</h1>
            <p>Write, run and judge your code — fast.</p>
          </div>
        </div>
        <div className="welcome-cols">
          <section>
            <h2>Start</h2>
            <button className="link" onClick={newFile}>{Icon.newFile()} New File <kbd>Ctrl+N</kbd></button>
            <button className="link" onClick={() => openFolder()}>{Icon.folderOpen()} Open Folder… <kbd>Ctrl+Shift+O</kbd></button>
            <button className="link" onClick={() => useImport.getState().show(true)}>{Icon.download()} Import a Problem… <kbd>Ctrl+I</kbd></button>
            <h2>Recent</h2>
            {recent.length === 0 && <p className="muted">Folders you open appear here.</p>}
            {recent.slice(0, 8).map((r) => (
              <button key={r} className={"link recent" + (r === folder ? " current" : "")} onClick={() => openFolder(r)} title={r}>
                <span className="recent-name">{baseName(r)}</span>
                <span className="recent-path">{r}</span>
              </button>
            ))}
          </section>
          <section>
            <h2>Shortcuts</h2>
            <table className="keys">
              <tbody>
                {KEYS.map(([what, keys]) => (
                  <tr key={what}><td>{what}</td><td><kbd>{keys}</kbd></td></tr>
                ))}
              </tbody>
            </table>
          </section>
        </div>
      </div>
    </div>
  );
}
