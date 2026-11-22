# Contributing to NppDev

Thank you for helping make NppDev better! Everyone is welcome here: first-time contributors, students, competitive programmers, experienced developers, and people who don't write code at all. Reporting a bug, improving the README, adding a snippet or fixing a typo all count.

## Ways to help

| You like... | Try this |
| --- | --- |
| Writing | Improve the README or the docs, explain a feature better, fix a typo |
| Competitive programming | Add snippets for your language in [`src/lib/snippets.ts`](src/lib/snippets.ts) |
| React / TypeScript | The window, panels and editor features in [`src/`](src) |
| Rust | The judge, SQL runner, files and language servers in [`src-tauri/src/`](src-tauri/src) |
| Testing | Try NppDev on your machine and report what breaks |

Look for issues labelled [`good first issue`](https://github.com/rohithprem18/NppDev/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22) or [`help wanted`](https://github.com/rohithprem18/NppDev/issues?q=is%3Aissue+is%3Aopen+label%3A%22help+wanted%22). Comment on the issue to claim it so nobody else starts the same thing. Have your own idea? Open an issue first so we can agree on the approach.

## Set up

You need **Node.js 20+** and **Rust (stable)**. On Ubuntu you also need Tauri's system libraries:

```sh
sudo apt-get install -y libwebkit2gtk-4.1-dev libayatana-appindicator3-dev librsvg2-dev patchelf libxdo-dev libssl-dev build-essential file
```

Then:

```sh
git clone https://github.com/<your-username>/NppDev.git
cd NppDev
npm ci
npx tauri dev        # opens NppDev and reloads as you edit
```

For the judge tests, install the compilers you want covered (GCC, a JDK, Python 3, Node.js, Rust, Mono or the .NET csc). Languages that aren't installed are skipped.

## Accessibility

Icon-only buttons must carry an `aria-label` (usually the same text as their `title`), and
toggle buttons an `aria-pressed`. `node scripts/check-aria-labels.mjs` verifies this over
`src/components` — please run it before opening a PR.

## Project layout

```text
src/                     The window: React + TypeScript + Monaco
  components/            Title bar, Explorer, editor area, Run panel, Settings, Command Palette...
  lib/                   Editor setup, language servers (LSP client), snippets, commands, updates
  store.ts               App state (zustand)
src-tauri/src/           The Rust side
  runner.rs              Compile and run with time and memory limits; verdicts
  sql.rs, sql/           SQL files on the built-in SQLite sample database
  lsp.rs                 clangd, pylsp and jdtls over stdio
  tools.rs, setup.rs     Finding and installing compilers and language servers
  fs.rs, settings.rs     Files, folders and settings
docs/DESIGN.md           How the app looks and behaves, and why
```

## Before you open a pull request

Run the same checks as CI:

```sh
npm run build                                                            # type-check and build the window
cargo test --manifest-path src-tauri/Cargo.toml --lib -- --test-threads=1   # judge tests
```

And please:

- **Keep it focused**: one fix or feature per pull request.
- **Add a test** in `src-tauri/src/` when you change how code is run or judged.
- **Follow the style around you**: comments explain *why*; user-facing text is short and plain.
- **Add a screenshot or GIF** for anything visible in the window.
- **Read [docs/DESIGN.md](docs/DESIGN.md)** before changing the look of the app.

## What happens next

1. **CI runs**: the frontend build and the judge tests on Ubuntu. First-time contributors may need the maintainer to approve the run.
2. **If everything passes, your pull request is merged automatically.**
3. **Your avatar appears in the README's [Contributors](README.md#contributors) section** a few minutes later.

Changes to workflows, installers (`install.ps1`, `install.sh`), dependencies (`package.json`, `Cargo.toml`, lockfiles), the Tauri config and capabilities, and the code that downloads or installs things (`src-tauri/src/setup.rs`, `src/lib/update.ts`) always wait for a maintainer review. They get the `needs-maintainer-review` label automatically.

If a check fails, open it from the **Checks** tab of your pull request, push a fix, and CI runs again.

## Reporting bugs

Open an issue with the **Bug report** form. Include your OS, the NppDev version (Settings shows it), the language, and the smallest program and input that show the problem.

Security problem? Please report it privately; see [SECURITY.md](https://github.com/rohithprem18/.github/blob/main/SECURITY.md).

## Code of Conduct

Everyone taking part follows the [Code of Conduct](https://github.com/rohithprem18/.github/blob/main/CODE_OF_CONDUCT.md). Be kind and patient; everyone was new once.

## License

By contributing, you agree that your contributions are licensed under the [GNU GPL v3.0](LICENSE), like the rest of NppDev.
