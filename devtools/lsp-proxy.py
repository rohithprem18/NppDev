"""Stdio LSP proxy for NppLspClient.

NppLspClient 0.0.40 fails its "initialized" step with
  "Feature diagnostic_provider is not supported by this ... language server"
because clangd, pylsp and jdtls only *push* diagnostics (publishDiagnostics)
and don't advertise pull diagnostics. This proxy sits between the plugin and
the real server and
  - adds a diagnosticProvider capability to the initialize result, and
  - answers textDocument/diagnostic pulls with the diagnostics the server
    last pushed for that file (workspace/diagnostic gets an empty report).
Everything else is passed through unchanged.

Usage (run with python -I):  lsp-proxy.py <c|cpp|python|java>
The servers are found on this PC when the proxy starts (see tools.py), so paths with
spaces never have to go through the plugin's argument string. When a server isn't
installed, the proxy answers as an empty server, so the editor shows no errors about it.
"""
import ctypes
import json
import os
import re
import subprocess
import sys
import threading
from urllib.parse import unquote

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # -I leaves this folder out
import tools  # noqa: E402

LANGS = ("c", "cpp", "python", "java", "javascript")


def clangd_major(clangd):
    """clangd's major version (remembered per build of clangd); 0 when unknown."""
    try:
        stamp = "clangd|%s|%d" % (os.path.normcase(clangd), int(os.path.getmtime(clangd)))
    except OSError:
        return 0

    def ask():
        m = re.search(r"version\s+(\d+)", tools.run_text([clangd, "--version"]))
        return int(m.group(1)) if m else 0
    return tools.cached(stamp, ask)


def server_command(lang):
    """Command line of the real language server, or None when it isn't installed."""
    if lang in ("c", "cpp"):
        clangd = tools.find_clangd()
        if not clangd:
            return None
        # --pch-storage=memory: the parsed headers (<bits/stdc++.h>) stay in memory, so completions
        # and errors come back sooner after each edit; background indexing yields to the editor
        cmd = [clangd, "--offset-encoding=utf-8", "--header-insertion=never", "--pch-storage=memory"]
        if clangd_major(clangd) >= 15:
            cmd.append("--background-index-priority=low")
        compiler = tools.find_gcc() if lang == "c" else tools.find_gpp()
        if compiler:                                    # lets clangd find <bits/stdc++.h> and the other headers
            cmd.append("--query-driver=" + os.path.join(os.path.dirname(compiler), "*").replace("\\", "/"))
        return cmd
    if lang == "python":
        return [sys.executable, "-m", "pylsp", "--check-parent-process"] if tools.have_pylsp() else None
    if lang == "java":
        home = tools.find_java_lsp_home()
        jdtls, launcher = tools.find_jdtls()
        if not home or not jdtls:
            return None
        root = os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "NppDev")
        data = os.path.join(root, "jdtls-workspace")
        os.makedirs(root, exist_ok=True)
        # VS Code's JVM settings for jdtls, plus a class-data archive the JVM creates on first
        # start (JDK 19+; jdtls needs 21): later starts are quicker
        return [os.path.join(home, "bin", "java.exe"),
                "-Declipse.application=org.eclipse.jdt.ls.core.id1",
                "-Dosgi.bundles.defaultStartLevel=4",
                "-Declipse.product=org.eclipse.jdt.ls.core.product",
                "-XX:+UseParallelGC", "-XX:GCTimeRatio=4", "-XX:AdaptiveSizePolicyWeight=90",
                "-Dsun.zip.disableMemoryMapping=true", "-Xms100m",
                "-XX:+AutoCreateSharedArchive", "-XX:SharedArchiveFile=" + os.path.join(root, "jdtls.jsa"), "-Xlog:disable",
                "-Xmx1G", "--add-modules=ALL-SYSTEM",
                "--add-opens", "java.base/java.util=ALL-UNNAMED",
                "--add-opens", "java.base/java.lang=ALL-UNNAMED",
                "-jar", launcher,
                "-configuration", os.path.join(jdtls, "config_win"),
                "-data", data]
    if lang == "javascript":
        cmd, _ = tools.find_js_lsp()
        return cmd
    return None


def javascript_init(params):
    """typescript-language-server (older TypeScript) needs to know where tsserver is (the global npm one).
    TypeScript 7's own server needs nothing."""
    _, tsserver = tools.find_js_lsp()
    if tsserver:
        opts = params.setdefault("initializationOptions", None) or {}
        opts.setdefault("tsserver", {}).setdefault("path", tsserver)
        params["initializationOptions"] = opts

DIAGNOSTIC_PROVIDER = {"interFileDependencies": False, "workspaceDiagnostics": False}


def uri_key(uri):
    """Servers and the plugin spell file URIs differently (c%3A vs C:)."""
    return unquote(uri or "").replace("\\", "/").lower()


def read_message(stream):
    """Read one Content-Length framed JSON-RPC message, or None at EOF."""
    length = None
    while True:
        line = stream.readline()
        if not line:
            return None
        line = line.strip()
        if not line:
            if length is not None:
                break
            continue
        name, _, value = line.decode("ascii", "replace").partition(":")
        if name.strip().lower() == "content-length":
            length = int(value.strip())
    body = stream.read(length)
    if len(body) < length:
        return None
    return json.loads(body)


def write_message(stream, lock, message):
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    with lock:
        stream.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        stream.flush()


def kill_with_proxy(proc):
    """Put the server in a job object so it dies when this proxy exits."""
    k32 = ctypes.windll.kernel32
    job = k32.CreateJobObjectW(None, None)
    if not job:
        return

    class Limits(ctypes.Structure):
        _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                    ("LimitFlags", ctypes.c_uint32), ("MinimumWorkingSetSize", ctypes.c_size_t),
                    ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", ctypes.c_uint32),
                    ("Affinity", ctypes.c_size_t), ("PriorityClass", ctypes.c_uint32),
                    ("SchedulingClass", ctypes.c_uint32),
                    ("IoCounters", ctypes.c_uint64 * 6),
                    ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                    ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]

    limits = Limits()
    limits.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    k32.SetInformationJobObject(ctypes.c_void_p(job), 9, ctypes.byref(limits), ctypes.sizeof(limits))  # ExtendedLimitInformation
    k32.AssignProcessToJobObject(ctypes.c_void_p(job), ctypes.c_void_p(int(proc._handle)))
    kill_with_proxy.job = job  # keep the handle open for the proxy's lifetime


def empty_server(client_in, client_out):
    """Stand-in for a language server that isn't installed: says it can do nothing, politely."""
    lock = threading.Lock()
    while True:
        msg = read_message(client_in)
        if msg is None or msg.get("method") == "exit":
            return 0
        if "id" not in msg or "method" not in msg:
            continue                                    # notifications and replies need no answer
        method = msg["method"]
        if method == "initialize":
            result = {"capabilities": {"diagnosticProvider": DIAGNOSTIC_PROVIDER}, "serverInfo": {"name": "not installed"}}
        elif method == "textDocument/diagnostic":
            result = {"kind": "full", "items": []}
        elif method == "workspace/diagnostic":
            result = {"items": []}
        else:
            result = None
        write_message(client_out, lock, {"jsonrpc": "2.0", "id": msg["id"], "result": result})


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in LANGS:
        sys.stderr.write("usage: lsp-proxy.py <%s>\n" % "|".join(LANGS))
        return 2

    client_in, client_out = sys.stdin.buffer, sys.stdout.buffer
    try:
        cmd = server_command(sys.argv[1])
    except Exception:                                 # an odd install must not break the editor
        cmd = None
    try:
        server = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=None,
                                  stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW) if cmd else None
    except OSError:
        server = None
    if not server:
        return empty_server(client_in, client_out)
    kill_with_proxy(server)

    client_lock, server_lock = threading.Lock(), threading.Lock()
    initialize_ids = set()
    pushed = {}  # uri -> last published diagnostics

    def client_to_server():
        while True:
            msg = read_message(client_in)
            if msg is None:
                break
            method = msg.get("method")
            if method == "initialize":
                initialize_ids.add(msg.get("id"))
                if sys.argv[1] == "javascript" and isinstance(msg.get("params"), dict):
                    javascript_init(msg["params"])
            elif method == "textDocument/diagnostic" and "id" in msg:
                uri = msg.get("params", {}).get("textDocument", {}).get("uri")
                write_message(client_out, client_lock, {"jsonrpc": "2.0", "id": msg["id"],
                                                        "result": {"kind": "full", "items": pushed.get(uri_key(uri), [])}})
                continue
            elif method == "workspace/diagnostic" and "id" in msg:
                write_message(client_out, client_lock, {"jsonrpc": "2.0", "id": msg["id"], "result": {"items": []}})
                continue
            try:
                write_message(server.stdin, server_lock, msg)
            except OSError:
                break
        try:
            server.stdin.close()
        except OSError:
            pass

    threading.Thread(target=client_to_server, daemon=True).start()

    while True:
        msg = read_message(server.stdout)
        if msg is None:
            break
        if msg.get("id") in initialize_ids and isinstance(msg.get("result"), dict):
            caps = msg["result"].setdefault("capabilities", {})
            caps.setdefault("diagnosticProvider", DIAGNOSTIC_PROVIDER)
        elif msg.get("method") == "textDocument/publishDiagnostics":
            params = msg.get("params", {})
            pushed[uri_key(params.get("uri"))] = params.get("diagnostics", [])
        try:
            write_message(client_out, client_lock, msg)
        except OSError:
            break
    server.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
