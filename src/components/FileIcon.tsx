// File and folder icons from Material Icon Theme (MIT, see src/assets/file-icons). The SVGs are
// tiny, so the build inlines them: no extra files to load.
import { fileType } from "../lib/fileTypes";

const ICONS = import.meta.glob("../assets/file-icons/*.svg", { eager: true, query: "?url", import: "default" }) as Record<string, string>;
const url = (name: string) => ICONS[`../assets/file-icons/${name}.svg`] ?? ICONS["../assets/file-icons/document.svg"];

export function FileIcon({ name }: { name: string }) {
  return <img className="file-icon" src={url(fileType(name).icon)} width={16} height={16} alt="" draggable={false} />;
}

export function FolderIcon({ open }: { open: boolean }) {
  return <img className="file-icon" src={url(open ? "folder-open" : "folder")} width={16} height={16} alt="" draggable={false} />;
}
