# NppDev — migration to Tauri 2 (Windows + Ubuntu)

Status file for the rebuild. Every work session (interactive or the scheduled "resume" task) starts by
reading this file, continues the first unchecked item, tests it, commits, and ticks it here.

## Decisions (from the owner, Rohith Prem)

- Rebuild NppDev as its own app: **Tauri 2 + Rust + React + TypeScript**, editor = **Monaco** (bundled, offline).
- Targets: **Windows** (NSIS per-user setup `NppDev-Setup.exe`) and **Ubuntu** (`.deb` + `.AppImage`).
- Everything is named **NppDev** (files, identifiers, folders). No Notepad++ inside any more.
- **Fast and small**: no unused dependencies or bundled files; measure startup and size.
- UI/UX: modern, clear, easy to understand (VS Code-like layout, NppDev's own look).
- Install stays the one-line `irm … | iex` (Windows) plus `curl … | bash` (Ubuntu). **No code signing** work.
- Problem import: **Codeforces, AtCoder, CodeChef, GeeksforGeeks, LeetCode, HackerRank, HackerEarth, NeetCode** (+ Competitive Companion).
- Roadmap items to build (all except code signing / winget).
- Commits: author rohithprem18, **no co-author or Claude attribution lines**.
- Name: the app is called **NppDev** everywhere (window, installer, shortcuts, README, release titles). Never "NppDev 2"; 2.0.0 is only the version number.
- Publishing: allowed once a milestone is built and tested (release as NppDev v2.0.0 when M1–M6 are done).
- The old Notepad++-based NppDev (plugin/, devtools/, installer/, settings/, fonts/) stays until NppDev (Tauri)
  matches it, then it is removed (milestone M6).

## Architecture

```
React + TS (src/)          UI: editor (Monaco), Explorer, tabs, Welcome, Run panel, palette, settings
        │  Tauri IPC (invoke + events)
Rust (src-tauri/src/)      fs, process runner + judge, LSP bridge, tools detection/installer,
                           problem import (HTTP fetch + Competitive Companion listener), terminal (PTY), git
        │
OS                         Windows: WebView2 · Ubuntu: WebKitGTK
```

## Feature map (old → new)

| Old NppDev (Notepad++ plugin) | NppDev (Tauri) |
|---|---|
| Editor, tabs, themes, Hack font | Monaco + React tabs; Dark / Light themes; bundled Hack font |
| Explorer (Folder as Workspace + header) | React tree; Rust `fs` commands + watcher |
| Welcome page | React start page with recent folders |
| Quick Open, Command Palette | React palettes |
| Run panel, runner.py judge | React panel + Rust runner (compile cache, C++17/20, PCH, verdicts, limits) |
| tools.py / Check / Install Tools | Rust detection + installer window (winget on Windows, apt on Ubuntu) |
| lsp-proxy.py + NppLspClient | Rust LSP bridge (stdio ↔ IPC) + TS LSP client for Monaco |
| Snippets (Tab), VS Code keys | Monaco snippets (built-in + user), Monaco keybindings |
| Update check (update.c) | Rust check of GitHub releases + download + verified install |
| install.ps1 | install.ps1 (Windows) + install.sh (Ubuntu) |

## Milestones

- [x] **M1 — shell**: Tauri app runs; Monaco editor; open folder; Explorer (new file/folder, rename,
      delete); tabs; save; Dark/Light; Welcome page; recent folders; window state.
- [x] **M2 — run & judge**: Run panel; C, C++, Java, Python, JavaScript (+ Go, Rust, Kotlin, C#);
      multiple test cases per file; Accepted / Wrong answer (diff) / Runtime error / TLE / MLE / CE;
      time & memory limits; compile cache; C++ PCH.
- [x] **M3 — editing**: LSP IntelliSense (clangd, jdtls, pylsp, TypeScript); built-in + user snippets;
      Command Palette; Quick Open; VS Code keys.
- [x] **M4 — problem import**: Competitive Companion listener; import by URL for Codeforces, AtCoder, HackerRank, HackerEarth, NeetCode,
      CodeChef, GeeksforGeeks, LeetCode; creates the file + test cases.
- [ ] **M5 — tools & extras**: tools detection + one-click installer; integrated terminal; Git panel;
      stress testing; settings export/import.
- [ ] **M6 — ship**: Windows = plain NppDev.exe installed by install.ps1 (no setup program) + Ubuntu .deb/.AppImage; update check; install.ps1 / install.sh;
      remove the old Notepad++ files; README; release NppDev v2.0.0.

## Progress log

(newest last)

- 2026-11-20 — M1 done: Tauri 2 + React + Monaco shell, Explorer, tabs, sessions, themes, Welcome.
- 2026-11-20 — M2 done: src-tauri/src/runner.rs (compile cache, C++17/20 auto, bits/stdc++.h PCH warm-up,
  Windows job object: kill tree on TLE, memory cap + MLE via completion port, peak working set;
  Linux rlimit + wait4), tools.rs (compiler discovery), tests.rs (tests per file in app data, FNV key),
  Run panel (src/run.ts, RunPanel.tsx; Ctrl+Enter, per-test run, diff on WA, limits).
  `cargo test --lib` judges C++/Java/Python/JS/Rust/C# programs for AC/WA/RE/TLE/MLE/CE.
  Not tested here: Go, Kotlin (not installed on this PC); Linux paths (needs Ubuntu CI).
- 2026-11-20 — M4 done: src-tauri/src/import.rs (Competitive Companion listener on 27121, falls back
  to CC's other default ports; URL import for Codeforces (curl fallback past Cloudflare), AtCoder,
  CodeChef API, LeetCode GraphQL (+ starter code), NeetCode (mapped to LeetCode via its bundle),
  GfG practice API, HackerRank REST (STDIN/Function notes stripped), HackerEarth (Next.js payload)).
  `cargo test --lib import -- --ignored` checks all 8 sites live. UI: Ctrl+I dialog, src/import.ts,
  src/lib/templates.ts (9 languages). Tested in the app: Codeforces link + simulated CC POST.
- 2026-11-20 — UI redesigned to docs/DESIGN.md (Vercel-inspired: ink/gray, hairlines, stacked shadows,
  Geist + Geist Mono bundled, Hack stays the code font, 6px radius, no emoji). Every later UI change
  must follow docs/DESIGN.md.
- 2026-11-20 — M3 done: src-tauri/src/lsp.rs (stdio bridge for clangd, pylsp, jdtls) + src/lib/lsp.ts
  (Monaco's built-in LSP client; transport filters each server to its own files, re-spells file URIs,
  gives clangd a g++ compile command per file). Verified: clangd diagnostics in the app.
  Command Palette (Ctrl+Shift+P / F1) and Quick Open (Ctrl+P) in src/components/Palette.tsx; built-in
  CP snippets + user snippets.json (Preferences: Edit My Snippets). Font zoom Ctrl+= / Ctrl+- / Ctrl+0.
  Not yet verified by hand: pylsp, jdtls (needs jdtls in app data tools/, M5 installs it), snippets UI.
- 2026-11-20 — Windows install decided: no setup program. Release asset is NppDev.exe; install.ps1
  (irm | iex) verifies it, installs to %LOCALAPPDATA%\Programs\NppDev, adds Start menu, "Open with
  NppDev" (files, folders, folder background) and the Open-with list for code files, a Settings > Apps
  uninstall entry (uninstall.ps1), and removes an old 1.x NppDev in that folder first.
  The app takes files/folders on its command line and keeps one window (tauri-plugin-single-instance).
  Installed on the owner's laptop from a local build (NPPDEV_EXE) and checked. Taskbar pinning can't be
  scripted on Windows 11; the owner pins it by hand. Notepad++ uninstall: UAC prompt was cancelled.
