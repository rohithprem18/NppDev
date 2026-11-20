// Small inline SVG icons (no icon library: keeps NppDev light).
import type { JSX } from "react";

const S = (d: JSX.Element, size = 16) => (
  <svg width={size} height={size} viewBox="0 0 16 16" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    {d}
  </svg>
);

export const Icon = {
  files: (s?: number) => S(<><path d="M4.5 1.5h5l3 3v8.5a1 1 0 0 1-1 1h-7a1 1 0 0 1-1-1v-10.5a1 1 0 0 1 1-1z" /><path d="M9.5 1.5v3h3" /></>, s),
  run: (s?: number) => S(<path d="M4.5 2.5v11l9-5.5z" fill="currentColor" stroke="none" />, s),
  settings: (s?: number) => S(<><circle cx="8" cy="8" r="2.2" /><path d="M8 1.5v2M8 12.5v2M1.5 8h2M12.5 8h2M3.4 3.4l1.4 1.4M11.2 11.2l1.4 1.4M3.4 12.6l1.4-1.4M11.2 4.8l1.4-1.4" /></>, s),
  newFile: (s?: number) => S(<><path d="M9.5 1.5h-5a1 1 0 0 0-1 1v11a1 1 0 0 0 1 1h7a1 1 0 0 0 1-1v-8.5z" /><path d="M8 7v5M5.5 9.5h5" /></>, s),
  newFolder: (s?: number) => S(<><path d="M1.5 4.5v8a1 1 0 0 0 1 1h11a1 1 0 0 0 1-1v-6.5a1 1 0 0 0-1-1h-6l-1.5-2h-3.5a1 1 0 0 0-1 1z" /><path d="M8 7.5v4M6 9.5h4" /></>, s),
  refresh: (s?: number) => S(<><path d="M13 8a5 5 0 1 1-1.5-3.5" /><path d="M13 2.5v3h-3" /></>, s),
  collapse: (s?: number) => S(<><rect x="2.5" y="2.5" width="11" height="11" rx="1.5" /><path d="M5.5 8h5" /></>, s),
  chevron: (open: boolean) => (
    <svg width="16" height="16" viewBox="0 0 16 16" style={{ transform: open ? "rotate(90deg)" : undefined, transition: "transform .12s" }} aria-hidden="true">
      <path d="M6 4l4 4-4 4" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  ),
  folder: (open: boolean) => (
    <svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true">
      <path d="M1.5 4a1 1 0 0 1 1-1h3.6l1.5 1.7h5.9a1 1 0 0 1 1 1V12a1 1 0 0 1-1 1h-11a1 1 0 0 1-1-1z" fill={open ? "#c09553" : "#d7a85d"} />
      {open && <path d="M3 7h12l-1.6 6H1.6z" fill="#e9c27e" />}
    </svg>
  ),
  close: (s?: number) => S(<path d="M4 4l8 8M12 4l-8 8" />, s),
  sun: (s?: number) => S(<><circle cx="8" cy="8" r="3" /><path d="M8 1v1.5M8 13.5V15M1 8h1.5M13.5 8H15M3 3l1 1M12 12l1 1M3 13l1-1M12 4l1-1" /></>, s),
  moon: (s?: number) => S(<path d="M13.5 9.5A5.5 5.5 0 0 1 6.5 2.5a5.5 5.5 0 1 0 7 7z" />, s),
  folderOpen: (s?: number) => S(<><path d="M1.5 4.5v8a1 1 0 0 0 1 1h11a1 1 0 0 0 1-1v-6.5a1 1 0 0 0-1-1h-6l-1.5-2h-3.5a1 1 0 0 0-1 1z" /></>, s),
  command: (s?: number) => S(<><rect x="2" y="2.5" width="12" height="11" rx="2" /><path d="M5 6.5l2 1.5-2 1.5M8.5 10h2.5" /></>, s),
  stop: (s?: number) => S(<rect x="3.5" y="3.5" width="9" height="9" rx="1.5" fill="currentColor" stroke="none" />, s),
  plus: (s?: number) => S(<path d="M8 3v10M3 8h10" />, s),
  trash: (s?: number) => S(<><path d="M2.5 4h11M6 4V2.5h4V4M4 4l.7 9.5h6.6L12 4" /><path d="M6.8 6.5v4.5M9.2 6.5v4.5" /></>, s),
  beaker: (s?: number) => S(<><path d="M6 1.5v4.2L2.3 12.6a1.3 1.3 0 0 0 1.1 1.9h9.2a1.3 1.3 0 0 0 1.1-1.9L10 5.7V1.5" /><path d="M5 1.5h6M4.2 9.5h7.6" /></>, s),
  download: (s?: number) => S(<><path d="M8 2v8M4.5 7L8 10.5 11.5 7" /><path d="M2.5 12.5v1h11v-1" /></>, s),
};
