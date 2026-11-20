# NppDev 2 — migration to Tauri 2 (Windows + Ubuntu)

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
- Publishing: allowed once a milestone is built and tested (release as NppDev 2.0 when M1–M6 are done).
- The old Notepad++-based NppDev (plugin/, devtools/, installer/, settings/, fonts/) stays until NppDev 2
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

| Old NppDev (Notepad++ plugin) | NppDev 2 |
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

- [ ] **M1 — shell**: Tauri app runs; Monaco editor; open folder; Explorer (new file/folder, rename,
      delete); tabs; save; Dark/Light; Welcome page; recent folders; window state.
- [ ] **M2 — run & judge**: Run panel; C, C++, Java, Python, JavaScript (+ Go, Rust, Kotlin, C#);
      multiple test cases per file; Accepted / Wrong answer (diff) / Runtime error / TLE / MLE / CE;
      time & memory limits; compile cache; C++ PCH.
- [ ] **M3 — editing**: LSP IntelliSense (clangd, jdtls, pylsp, TypeScript); built-in + user snippets;
      Command Palette; Quick Open; VS Code keys.
- [ ] **M4 — problem import**: Competitive Companion listener; import by URL for Codeforces, AtCoder, HackerRank, HackerEarth, NeetCode,
      CodeChef, GeeksforGeeks, LeetCode; creates the file + test cases.
- [ ] **M5 — tools & extras**: tools detection + one-click installer; integrated terminal; Git panel;
      stress testing; settings export/import.
- [ ] **M6 — ship**: Windows NSIS setup + Ubuntu .deb/.AppImage; update check; install.ps1 / install.sh;
      remove the old Notepad++ files; README; release NppDev 2.0.

## Progress log

(newest last)
