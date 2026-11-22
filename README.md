<a id="readme-top"></a>

<br />
<div align="center">
  <a href="https://github.com/rohithprem18/NppDev">
    <img src="docs/logo.svg" alt="NppDev logo" width="96" height="96">
  </a>

  <h3 align="center">NppDev</h3>

  <p align="center">
    A fast, lightweight code editor for practice: write, run and judge C, C++, Java, Python, JavaScript, Go, Rust, Kotlin, C# and SQL with one key.
    <br />
    <a href="#installation"><strong>Install »</strong></a>
    <br />
    <br />
    <a href="https://github.com/rohithprem18/NppDev/releases">Releases</a>
    &middot;
    <a href="https://github.com/rohithprem18/NppDev/issues/new?labels=bug">Report Bug</a>
    &middot;
    <a href="https://github.com/rohithprem18/NppDev/issues/new?labels=enhancement">Request Feature</a>
    &middot;
    <a href="CONTRIBUTING.md">Contribute</a>
  </p>
</div>

<p align="center">
  <a href="https://github.com/rohithprem18/NppDev/releases/latest"><img src="https://img.shields.io/github/v/release/rohithprem18/NppDev?style=flat-square&label=release&color=2f81f7" alt="Latest release"></a>
  <a href="https://github.com/rohithprem18/NppDev/releases"><img src="https://img.shields.io/github/downloads/rohithprem18/NppDev/total?style=flat-square&color=2f81f7" alt="Downloads"></a>
  <a href="https://github.com/rohithprem18/NppDev/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/rohithprem18/NppDev/ci.yml?branch=main&style=flat-square&label=CI" alt="CI status"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/rohithprem18/NppDev?style=flat-square" alt="License: GPL-3.0"></a>
  <br />
  <a href="CONTRIBUTING.md"><img src="https://img.shields.io/badge/PRs-welcome-brightgreen?style=flat-square" alt="PRs welcome"></a>
  <a href="https://github.com/rohithprem18/NppDev/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22"><img src="https://img.shields.io/github/issues/rohithprem18/NppDev/good%20first%20issue?style=flat-square&label=good%20first%20issues&color=7057ff" alt="Good first issues"></a>
  <a href="#contributors"><img src="https://img.shields.io/github/contributors/rohithprem18/NppDev?style=flat-square&color=orange" alt="Contributors"></a>
  <a href="https://github.com/rohithprem18/NppDev/stargazers"><img src="https://img.shields.io/github/stars/rohithprem18/NppDev?style=flat-square&color=yellow" alt="Stars"></a>
</p>

<div align="center">

**Install on Windows in one line**: open PowerShell (<kbd>Win</kbd>, type `powershell`, <kbd>Enter</kbd>) and paste:

</div>

```powershell
irm https://raw.githubusercontent.com/rohithprem18/NppDev/main/install.ps1 | iex
```

<div align="center">

**Ubuntu**: paste into a terminal:

</div>

```sh
curl -fsSL https://raw.githubusercontent.com/rohithprem18/NppDev/main/install.sh | bash
```

<p align="center"><sub>Both install the latest release, verified against GitHub's SHA-256. Run again to update.</sub></p>

<details>
  <summary>Table of Contents</summary>
  <ol>
    <li><a href="#about-the-project">About The Project</a></li>
    <li><a href="#installation">Installation</a></li>
    <li><a href="#usage">Usage</a></li>
    <li><a href="#languages">Languages</a></li>
    <li><a href="#keyboard-shortcuts">Keyboard Shortcuts</a></li>
    <li><a href="#troubleshooting">Troubleshooting</a></li>
    <li><a href="#building-from-source">Building From Source</a></li>
    <li><a href="#roadmap">Roadmap</a></li>
    <li><a href="#contributing">Contributing</a></li>
    <li><a href="#contributors">Contributors</a></li>
    <li><a href="#license">License</a></li>
    <li><a href="#contact">Contact</a></li>
  </ol>
</details>



## About The Project

<p align="center">
  <img src="docs/screenshot-dark.png" alt="NppDev: a C++ solution open in the editor, its Input, Output (Accepted, 24 ms, 4.6 MB) and Expected output on the right" width="100%">
</p>

NppDev is a desktop editor made for solving problems: open a file, type the input, press <kbd>Ctrl</kbd>+<kbd>Enter</kbd> and see the output next to the code, checked against the expected answer like an online judge. It starts in a moment, uses little memory, and stays out of the way.

### Features

* **Run with one key.** <kbd>Ctrl</kbd>+<kbd>Enter</kbd> compiles (only when the code changed) and runs the file. The panel on the right has three boxes, like an online compiler: **Input**, **Output** and an optional **Expected output**.
* **Judge verdicts.** Accepted, Wrong Answer (the differing lines are marked), Runtime Error (with the reason, e.g. a bad pointer or stack overflow), Time Limit Exceeded, Memory Limit Exceeded or the compiler's message, with the time and memory the program used. Time and memory limits are yours to set.
* **SQL with a built-in database.** A new `.sql` file runs on NppDev's sample database (employees, departments, customers, orders, products, students and more) with nothing to install. It works like a real database: what your queries change stays changed for the next run, and **Reset database** brings back the original data. Results print as tables; the tables and their columns are listed next to your query, and clicking one opens the whole database in a tab, with every row, column type and key.
* **IntelliSense.** Completions, errors as you type, go to definition and rename, from clangd (C/C++), pylsp (Python) and jdtls (Java); JavaScript and TypeScript are built in.
* **Command Palette and Quick Open.** <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> for any command, <kbd>Ctrl</kbd>+<kbd>P</kbd> for any file in the folder.
* **Snippets** for competitive programming (`cp`, `fori`, `dsu`, `segtree`, `dijkstra`, ...), plus your own.
* **Auto save.** Files save themselves a second after you stop typing (you can turn it off in Settings).
* **Settings** (<kbd>Ctrl</kbd>+<kbd>,</kbd>): dark or light theme, editor font and size, auto save, update check, and a **Languages** list that shows which compilers and language servers work, with one-click installs.
* **Updates ask first.** When a new release is out, NppDev shows a box: Update now, Skip this version, or Later.
* **Fits into Windows.** Start menu entry, "Open with NppDev" on files and folders in File Explorer, one window for everything you open.
* **Small.** One 7.5 MB program; your projects are never changed by NppDev's settings or tests.

### Built With

<p>
  <a href="https://tauri.app"><img src="https://img.shields.io/badge/Tauri_2-24C8D8?style=for-the-badge&logo=tauri&logoColor=white" alt="Tauri"></a>
  <a href="https://www.rust-lang.org"><img src="https://img.shields.io/badge/Rust-000000?style=for-the-badge&logo=rust&logoColor=white" alt="Rust"></a>
  <a href="https://react.dev"><img src="https://img.shields.io/badge/React_19-20232A?style=for-the-badge&logo=react&logoColor=61DAFB" alt="React"></a>
  <a href="https://www.typescriptlang.org"><img src="https://img.shields.io/badge/TypeScript-3178C6?style=for-the-badge&logo=typescript&logoColor=white" alt="TypeScript"></a>
  <a href="https://microsoft.github.io/monaco-editor/"><img src="https://img.shields.io/badge/Monaco_Editor-0078D4?style=for-the-badge&logo=visualstudiocode&logoColor=white" alt="Monaco Editor"></a>
  <a href="https://www.sqlite.org"><img src="https://img.shields.io/badge/SQLite-003B57?style=for-the-badge&logo=sqlite&logoColor=white" alt="SQLite"></a>
</p>

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## Installation

### Windows 10 and 11

Paste into PowerShell:

```powershell
irm https://raw.githubusercontent.com/rohithprem18/NppDev/main/install.ps1 | iex
```

It downloads `NppDev.exe` from the latest release, checks it against the SHA-256 GitHub publishes, installs it to `%LOCALAPPDATA%\Programs\NppDev`, and adds NppDev to the Start menu and to "Open with" in File Explorer. Run it again to update, or let NppDev ask you when a new version is out. To remove NppDev, use Settings > Apps.

To keep NppDev on the taskbar, right-click its taskbar icon and choose **Pin to taskbar**.

### Ubuntu 22.04 and newer

Paste into a terminal:

```sh
curl -fsSL https://raw.githubusercontent.com/rohithprem18/NppDev/main/install.sh | bash
```

It downloads the `.deb` from the latest release, checks it against GitHub's SHA-256 and installs it with apt (it asks for your password once). Run it again to update; remove NppDev with `sudo apt remove npp-dev`. There's also an `.AppImage` on the [releases page](https://github.com/rohithprem18/NppDev/releases/latest).

### Compilers

NppDev uses the compilers on your computer. **Settings > Languages** shows which ones it found and installs the missing ones: on Windows through winget (GCC, Java, Python, Node.js, Go, Rust, clangd), and directly for Kotlin, Java IntelliSense (jdtls) and Python IntelliSense (pylsp). On Ubuntu it shows the `apt` command to run.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## Usage

1. **Open a folder** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd>) and pick or create a file.
2. **Type the input** in the Input box on the right. Add the **Expected output** if you want it checked.
3. **Press <kbd>Ctrl</kbd>+<kbd>Enter</kbd>.** The Output box shows what the program printed, the verdict, the time and the memory.

Each file keeps its own Input and Expected output, so switching between problems is instant.

**SQL:** create a file ending in `.sql` and press <kbd>Ctrl</kbd>+<kbd>Enter</kbd>. Instead of the Input box, the panel lists the sample database's tables and columns. Click a table name to open the **sample.db** tab (all rows, with each column's type, primary and foreign keys, and the `CREATE TABLE` statement under Structure), or **Insert** to add `SELECT * FROM` it to your file. The Command Palette has **SQL: Open Sample Database** too. Table and column names also show up as you type (`employees.` lists its columns).


<p align="center">
  <img src="docs/screenshot-light.png" alt="NppDev in the light theme" width="100%">
</p>

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## Languages

| Language | Runs with | IntelliSense |
| --- | --- | --- |
| C, C++ | GCC (C++17, or C++20 when the code needs it; `bits/stdc++.h` precompiled) | clangd |
| Java | JDK 17 or newer | jdtls (JDK 21 or newer) |
| Python | Python 3 | pylsp |
| JavaScript | Node.js, with `next()`, `nextInt()`, `readline()` and `print()` for reading input | built in |
| Go | Go | |
| Rust | rustc | |
| Kotlin | kotlinc | |
| C# | csc (Windows) or mcs (Linux) | |
| SQL | built in (SQLite), on a sample database | table and column names |

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## Keyboard Shortcuts

| Action | Keys |
| --- | --- |
| Run | <kbd>Ctrl</kbd>+<kbd>Enter</kbd> or <kbd>F5</kbd> |
| Show or hide Input / Output | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>B</kbd> |
| Quick Open a file | <kbd>Ctrl</kbd>+<kbd>P</kbd> |
| Command Palette | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> or <kbd>F1</kbd> |
| Settings | <kbd>Ctrl</kbd>+<kbd>,</kbd> |
| New file / Open folder | <kbd>Ctrl</kbd>+<kbd>N</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd> |
| Save / Save as | <kbd>Ctrl</kbd>+<kbd>S</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Close tab / Next tab | <kbd>Ctrl</kbd>+<kbd>W</kbd> / <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Show or hide the Explorer | <kbd>Ctrl</kbd>+<kbd>B</kbd> |
| Font size | <kbd>Ctrl</kbd>+<kbd>=</kbd> / <kbd>Ctrl</kbd>+<kbd>-</kbd> / <kbd>Ctrl</kbd>+<kbd>0</kbd> |

**Editing** (the same keys as VS Code):

| Action | Keys |
| --- | --- |
| Move line up / down | <kbd>Alt</kbd>+<kbd>↑</kbd> / <kbd>Alt</kbd>+<kbd>↓</kbd> |
| Duplicate line | <kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>↓</kbd> |
| Delete line | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>K</kbd> |
| Toggle comment | <kbd>Ctrl</kbd>+<kbd>/</kbd> |
| Select next match / all matches | <kbd>Ctrl</kbd>+<kbd>D</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>L</kbd> |
| Add cursor above / below | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>↑</kbd> / <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>↓</kbd> (or <kbd>Alt</kbd>+click) |
| Find / Replace | <kbd>Ctrl</kbd>+<kbd>F</kbd> / <kbd>Ctrl</kbd>+<kbd>H</kbd> |
| Go to line | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Suggestions / parameter hints | <kbd>Ctrl</kbd>+<kbd>Space</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Space</kbd> |
| Go to definition / find references | <kbd>F12</kbd> / <kbd>Shift</kbd>+<kbd>F12</kbd> |
| Rename / next error | <kbd>F2</kbd> / <kbd>F8</kbd> |
| Format document | <kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd> |
| Fold / unfold | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>[</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>]</kbd> |
| Expand selection / jump to bracket | <kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>→</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>\</kbd> |

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## Troubleshooting

The same few questions come up most often: a tool that isn't found, a language server that won't start, or not knowing where NppDev keeps its files. Below: installing compilers, IntelliSense, settings and saved test cases, stale output, verdicts, SQL, build files and updates. If yours isn't here, search the [open issues](https://github.com/rohithprem18/NppDev/issues) or open a new one with the **bug** label — include your OS, the NppDev version (title bar) and what the Output box showed.

**A compiler "isn't found" when I press Ctrl+Enter — what do I do?**

NppDev runs the compilers already on your computer; it doesn't bundle them. Open **Settings** (<kbd>Ctrl</kbd>+<kbd>,</kbd>) > **Languages**: each row shows whether the tool was found (with its version) and, when it wasn't, an install button. On Windows the install runs through winget in a visible window; on Ubuntu the row shows the `sudo apt install ...` command to run yourself, because it needs your password. Kotlin, jdtls (Java IntelliSense) and pylsp are installed by NppDev directly instead. The exact packages per language:

| Language | Windows (winget) | Ubuntu (apt) | Also used for |
| --- | --- | --- | --- |
| C / C++ (GCC) | `BrechtSanders.WinLibs.POSIX.UCRT` | `build-essential` | running C and C++ |
| Java (JDK 21) | `EclipseAdoptium.Temurin.21.JDK` | `openjdk-21-jdk` | running Java, jdtls |
| Python 3 | `Python.Python.3.13` | `python3` | running Python, pylsp |
| Node.js | `OpenJS.NodeJS.LTS` | `nodejs` | running JavaScript |
| Go | `GoLang.Go` | `golang-go` | running Go |
| Rust | `Rustlang.Rust.GNU` | `rustc` | running Rust |
| Kotlin | downloaded from JetBrains' releases (needs Java first) | `kotlin` | running Kotlin |
| C# | — | `mono-mcs` (`csc` on Windows) | running C# |
| clangd | `LLVM.LLVM` | `clangd` | C/C++ IntelliSense |
| pylsp | `pip install --user python-lsp-server` (needs Python first) | `python3-pylsp` | Python IntelliSense |
| jdtls | downloaded from eclipse.org (needs JDK 21+) | — | Java IntelliSense |
| SQL (SQLite) | built in | built in | running SQL |

Once the row shows *installed*, run your file again. If you installed a compiler by hand into a folder that wasn't on `PATH` when NppDev started, restart NppDev so it picks up the new `PATH`.

**IntelliSense (completions, red squiggles) doesn't start for my language. Why?**

Each language has its own language server, and **Settings > Languages** is the checklist: clangd for C/C++, pylsp for Python (needs `python` on `PATH`; NppDev installs it with `pip install --user python-lsp-server`), jdtls for Java (needs JDK 21 or newer; NppDev downloads it from eclipse.org into its own tools folder), while JavaScript and TypeScript are built into the editor and SQL gets its completions from the built-in database. Open a file of that language — the server starts on first use, and the first start can take a few seconds while it indexes your folder. If you installed the server after NppDev started, re-check the row or restart NppDev.

**Where are my settings stored?**

In one JSON file in NppDev's config folder: `%APPDATA%\dev.rohithprem.nppdev\settings.json` on Windows, `~/.config/dev.rohithprem.nppdev/settings.json` on Ubuntu. It's written to a temporary file first and then moved into place, so a crash can never leave it half-written. The file holds the theme, editor font and size, auto save, the default language for new files, the sidebar width, the recent folders and the per-folder session (open tabs, Input/Output split), plus the version you chose to skip in the update prompt. Your own snippets live next to it, in `snippets.json`.

**Where are the Input / Expected output and the time and memory limits I set for a file?**

In NppDev's data folder, **never inside your project**: `<app data>/tests/<name>.json`, where `<app data>` is `%APPDATA%\dev.rohithprem.nppdev` on Windows and `~/.local/share/dev.rohithprem.nppdev` on Ubuntu, and `<name>` is a fixed hash of the file's lower-cased path (so the same file always finds its tests, whatever slash style or capitalization the path has). The file looks like this:

```json
{
  "tests": [{ "input": "3 4\n", "expected": "7\n" }],
  "timeLimitMs": 1000,
  "memoryLimitMb": 256
}
```

You normally never touch it: the run panel edits it, and saving an empty Input, Expected output and default limits deletes the file again. Deleting the file just resets that file's saved tests and limits.

**My code change didn't seem to run — the output looks stale.**

NppDev compiles **only when the code changed** — the run result tells you whether the compile was served from cache — so a changed file always runs the new code. If you edited the file outside NppDev while auto save was off, save first with <kbd>Ctrl</kbd>+<kbd>S</kbd>. Auto save is on by default and saves a second after you stop typing; turn it off in **Settings** if you prefer. If the compile itself failed, the Output box shows the compiler's message instead of a verdict.

**My program runs forever — how do I stop it?**

The Run button on the tab bar turns into **Stop** while a run is in progress; press it (or <kbd>Ctrl</kbd>+<kbd>Enter</kbd> again) and the run ends with the verdict **Stopped**. The time limit also stops it on its own with **TLE**.

**Wrong Answer, but the Output and Expected output look identical.**

The judge compares line by line: trailing spaces on a line and blank lines at the end are ignored (and `\r\n` counts as `\n`), but a missing space at the *start* of a line, an extra line in the middle, or a different token anywhere is a real difference — the differing lines are marked in the panel. If the Expected output box is empty, the run is judged **OK** (ran fine, nothing to compare against), not AC.

**The verdict says Time Limit Exceeded or Memory Limit Exceeded.**

Your program used more than the limits set for that file — by default 2000 ms and 256 MB. Change them in the run panel next to Input/Output; they are stored per file together with its test cases (see above). The limits apply to the run, not the compile, and a run that exceeds them is stopped.

**My SQL queries changed the sample database and now everything looks wrong.**

That's by design: a `.sql` file runs on NppDev's sample database — one file, `sample.db` in the app's data folder, created from the bundled `sample.sql` on first run — and what your queries change stays changed for the next run. Press **Reset database** (or reset it from the **sample.db** tab) to bring back the original data.

**Does NppDev leave build files in my project folder?**

No. Compiled programs go to `<temp>/nppdev-run/<hash of the source path>` — `%TEMP%\nppdev-run` on Windows, `/tmp/nppdev-run` on Ubuntu — and your project folder keeps only your files. NppDev's own data (settings, snippets, saved tests) lives in the config and data folders described above, also outside your projects.

**My program's output got cut off.**

Output is capped at 4 MB; when it's longer, the Output box ends with "Output is longer than 4 MB; the rest isn't shown." Print less (or loop over a summary), and use the Input box to feed smaller test data.

**The first C++ compile is slow — can it be faster?**

Yes, and it already is after the first time: code that includes `bits/stdc++.h` is compiled against a precompiled header that NppDev prepares once per compiler in the background (C++17 and C++20), which typically makes such compiles about twice as fast. Unchanged code isn't rebuilt at all.

**How do I add my own snippets?**

Your own snippets live in `snippets.json` next to `settings.json` in the config folder (see above). NppDev watches that file, so editing it takes effect immediately; the built-in competitive-programming snippets (`cp`, `fori`, `dsu`, `segtree`, `dijkstra`, ...) are separate and can't be edited, only extended.

**"Open with NppDev" is missing from the right-click menu in File Explorer.**

The installer registers it; run the one-line installer from [Installation](#installation) again to repair the registration. You can always open files from inside NppDev instead: <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd> opens a folder, <kbd>Ctrl</kbd>+<kbd>P</kbd> quick-opens any file in it.

**NppDev won't start on Ubuntu — no window opens.**

The desktop app needs WebKitGTK and a tray library, which the `.deb` normally pulls in as dependencies. If you installed from a minimal system or run the `.AppImage` without them, install them yourself: `sudo apt install libwebkit2gtk-4.1-0 libayatana-appindicator3-1`, then start NppDev again. On Windows no extra components are needed — the installer sets up everything.

**How do I update or remove NppDev?**

When a new release is out, NppDev shows a box with **Update now**, **Skip this version** and **Later** — or run the one-line installer from [Installation](#installation) again; it verifies the download against GitHub's SHA-256 and updates in place. You can also grab any version, including the `.AppImage`, from the [releases page](https://github.com/rohithprem18/NppDev/releases). To remove NppDev, use Settings > Apps on Windows or `sudo apt remove npp-dev` on Ubuntu; your settings, snippets and saved tests stay in the folders described above until you delete them yourself.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## Building From Source

NppDev is a [Tauri 2](https://tauri.app) app: Rust for the judge, files and language servers; React, TypeScript and the [Monaco](https://microsoft.github.io/monaco-editor/) editor for the window.

You need Node.js 20+, Rust (stable) and, on Ubuntu, Tauri's system libraries (`libwebkit2gtk-4.1-dev` and friends; see `.github/workflows/linux.yml`).

```sh
npm ci
npx tauri dev                              # run it while you work on it
npx tauri build --no-bundle                # Windows: src-tauri/target/release/nppdev.exe
npx tauri build --bundles deb,appimage     # Ubuntu packages
cargo test --manifest-path src-tauri/Cargo.toml --lib -- --test-threads=1   # judge tests
```

The judge tests compile and run real programs in every installed language and check each verdict. They also run on Ubuntu for every push and pull request. Working on NppDev? [CONTRIBUTING.md](CONTRIBUTING.md) explains the project layout and how pull requests are handled.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## Roadmap

- [x] Run and judge with time and memory limits
- [x] IntelliSense for C/C++, Java, Python, JavaScript
- [x] Settings with one-click language installs
- [x] Update prompt
- [x] Ubuntu packages
- [ ] Stress testing against a brute-force solution
- [ ] Settings export and import

See the [open issues](https://github.com/rohithprem18/NppDev/issues) for proposed features and known issues. Want to build one of these? Comment on its issue and see [Contributing](#contributing).

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## Contributing

NppDev is open source and contributions of every size are welcome, from fixing a typo to adding a language. You don't need to be an expert: issues labelled [**good first issue**](https://github.com/rohithprem18/NppDev/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22) are picked to be a friendly start.

1. **Pick an issue** (or open one with your idea) and comment that you're working on it.
2. **Fork** the repository and create a branch: `git checkout -b feature/my-change`.
3. **Make your change** and run the checks: `npm run build` and the judge tests (see [Building From Source](#building-from-source)).
4. **Open a pull request.** CI builds NppDev and runs the judge tests on Ubuntu.
5. **That's it.** When every check passes, the pull request is **merged automatically**, and your avatar shows up under [Contributors](#contributors).

Read [CONTRIBUTING.md](CONTRIBUTING.md) for the project layout, coding style, and which files always get a maintainer review. Everyone taking part follows the [Code of Conduct](https://github.com/rohithprem18/.github/blob/main/CODE_OF_CONDUCT.md).

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## Contributors

Thanks to everyone who has helped build NppDev!

<!-- CONTRIBUTORS:START -->
<table>
  <tbody>
    <tr>
      <td align="center" valign="top" width="14%"><a href="https://github.com/rohithprem18"><img src="https://avatars.githubusercontent.com/u/146315115?v=4&s=100" width="80" height="80" alt="rohithprem18"/><br /><sub><b>rohithprem18</b></sub></a><br /><sub>41 commits</sub></td>
    </tr>
  </tbody>
</table>
<p><sub>1 contributor so far. This list updates automatically after every merged pull request.</sub></p>
<!-- CONTRIBUTORS:END -->

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## License

Distributed under the GNU General Public License v3.0. See `LICENSE` for more information.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



## Contact

Rohith Prem: [github.com/rohithprem18](https://github.com/rohithprem18)

Project Link: [https://github.com/rohithprem18/NppDev](https://github.com/rohithprem18/NppDev)

<p align="right">(<a href="#readme-top">back to top</a>)</p>
