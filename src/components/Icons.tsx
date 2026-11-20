// Interface icons: Lucide (https://lucide.dev, ISC licence), generated from lucide-static.
// File-type icons are separate: see FileIcon.tsx (Material Icon Theme, MIT).
import type { JSX } from "react";

const S = (body: JSX.Element, size = 16, fill = false) => (
  <svg width={size} height={size} viewBox="0 0 24 24" fill={fill ? "currentColor" : "none"} stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
    {body}
  </svg>
);

export const Icon = {
  files: (s?: number) => S(<><path d="M15 2h-4a2 2 0 0 0-2 2v11a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2V8" /> <path d="M16.706 2.706A2.4 2.4 0 0 0 15 2v5a1 1 0 0 0 1 1h5a2.4 2.4 0 0 0-.706-1.706z" /> <path d="M5 7a2 2 0 0 0-2 2v11a2 2 0 0 0 2 2h8a2 2 0 0 0 1.732-1" /></>, s),
  command: (s?: number) => S(<><path d="m7 11 2-2-2-2" /> <path d="M11 13h4" /> <rect width="18" height="18" x="3" y="3" rx="2" ry="2" /></>, s),
  settings: (s?: number) => S(<><path d="M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915" /> <circle cx="12" cy="12" r="3" /></>, s),
  run: (s?: number) => S(<><path d="M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z" /></>, s, true),
  stop: (s?: number) => S(<><rect width="18" height="18" x="3" y="3" rx="2" /></>, s, true),
  newFile: (s?: number) => S(<><path d="M6 22a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h8a2.4 2.4 0 0 1 1.704.706l3.588 3.588A2.4 2.4 0 0 1 20 8v12a2 2 0 0 1-2 2z" /> <path d="M14 2v5a1 1 0 0 0 1 1h5" /> <path d="M9 15h6" /> <path d="M12 18v-6" /></>, s),
  newFolder: (s?: number) => S(<><path d="M12 10v6" /> <path d="M9 13h6" /> <path d="M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2Z" /></>, s),
  refresh: (s?: number) => S(<><path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8" /> <path d="M21 3v5h-5" /> <path d="M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16" /> <path d="M8 16H3v5" /></>, s),
  collapse: (s?: number) => S(<><path d="m7 20 5-5 5 5" /> <path d="m7 4 5 5 5-5" /></>, s),
  close: (s?: number) => S(<><path d="M18 6 6 18" /> <path d="m6 6 12 12" /></>, s),
  sun: (s?: number) => S(<><circle cx="12" cy="12" r="4" /> <path d="M12 2v2" /> <path d="M12 20v2" /> <path d="m4.93 4.93 1.41 1.41" /> <path d="m17.66 17.66 1.41 1.41" /> <path d="M2 12h2" /> <path d="M20 12h2" /> <path d="m6.34 17.66-1.41 1.41" /> <path d="m19.07 4.93-1.41 1.41" /></>, s),
  moon: (s?: number) => S(<><path d="M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46.402.803a6 6 0 0 0 8.268 8.268c.344-.215.825-.004.803.401" /></>, s),
  search: (s?: number) => S(<><path d="m21 21-4.34-4.34" /> <circle cx="11" cy="11" r="8" /></>, s),
  check: (s?: number) => S(<><path d="M20 6 9 17l-5-5" /></>, s),
  cross: (s?: number) => S(<><path d="M18 6 6 18" /> <path d="m6 6 12 12" /></>, s),
  plus: (s?: number) => S(<><path d="M5 12h14" /> <path d="M12 5v14" /></>, s),
  trash: (s?: number) => S(<><path d="M10 11v6" /> <path d="M14 11v6" /> <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6" /> <path d="M3 6h18" /> <path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" /></>, s),
  folderOpen: (s?: number) => S(<><path d="m6 14 1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.54 6a2 2 0 0 1-1.95 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2" /></>, s),
  alert: (s?: number) => S(<><circle cx="12" cy="12" r="10" /> <line x1="12" x2="12" y1="8" y2="12" /> <line x1="12" x2="12.01" y1="16" y2="16" /></>, s),
  info: (s?: number) => S(<><circle cx="12" cy="12" r="10" /> <path d="M12 16v-4" /> <path d="M12 8h.01" /></>, s),
  chevron: (open: boolean) => (
    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" strokeLinejoin="round" style={{ transform: open ? "rotate(90deg)" : undefined, transition: "transform .12s" }} aria-hidden="true">
      <path d="m9 18 6-6-6-6" />
    </svg>
  ),
};
