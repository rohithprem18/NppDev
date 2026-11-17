"""Find the compilers and language servers on this PC (NppDev / Notepad++ Dev Tools).

Nothing is hard-coded to one machine: every tool is looked up when it is needed, so an
upgrade or a newly installed tool is picked up without changing any setting.

    python -I tools.py --report     JSON describing every tool, for the Check / Install Tools window

A choice made in that window is saved in tools.ini next to this file and wins over the search:
    [paths]
    gpp      = C:\\...\\bin\\g++.exe     C and C++ compiler (gcc.exe is taken from the same folder)
    clangd   = C:\\...\\clangd.exe       C/C++ IntelliSense
    java     = C:\\...\\jdk-17           JDK that compiles and runs Java programs
    java_lsp = C:\\...\\jdk-21           JDK (21 or newer) for Java IntelliSense
"""
import glob
import os
import re
import subprocess
import sys

BASE = os.path.dirname(os.path.abspath(__file__))
INI = os.path.join(BASE, "tools.ini")
LOCAL = os.environ.get("LOCALAPPDATA", "")
PROGRAMS = [os.environ.get(v) for v in ("ProgramFiles", "ProgramW6432", "ProgramFiles(x86)")]
PROGRAMS = sorted({p for p in PROGRAMS if p})
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
JAVA_LSP_MIN = 21
GPP_MIN = 9                    # older g++ lacks parts of C++17; a newer one is offered (and preferred once installed)
PYTHON_MIN = (3, 9)
HIDDEN = set(os.environ.get("NPPDEV_TEST_HIDE", "").split(","))   # testing: pretend these tools are missing


def settings():
    import configparser                      # only when tools.ini exists (it usually doesn't)
    cp = configparser.ConfigParser()
    try:
        cp.read(INI, encoding="utf-8")
    except (OSError, configparser.Error):
        pass
    return cp


def chosen(key):
    """A path picked in the Check / Install Tools window, if it still exists."""
    if not os.path.isfile(INI):
        return None
    p = settings().get("paths", key, fallback="").strip().strip('"')
    return p if p and os.path.exists(p) else None


CACHE_FILE = os.path.join(os.environ.get("TEMP", BASE), "nppdev-cppstd.json")


def cached(key, compute):
    """compute() once per key, remembered in %TEMP% across runs (keys include the tool's date)."""
    import json
    try:
        cache = json.load(open(CACHE_FILE, encoding="utf-8"))
    except (OSError, ValueError):
        cache = {}
    if key in cache:
        return cache[key]
    value = compute()
    cache[key] = value
    try:                                     # whole-file replace: runs and language servers start together
        tmp = "%s.%d.tmp" % (CACHE_FILE, os.getpid())
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(cache, f)
        os.replace(tmp, CACHE_FILE)
    except OSError:
        pass
    return value


def run_text(cmd, timeout=10):
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
                           creationflags=NO_WINDOW, stdin=subprocess.DEVNULL)
        return p.stdout.decode("utf-8", "replace")
    except (OSError, subprocess.SubprocessError):
        return ""


def version_key(text):
    """'14.2.0' -> (14, 2, 0); unknown versions sort first."""
    return tuple(int(x) for x in re.findall(r"\d+", text or "")[:3]) or (0,)


def unique(paths):
    seen, out = set(), []
    for p in paths:
        if not p:
            continue
        k = os.path.normcase(os.path.normpath(p))
        if k not in seen and os.path.exists(p):
            seen.add(k)
            out.append(os.path.normpath(p))
    return out


def on_path(exe):
    """exe in each PATH folder, in PATH order, with links followed to the real file
    (Oracle's ...\\Common Files\\Oracle\\Java\\javapath and winget's ...\\WinGet\\Links hold only
    links). WindowsApps store stubs are skipped."""
    hits = []
    for d in os.environ.get("PATH", "").split(os.pathsep):
        d = d.strip().strip('"')
        if not d or "windowsapps" in d.lower():
            continue
        p = os.path.join(d, exe)
        if os.path.isfile(p):
            try:
                real = os.path.realpath(p)
            except OSError:
                real = p
            hits.append(real if os.path.isfile(real) else p)
    return unique(hits)


# ---------------- C / C++ ----------------

def gcc_bin_dirs():
    dirs = [os.path.dirname(p) for p in on_path("g++.exe")]
    for root in ("C:\\msys64", "D:\\msys64"):
        dirs += [os.path.join(root, env, "bin") for env in ("ucrt64", "mingw64", "clang64")]
    dirs += ["C:\\MinGW\\bin", "C:\\mingw64\\bin", "C:\\TDM-GCC-64\\bin", "C:\\Strawberry\\c\\bin"]
    dirs += glob.glob(os.path.join(LOCAL, "Microsoft", "WinGet", "Packages", "BrechtSanders.WinLibs*", "mingw64", "bin"))
    dirs += glob.glob("C:\\ProgramData\\chocolatey\\lib\\mingw\\tools\\install\\mingw64\\bin")
    for pf in PROGRAMS:
        dirs += glob.glob(os.path.join(pf, "mingw-w64", "*", "mingw64", "bin"))
        dirs += [os.path.join(pf, "CodeBlocks", "MinGW", "bin")]
    return unique(d for d in dirs if os.path.isfile(os.path.join(d, "g++.exe")))


_versions = {}


def gpp_version(exe):
    """'14.2.0' for a g++ (asked once per build of that g++, then remembered)."""
    if exe not in _versions:
        try:
            stamp = "version|%s|%d" % (os.path.normcase(exe), int(os.path.getmtime(exe)))
        except OSError:
            stamp = None
        ask = lambda: run_text([exe, "-dumpfullversion"]).strip() or "?"   # noqa: E731
        _versions[exe] = cached(stamp, ask) if stamp else ask()
    return _versions[exe]


def gpp_ok(exe):
    return version_key(gpp_version(exe))[0] >= GPP_MIN


def gpp_candidates():
    return [{"path": os.path.join(d, "g++.exe"), "version": gpp_version(os.path.join(d, "g++.exe"))} for d in gcc_bin_dirs()]


def find_gpp():
    """g++ to use: the one picked in the window; else the first on PATH that is recent enough
    (GPP_MIN); else the newest recent-enough one found; else whatever old one there is."""
    if "gpp" in HIDDEN:
        return None
    pick = chosen("gpp")
    if pick:
        return pick
    path_hits = on_path("g++.exe")
    for exe in path_hits:
        if gpp_ok(exe):
            return exe
    cands = gpp_candidates()
    if cands:
        return max(cands, key=lambda c: version_key(c["version"]))["path"]
    return path_hits[0] if path_hits else None


def find_gcc():
    gpp = find_gpp()
    if gpp:
        gcc = os.path.join(os.path.dirname(gpp), "gcc.exe")
        if os.path.isfile(gcc):
            return gcc
    return None


def cpp_std(gpp):
    """Newest C++ standard (up to C++20) this g++ accepts; cached per compiler build."""
    try:
        stamp = "%s|%d" % (os.path.normcase(gpp), int(os.path.getmtime(gpp)))
    except OSError:
        return None

    def probe():
        for s in ("gnu++20", "gnu++17", "gnu++14", "gnu++11"):
            try:
                p = subprocess.run([gpp, "-std=" + s, "-fsyntax-only", "-x", "c++", "-"], input=b"int main(){}\n",
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20, creationflags=NO_WINDOW)
                if p.returncode == 0:
                    return s
            except (OSError, subprocess.SubprocessError):
                break
        return None
    return cached(stamp, probe)


def clangd_candidates():
    exes = on_path("clangd.exe")
    exes += [os.path.join(d, "clangd.exe") for d in gcc_bin_dirs()]
    for pf in PROGRAMS:
        exes.append(os.path.join(pf, "LLVM", "bin", "clangd.exe"))
    out = []
    for exe in unique(exes):
        m = re.search(r"version\s+([\d.]+)", run_text([exe, "--version"]))
        out.append({"path": exe, "version": m.group(1) if m else "?"})
    return out


def find_clangd():
    if "clangd" in HIDDEN:
        return None
    pick = chosen("clangd")
    if pick:
        return pick
    gpp = find_gpp()
    if gpp and os.path.isfile(os.path.join(os.path.dirname(gpp), "clangd.exe")):
        return os.path.join(os.path.dirname(gpp), "clangd.exe")       # same toolchain as the compiler
    cands = clangd_candidates()
    return max(cands, key=lambda c: version_key(c["version"]))["path"] if cands else None


# ---------------- Java ----------------

def is_jdk(home):
    """A folder with bin\\javac.exe and bin\\java.exe (a JDK, not a JRE or a folder of links)."""
    return bool(home) and all(os.path.isfile(os.path.join(home, "bin", x)) for x in ("javac.exe", "java.exe"))


def jdk_version(home, javac=None):
    try:
        text = open(os.path.join(home, "release"), encoding="utf-8", errors="replace").read()
        m = re.search(r'JAVA_VERSION="([^"]+)"', text)
        if m:
            return m.group(1)
    except (OSError, TypeError):
        pass
    m = re.search(r"javac\s+([\d._]+)", run_text([javac or os.path.join(home, "bin", "javac.exe"), "-version"]))
    return m.group(1) if m else "?"


def java_major(version):
    v = version_key(version)
    return v[1] if v[0] == 1 and len(v) > 1 else v[0]     # 1.8.0 -> 8


def jdk_homes():
    homes = [os.path.dirname(os.path.dirname(p)) for p in on_path("javac.exe")]
    if os.environ.get("JAVA_HOME"):
        homes.append(os.environ["JAVA_HOME"])
    vendors = ("Eclipse Adoptium", "Java", "Microsoft", "Zulu", "Amazon Corretto", "BellSoft", "Semeru",
               "OpenJDK", "AdoptOpenJDK", "ojdkbuild", "RedHat", "Android\\Android Studio")
    for pf in PROGRAMS:
        for v in vendors:
            homes += glob.glob(os.path.join(pf, v, "*"))
        homes.append(os.path.join(pf, "Android", "Android Studio", "jbr"))
    homes += glob.glob(os.path.join(LOCAL, "Programs", "Eclipse Adoptium", "*"))
    homes += glob.glob(os.path.join(os.path.expanduser("~"), ".jdks", "*"))
    return unique(h for h in homes if is_jdk(h))


def jdk_candidates():
    return [{"path": h, "version": jdk_version(h)} for h in jdk_homes()]


def jdk_pair(home):
    return os.path.join(home, "bin", "javac.exe"), os.path.join(home, "bin", "java.exe")


def find_java_tools():
    """(javac.exe, java.exe) that compile and run programs, or (None, None): the JDK picked in the
    window, else the javac on PATH, else JAVA_HOME, else the newest JDK found."""
    if "java" in HIDDEN:
        return None, None
    pick = chosen("java")
    if is_jdk(pick):
        return jdk_pair(pick)
    for javac in on_path("javac.exe"):
        home = os.path.dirname(os.path.dirname(javac))
        if is_jdk(home):
            return jdk_pair(home)
        java = os.path.join(os.path.dirname(javac), "java.exe")       # a folder of launchers: use them as they are
        if not os.path.isfile(java):
            java = next(iter(on_path("java.exe")), None)
        if java:
            return javac, java
    jh = os.environ.get("JAVA_HOME", "").strip().strip('"')
    if is_jdk(jh):
        return jdk_pair(jh)
    cands = jdk_candidates()
    if cands:
        return jdk_pair(max(cands, key=lambda c: version_key(c["version"]))["path"])
    return None, None


def find_java_home():
    """The JDK folder behind find_java_tools() (or the launchers' folder), for showing it."""
    javac, _ = find_java_tools()
    if not javac:
        return None
    home = os.path.dirname(os.path.dirname(javac))
    return home if is_jdk(home) else os.path.dirname(javac)


def find_java_lsp_home():
    """JDK for the Java language server: the picked one, else the newest that is 21 or later."""
    if "java_lsp" in HIDDEN:
        return None
    pick = chosen("java_lsp")
    if pick:
        return pick
    ok = [c for c in jdk_candidates() if java_major(c["version"]) >= JAVA_LSP_MIN]
    return max(ok, key=lambda c: version_key(c["version"]))["path"] if ok else None


def find_jdtls():
    for d in (os.path.join(BASE, "jdtls-1.56"), os.path.join(BASE, "jdtls")):
        jars = glob.glob(os.path.join(d, "plugins", "org.eclipse.equinox.launcher_*.jar"))
        if jars:
            return d, sorted(jars)[-1]
    return None, None


# ---------------- Python ----------------

def have_pylsp():
    """pylsp importable by this Python (asked without -I, so per-user packages count too)."""
    if "pylsp" in HIDDEN:
        return False
    try:
        return subprocess.run([sys.executable.replace("pythonw.exe", "python.exe"), "-c", "import pylsp"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20,
                              creationflags=NO_WINDOW).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


# ---------------- JavaScript ----------------

NODE_MIN = 18


def node_version(exe):
    try:
        stamp = "node|%s|%d" % (os.path.normcase(exe), int(os.path.getmtime(exe)))
    except OSError:
        return "?"
    return cached(stamp, lambda: run_text([exe, "--version"]).strip().lstrip("v") or "?")


def find_node():
    """node.exe to run .js files: the one picked in the window, else the first on PATH (nvm, fnm,
    volta and winget installs put theirs there), else the usual install folders."""
    if "node" in HIDDEN:
        return None
    pick = chosen("node")
    if pick:
        return pick
    hits = on_path("node.exe")
    if hits:
        return hits[0]
    dirs = [os.path.join(pf, "nodejs") for pf in PROGRAMS]
    dirs += [os.path.join(LOCAL, "Programs", "nodejs"), "C:\\nvm4w\\nodejs"]
    dirs += sorted(glob.glob(os.path.join(os.environ.get("APPDATA", ""), "nvm", "v*")), reverse=True)
    for d in dirs:
        exe = os.path.join(d, "node.exe")
        if os.path.isfile(exe):
            return exe
    return None


def node_ok(exe):
    return version_key(node_version(exe))[0] >= NODE_MIN


def npm_global_dirs(node):
    """Where `npm install -g` puts packages: %APPDATA%\\npm, or next to node.exe (nvm, custom prefix)."""
    dirs = [os.path.join(os.environ.get("APPDATA", ""), "npm", "node_modules")]
    if node:
        dirs.insert(0, os.path.join(os.path.dirname(node), "node_modules"))
    return [d for d in dirs if os.path.isdir(d)]


def find_js_lsp(node=None):
    """JavaScript IntelliSense: (server command, tsserver.js or None), or (None, None).
    TypeScript 7+ (npm install -g typescript) has a fast native language server built in
    (tsc.exe --lsp --stdio); older TypeScript needs typescript-language-server + tsserver.js."""
    if "js_lsp" in HIDDEN:
        return None, None
    node = node or find_node()
    dirs = npm_global_dirs(node)
    for d in dirs:
        for native in (os.path.join(d, "typescript", "node_modules", "@typescript", "typescript-win32-x64", "lib", "tsc.exe"),
                       os.path.join(d, "@typescript", "typescript-win32-x64", "lib", "tsc.exe")):
            if os.path.isfile(native):
                return [native, "--lsp", "--stdio"], None
    cli = tss = None
    for d in dirs:
        c = os.path.join(d, "typescript-language-server", "lib", "cli.mjs")
        t = os.path.join(d, "typescript", "lib", "tsserver.js")
        cli = cli or (c if os.path.isfile(c) else None)
        tss = tss or (t if os.path.isfile(t) else None)
    if cli and tss and node:
        return [node, cli, "--stdio"], tss
    return None, None


def path_with(*dirs):
    """PATH for a child process, with the toolchain folders first (compiled programs need their DLLs)."""
    extra = [d for d in dirs if d]
    return os.pathsep.join(extra + [os.environ.get("PATH", "")])


# ---------------- report for the Check / Install Tools window ----------------

def report():
    gpp, clangd = find_gpp(), find_clangd()
    (javac, _), jlsp = find_java_tools(), find_java_lsp_home()
    jrun = find_java_home()
    jdks = jdk_candidates()
    jdtls_dir, _ = find_jdtls()
    py_ver = "%d.%d.%d" % sys.version_info[:3]
    tools = [
        {"key": "python", "name": "Python", "need": "Runs .py files and the editor helpers",
         "ok": sys.version_info >= PYTHON_MIN, "path": sys.executable, "version": py_ver, "candidates": []},
        {"key": "gpp", "name": "C / C++ compiler (gcc, g++)", "need": "Runs .c and .cpp files",
         "ok": bool(gpp) and gpp_ok(gpp), "old": bool(gpp) and not gpp_ok(gpp),
         "path": gpp or "", "version": gpp_version(gpp) if gpp else "",
         "std": cpp_std(gpp) if gpp else None, "candidates": gpp_candidates()},
        {"key": "clangd", "name": "C / C++ IntelliSense (clangd)", "need": "Autocomplete and errors in .c / .cpp",
         "ok": bool(clangd), "path": clangd or "", "version": "", "candidates": clangd_candidates()},
        {"key": "java", "name": "Java (JDK)", "need": "Runs .java files",
         "ok": bool(javac), "path": jrun or "", "version": jdk_version(jrun, javac) if javac else "", "candidates": jdks},
        {"key": "java_lsp", "name": "Java IntelliSense (JDK %d+)" % JAVA_LSP_MIN, "need": "Autocomplete and errors in .java",
         "ok": bool(jlsp and jdtls_dir), "path": jlsp or "", "version": jdk_version(jlsp) if jlsp else "",
         "candidates": [c for c in jdks if java_major(c["version"]) >= JAVA_LSP_MIN]},
        {"key": "pylsp", "name": "Python IntelliSense (pylsp)", "need": "Autocomplete and errors in .py",
         "ok": have_pylsp(), "path": "", "version": "", "candidates": []},
    ]
    node = find_node()
    js_cmd, _ = find_js_lsp(node)
    tools += [
        {"key": "node", "name": "JavaScript (Node.js %d+)" % NODE_MIN, "need": "Runs .js files",
         "ok": bool(node) and node_ok(node), "old": bool(node) and not node_ok(node),
         "path": node or "", "version": node_version(node) if node else "", "candidates": []},
        {"key": "js_lsp", "name": "JavaScript IntelliSense (TypeScript)", "need": "Autocomplete and errors in .js",
         "ok": bool(js_cmd), "path": js_cmd[0] if js_cmd else "", "version": "", "candidates": []},
    ]
    for t in tools:
        if t["key"] in HIDDEN:
            t.update(ok=False, path="", version="", candidates=[])
        if t["key"] == "clangd" and t["ok"]:
            t["version"] = next((c["version"] for c in t["candidates"]
                                 if os.path.normcase(c["path"]) == os.path.normcase(clangd)), "")
    return tools


if __name__ == "__main__":
    if "--report" in sys.argv:
        import json
        sys.stdout.write(json.dumps(report(), indent=1))
