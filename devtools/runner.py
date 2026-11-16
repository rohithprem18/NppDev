"""Compile and run one source file for the Notepad++ Run panel.

    python -I runner.py SOURCE [--input FILE] [--expected FILE] [--timeout SECONDS]

Program output is streamed to stdout as it happens. Compiler messages keep
their colors (ANSI). The last line is always a status line for the panel:

    @@STATUS <kind> <text>      kind: ok accepted wrong runtime compile tle error
    @@STATUS missing <tool> <text>   tool (gpp, java) isn't installed: the panel offers to install it

Build output goes to %TEMP%\\npp-run, so nothing is written next to the source.
C and C++ are compiled with -DONLINE_JUDGE, so "#ifndef ONLINE_JUDGE freopen(...)"
blocks are skipped and stdin/stdout come from the panel. C++ uses the newest standard
(up to C++20) the installed g++ supports. Compilers and the JDK are found by tools.py.

Speed: unchanged code isn't compiled again; <bits/stdc++.h> comes precompiled (runner.py
--warmup builds it once, in the background); C++ is compiled as C++17 unless the code uses
C++20 (the C++20 library is twice as slow to load); javac starts with quick-start JVM options
and, on JDK 19+, from a class-data archive; slow modules are imported only when needed.
"""
import os
import re
import subprocess
import sys
import threading
import time

try:
    from _sha1 import sha1           # without loading OpenSSL (hashlib), ~8 ms
except ImportError:
    from hashlib import sha1

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # -I leaves this folder out
import tools  # noqa: E402

OUT = sys.stdout.buffer
MISSING = "Open Plugins > Dev Tools > Check / Install Tools to install it"
MAX_SHOWN = 1 << 20          # stop echoing after 1 MB of program output
RED, GREEN, YELLOW, CYAN, GRAY, RESET = "\x1b[31m", "\x1b[32m", "\x1b[33m", "\x1b[36m", "\x1b[90m", "\x1b[0m"

NTSTATUS = {
    -1073741819: "access violation / segmentation fault (bad pointer or array index)",
    -1073741571: "stack overflow (infinite recursion?)",
    -1073741676: "integer division by zero",
    -1073740940: "heap corruption (bad free / buffer overflow)",
    -1073740791: "stack buffer overrun",
    -1073741795: "illegal instruction",
    3221225477: "access violation / segmentation fault (bad pointer or array index)",
    3221225725: "stack overflow (infinite recursion?)",
    3221225620: "integer division by zero",
}


def say(text):
    OUT.write(text.encode("utf-8"))
    OUT.flush()


def status(kind, text):
    say(f"@@STATUS {kind} {text}\n")
    sys.exit(0)


RUN_ROOT = os.path.join(os.environ.get("TEMP", "."), "npp-run")
JAVAC_FAST = ["-J-XX:TieredStopAtLevel=1", "-J-XX:+UseSerialGC", "-J-Xshare:auto"]   # javac starts ~20% faster
STDCPP = re.compile(rb"^\s*#\s*include\s*<bits/stdc\+\+\.h>", re.M)


def build_dir(src):
    h = sha1(os.path.abspath(src).lower().encode("utf-8")).hexdigest()[:12]
    d = os.path.join(RUN_ROOT, h)
    os.makedirs(d, exist_ok=True)
    return d


def build_key(src, tool, flags):
    """Fingerprint of everything a build depends on: the source, its own "local.h" headers,
    the compiler (path and date) and the flags. Same key = the last build can be reused."""
    h = sha1()
    text = open(src, "rb").read()
    h.update(text)
    for inc in re.findall(rb'^\s*#\s*include\s*"([^"]+)"', text, re.M):
        try:
            h.update(open(os.path.join(os.path.dirname(src), inc.decode("utf-8", "replace")), "rb").read())
        except OSError:
            pass
    try:
        h.update(f"{tool}|{os.path.getmtime(tool)}|{' '.join(flags)}".encode("utf-8"))
    except OSError:
        h.update(tool.encode("utf-8"))
    return h.hexdigest()


def up_to_date(output, key):
    try:
        return os.path.exists(output) and open(output + ".key", encoding="ascii").read() == key
    except OSError:
        return False


def mark_built(output, key):
    try:
        open(output + ".key", "w", encoding="ascii").write(key)
    except OSError:
        pass


def pch_dir(compiler, flags):
    """Folder holding bits/stdc++.h.gch for this compiler and these flags (they must match exactly)."""
    try:
        stamp = f"{os.path.normcase(compiler)}|{os.path.getmtime(compiler)}|{' '.join(flags)}"
    except OSError:
        return None
    return os.path.join(RUN_ROOT, "pch", sha1(stamp.encode("utf-8")).hexdigest()[:12])


def build_pch(compiler, flags, env, keep=()):
    """Precompile <bits/stdc++.h> once (~6 s, 100-130 MB) so each C++ compile skips re-reading it (~1-2 s faster).
    Older ones (another compiler or other flags, not in `keep`) are removed."""
    d = pch_dir(compiler, flags)
    if not d:
        return
    gch = os.path.join(d, "bits", "stdc++.h.gch")
    if os.path.exists(gch):
        return
    os.makedirs(os.path.dirname(gch), exist_ok=True)
    header = os.path.join(d, "pch-source.h")
    open(header, "w").write("#include <bits/stdc++.h>\n")
    tmp = gch + ".%d.tmp" % os.getpid()
    p = subprocess.run([compiler] + flags + ["-x", "c++-header", header, "-o", tmp], env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
    if p.returncode == 0:
        os.replace(tmp, gch)
        import shutil
        root = os.path.dirname(d)
        wanted = {os.path.basename(d)} | {os.path.basename(k) for k in keep if k}
        for old in os.listdir(root):
            if old not in wanted:
                shutil.rmtree(os.path.join(root, old), ignore_errors=True)
    elif os.path.exists(tmp):
        os.remove(tmp)


# Code that needs C++20 (library or language). Everything else is compiled as C++17, whose
# precompiled <bits/stdc++.h> loads in half the time (~0.55 s instead of ~1.25 s with g++ 15).
CPP20 = re.compile(
    rb"<(ranges|format|span|numbers|concepts|compare|bit|source_location|coroutine|print|expected|stop_token)>"
    rb"|\b(ranges|views)::|std::(format|span|numbers|print|jthread|erase_if|to_array|cmp_\w+|in_range|ssize)\b"
    rb"|<=>|\b(concept|requires|consteval|constinit|co_await|co_yield|co_return|char8_t)\b|\[\[(un)?likely\]\]"
    rb"|\b(popcount|bit_width|bit_ceil|bit_floor|countl_zero|countl_one|countr_zero|countr_one|has_single_bit|midpoint|lerp)\s*\("
    rb"|\.(contains|starts_with|ends_with)\s*\(")
# A C++17 compile failed with one of these: try C++20 before showing the errors. Only errors about
# the standard library or the standard itself: a plain typo ("'x' was not declared") is shown at once.
NEEDS_NEWER = re.compile(rb"is not a member of '?std|in namespace 'std' does not name|'std::\w+' has not been declared"
                         rb"|[cC]\+\+(20|2a)|-std=c\+\+2|-std=gnu\+\+2|only available with")


def cpp_flags(compiler, newest=False):
    """-O2 -DONLINE_JUDGE and the standard: C++17, or the newest this g++ has (up to C++20) when asked."""
    flags = ["-O2", "-DONLINE_JUDGE"]
    std = tools.cpp_std(compiler)
    if std == "gnu++20" and not newest:
        std = "gnu++17"
    if std:
        flags.append("-std=" + std)
    return flags


def javac_cds(javac):
    """JDK 19+: javac starts from a class-data archive it creates itself on first use (~40% faster)."""
    home = os.path.dirname(os.path.dirname(javac))
    try:
        if not tools.is_jdk(home) or tools.java_major(tools.jdk_version(home)) < 19:
            return []
        stamp = f"{os.path.normcase(javac)}|{os.path.getmtime(javac)}"
    except (OSError, ValueError):
        return []
    os.makedirs(RUN_ROOT, exist_ok=True)
    jsa = os.path.join(RUN_ROOT, "javac-" + sha1(stamp.encode("utf-8")).hexdigest()[:12] + ".jsa")
    return ["-J-XX:+AutoCreateSharedArchive", "-J-XX:SharedArchiveFile=" + jsa, "-J-Xlog:disable"]


def warmup():
    """Started in the background when Notepad++ starts: prepare the precompiled C++ headers (C++17,
    then C++20) and javac's class-data archive, so the first Run is as quick as the next ones."""
    gpp = tools.find_gpp()
    if gpp:
        env = dict(os.environ, PATH=tools.path_with(os.path.dirname(gpp)))
        f17, f20 = cpp_flags(gpp), cpp_flags(gpp, newest=True)
        keep = (pch_dir(gpp, f17), pch_dir(gpp, f20))
        build_pch(gpp, f17, env, keep)
        if f20 != f17:
            build_pch(gpp, f20, env, keep)
    javac, _ = tools.find_java_tools()
    cds = javac_cds(javac) if javac else []
    jsa = cds[1].split("=", 1)[1] if cds else None
    if jsa and not os.path.exists(jsa):
        d = os.path.join(RUN_ROOT, "warm")
        os.makedirs(d, exist_ok=True)
        open(os.path.join(d, "Warm.java"), "w").write("public class Warm { public static void main(String[] a) { } }\n")
        subprocess.run([javac] + JAVAC_FAST + cds + ["-d", d, "Warm.java"], cwd=d, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)


def compile_quiet(cmd, cwd, env=None):
    """Run a compiler; return (ok, its output)."""
    try:
        p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
                           creationflags=subprocess.CREATE_NO_WINDOW)
    except OSError as e:
        status("error", f"Could not start {cmd[0]} ({e.strerror or e}). {MISSING}")
    return p.returncode == 0, p.stdout or b""


def show(output):
    if output:
        OUT.write(output)
        if not output.endswith(b"\n"):
            OUT.write(b"\n")
        OUT.flush()


def run_quiet(cmd, cwd, env=None):
    """Run a compiler; show its output; return True on success."""
    ok, output = compile_quiet(cmd, cwd, env)
    show(output)
    return ok


def java_main_class(src):
    text = open(src, encoding="utf-8", errors="replace").read()
    m = re.search(r"^\s*package\s+([\w.]+)\s*;", text, re.M)
    stem = os.path.splitext(os.path.basename(src))[0]
    return f"{m.group(1)}.{stem}" if m else stem


def prepare(src):
    """Compile if needed; return the command that runs the program and its environment.
    The compilers are looked up on this PC each time (see tools.py)."""
    ext = os.path.splitext(src)[1].lower()
    cwd = os.path.dirname(src)
    name = os.path.basename(src)
    out = build_dir(src)
    if ext in (".c", ".cpp", ".cc", ".cxx"):
        compiler = tools.find_gcc() if ext == ".c" else tools.find_gpp()
        if not compiler:
            status("missing", "gpp No C/C++ compiler (gcc / g++) is installed")   # the Run panel offers to install it
        gpp = tools.find_gpp()
        if gpp and not tools.gpp_ok(gpp):
            say(f"{YELLOW}Note: g++ {tools.gpp_version(gpp)} is old (parts of C++17/20 are missing). "
                f"Plugins > Dev Tools > Check / Install Tools can add a newer one next to it.{RESET}\n")
        env = dict(os.environ, PATH=tools.path_with(os.path.dirname(compiler)))   # its DLLs, for the program too
        exe = os.path.join(out, os.path.splitext(name)[0] + ".exe")
        text = open(src, "rb").read()
        cpp = ext != ".c"

        def args_for(newest):
            flags = cpp_flags(compiler, newest) if cpp else ["-O2", "-DONLINE_JUDGE"]
            if cpp and STDCPP.search(text):
                d = pch_dir(compiler, flags)
                if d and os.path.exists(os.path.join(d, "bits", "stdc++.h.gch")):
                    flags = flags + ["-I", d]          # g++ picks bits/stdc++.h.gch from here
            return flags

        newest = cpp and bool(CPP20.search(text))
        args = args_for(newest)
        key = build_key(src, compiler, args)
        if up_to_date(exe, key):                      # unchanged code: run the last build again
            return [exe], env
        if cpp and not newest:                        # ... also when it was last built as C++20
            key20 = build_key(src, compiler, args_for(True))
            if key20 != key and up_to_date(exe, key20):
                return [exe], env

        def build(args):
            cmd = [compiler, "-Wall", "-fdiagnostics-color=always"] + args + [name, "-o", exe]
            return compile_quiet(cmd + ([] if cpp else ["-lm"]), cwd, env)

        ok, output = build(args)
        plain = re.sub(rb"\x1b\[[0-9;]*[A-Za-z]", b"", output)   # g++'s colours sit between the words
        if not ok and cpp and not newest and NEEDS_NEWER.search(plain):
            args20 = args_for(True)
            if args20[:3] != args[:3]:                # this g++ has a newer standard: try it
                ok20, output20 = build(args20)
                if ok20:
                    ok, output, key = True, output20, build_key(src, compiler, args20)
        show(output)
        if not ok:
            status("compile", "Compilation error")
        mark_built(exe, key)
        return [exe], env
    if ext == ".java":
        javac, java = tools.find_java_tools()
        if not javac:
            status("missing", "java No Java JDK (javac) is installed")
        classes = os.path.join(out, "classes")
        key = build_key(src, javac, ["java"])
        if not up_to_date(classes, key):
            import shutil
            shutil.rmtree(classes, ignore_errors=True)
            if not run_quiet([javac] + JAVAC_FAST + javac_cds(javac) + ["-encoding", "UTF-8", "-d", classes, name], cwd):
                status("compile", "Compilation error")
            mark_built(classes, key)
        return [java, "-Dstdout.encoding=UTF-8", "-Dstderr.encoding=UTF-8",
                "-cp", classes, java_main_class(src)], None
    if ext == ".py":
        return [sys.executable.replace("pythonw.exe", "python.exe"), "-u", "-X", "utf8", name], None
    if ext in (".js", ".mjs", ".cjs"):
        node = tools.find_node()
        if not node:
            status("missing", "node Node.js (to run JavaScript) is not installed")   # the Run panel offers to install it
        prelude = os.path.join(os.path.dirname(os.path.abspath(__file__)), "js-prelude.js")
        if os.path.isfile(prelude):                   # input, next(), nextInt() ... without code in the file
            return [node, "--require", prelude, name], None
        return [node, name], None
    status("error", f"Can't run {ext or 'this'} files (supported: .c .cpp .java .py .js)")


def normalize(text):
    lines = [line.rstrip() for line in text.replace("\r\n", "\n").split("\n")]
    while lines and not lines[-1]:
        lines.pop()
    return lines


def show_diff(expected, got):
    width = min(max([len(x) for x in expected] + [8]), 48)
    say(f"\n{GRAY}{'line':>4}  {'expected'.ljust(width)}  │ your output{RESET}\n")
    for i in range(max(len(expected), len(got))):
        e = expected[i] if i < len(expected) else ""
        g = got[i] if i < len(got) else ""
        shown_e = (e[:width - 1] + "…") if len(e) > width else e
        color = RESET if e == g else RED
        mark = " " if e == g else "≠"
        say(f"{color}{i + 1:>4}{mark} {shown_e.ljust(width)}  │ {g}{RESET}\n")


class Args:
    source = input = expected = None
    timeout = 10.0


def parse_args(argv):
    """SOURCE [--input FILE] [--expected FILE] [--timeout SECONDS] (argparse costs ~20 ms to import)."""
    a = Args()
    i = 0
    while i < len(argv):
        opt = argv[i]
        if opt in ("--input", "--expected", "--timeout") and i + 1 < len(argv):
            value = argv[i + 1]
            if opt == "--timeout":
                try:
                    a.timeout = float(value)
                except ValueError:
                    status("error", f"Bad --timeout value: {value}")
            else:
                setattr(a, opt[2:], value)
            i += 2
            continue
        if a.source is None:
            a.source = opt
        i += 1
    if not a.source:
        status("error", "usage: runner.py SOURCE [--input FILE] [--expected FILE] [--timeout SECONDS]")
    return a


def main():
    if sys.argv[1:] == ["--warmup"]:
        return warmup()
    a = parse_args(sys.argv[1:])
    src = os.path.abspath(a.source)
    if not os.path.isfile(src):
        status("error", f"File not found: {src}")

    cmd, env = prepare(src)
    stdin = open(a.input, "rb") if a.input and os.path.isfile(a.input) else subprocess.DEVNULL
    start = time.perf_counter()
    try:
        p = subprocess.Popen(cmd, cwd=os.path.dirname(src), stdin=stdin, stdout=subprocess.PIPE, env=env,
                             stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    except OSError as e:
        status("error", f"Could not start: {e}")

    timed_out = threading.Event()

    def kill_after_timeout():
        timed_out.set()
        p.kill()

    timer = threading.Timer(a.timeout, kill_after_timeout)
    timer.start()
    captured = bytearray()
    shown = 0
    while True:
        chunk = os.read(p.stdout.fileno(), 65536)
        if not chunk:
            break
        if len(captured) < 8 * MAX_SHOWN:
            captured += chunk
        if shown < MAX_SHOWN:
            part = chunk[:MAX_SHOWN - shown]
            OUT.write(part)
            OUT.flush()
            shown += len(part)
            if shown >= MAX_SHOWN:
                say(f"\n{YELLOW}… output longer than 1 MB, not showing the rest{RESET}\n")
    code = p.wait()
    elapsed = time.perf_counter() - start
    timer.cancel()
    if captured and not captured.endswith(b"\n") and shown < MAX_SHOWN:
        say("\n")

    secs = f"{elapsed:.2f} s"
    if timed_out.is_set():
        status("tle", f"Time limit exceeded ({a.timeout:g} s)")
    if code != 0:
        why = NTSTATUS.get(code)
        status("runtime", f"Runtime error · exit code {code}" + (f" · {why}" if why else "") + f" · {secs}")

    expected = None
    if a.expected and os.path.isfile(a.expected):
        exp_text = open(a.expected, encoding="utf-8", errors="replace").read()
        if exp_text.strip():
            expected = normalize(exp_text)
    if expected is None:
        status("ok", f"Finished in {secs}")
    got = normalize(captured.decode("utf-8", errors="replace"))
    if got == expected:
        status("accepted", f"Accepted · {secs}")
    show_diff(expected, got)
    status("wrong", f"Wrong answer · {secs}")


if __name__ == "__main__":
    try:
        main()
    except SystemExit:
        raise
    except BaseException as e:                       # never a raw traceback in the Run panel
        log = os.path.join(os.environ.get("TEMP", "."), "nppdev-runner-error.log")
        try:
            import traceback
            with open(log, "a", encoding="utf-8") as f:
                f.write(time.strftime("%Y-%m-%d %H:%M:%S ") + " ".join(sys.argv) + "\n" + traceback.format_exc() + "\n")
        except OSError:
            pass
        status("error", f"Run failed: {type(e).__name__}: {e}  (details in {log})")
