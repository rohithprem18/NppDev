/*
 * pyrun.exe <script.py> [args...]   run a script with the Python found on this PC: python -I script args
 * pyrun.exe --lsp <language>        run lsp-proxy.py (next to this exe) for a language: the LSP plugin's
 *                                   settings then need no path (it mangles quoted paths in arguments)
 * pyrun.exe --where                 print that python.exe (nothing if there is none)
 *
 * Used by the Run panel and the language-server settings so that Python can be installed, moved
 * or upgraded without any setting changing. Python is, in this order:
 *   - [paths] python = ... in tools.ini next to this exe (picked in Check / Install Tools)
 *   - the first python.exe 3.9+ on PATH (the Microsoft Store stub is skipped)
 *   - the newest 3.9+ in the usual install folders
 * Built as a GUI program, so no console window appears; standard input/output are passed through.
 *
 *   gcc -O2 -municode -mwindows -o ..\pyrun.exe pyrun.c -lversion -lshlwapi
 */
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <wchar.h>

static int versionOf(const wchar_t *exe)          /* 313 for 3.13; 0 if unknown */
{
    DWORD h, n = GetFileVersionInfoSizeW(exe, &h);
    if (!n) return 0;
    void *buf = HeapAlloc(GetProcessHeap(), 0, n);
    VS_FIXEDFILEINFO *fi; UINT len;
    int v = 0;
    if (buf && GetFileVersionInfoW(exe, 0, n, buf) && VerQueryValueW(buf, L"\\", (void **)&fi, &len))
        v = HIWORD(fi->dwFileVersionMS) * 100 + LOWORD(fi->dwFileVersionMS);
    if (buf) HeapFree(GetProcessHeap(), 0, buf);
    return v;
}

static bool usable(const wchar_t *exe) { return GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES && versionOf(exe) >= 309; }

/* Newest pythonXY folder under base matching the pattern (e.g. "Python3*"). */
static void newestIn(const wchar_t *base, const wchar_t *pattern, wchar_t *best, int *bestVer)
{
    wchar_t pat[MAX_PATH], exe[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\%ls", base, pattern);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        swprintf(exe, MAX_PATH, L"%ls\\%ls\\python.exe", base, fd.cFileName);
        int v = usable(exe) ? versionOf(exe) : 0;
        if (v > *bestVer) { *bestVer = v; wcscpy(best, exe); }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

static bool findPython(wchar_t *out)
{
    wchar_t self[MAX_PATH], ini[MAX_PATH], env[32767];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    PathRemoveFileSpecW(self);
    swprintf(ini, MAX_PATH, L"%ls\\tools.ini", self);
    if (GetPrivateProfileStringW(L"paths", L"python", L"", out, MAX_PATH, ini) && usable(out)) return true;

    if (GetEnvironmentVariableW(L"PATH", env, 32767)) {
        for (wchar_t *next, *d = wcstok(env, L";", &next); d; d = wcstok(NULL, L";", &next)) {
            size_t n = wcslen(d);
            while (n > 3 && d[n - 1] == L'\\') d[--n] = 0;
            if (!*d || StrStrIW(d, L"WindowsApps")) continue;
            swprintf(out, MAX_PATH, L"%ls\\python.exe", d);
            if (usable(out)) return true;
        }
    }
    int best = 0;
    out[0] = 0;
    wchar_t base[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH)) {
        wcscat(base, L"\\Programs\\Python");
        newestIn(base, L"Python3*", out, &best);
    }
    newestIn(L"C:", L"Python3*", out, &best);
    if (GetEnvironmentVariableW(L"ProgramFiles", base, MAX_PATH)) newestIn(base, L"Python3*", out, &best);
    return best > 0;
}

static void say(const char *text)
{
    DWORD put;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, text, (DWORD)strlen(text), &put, NULL);
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t python[MAX_PATH];
    if (argc < 2) return 2;
    if (wcscmp(argv[1], L"--where") == 0) {
        if (!findPython(python)) return 1;
        char utf8[3 * MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, python, -1, utf8, sizeof utf8, NULL, NULL);
        say(utf8); say("\n");
        return 0;
    }
    if (!findPython(python)) {
        if (StrStrIW(argv[1], L"runner.py"))      /* the Run panel offers to install it; a language server just stops */
            say("@@STATUS missing python Python 3.9 or newer is not installed\n");
        return 9009;
    }
    /* everything after our own name, exactly as given */
    const wchar_t *rest = GetCommandLineW();
    if (*rest == L'"') { rest = wcschr(rest + 1, L'"'); rest = rest ? rest + 1 : L""; }
    else while (*rest && *rest != L' ' && *rest != L'\t') rest++;
    wchar_t script[MAX_PATH + 2] = L"";
    if (wcscmp(argv[1], L"--lsp") == 0) {                    /* --lsp <language> */
        GetModuleFileNameW(NULL, script, MAX_PATH);
        PathRemoveFileSpecW(script);
        wcscat(script, L"\\lsp-proxy.py");
        rest = wcsstr(rest, L"--lsp") + 5;
    }
    size_t len = wcslen(python) + wcslen(script) + wcslen(rest) + 32;
    wchar_t *cmd = HeapAlloc(GetProcessHeap(), 0, len * sizeof(wchar_t));
    if (script[0]) swprintf(cmd, len, L"\"%ls\" -I \"%ls\"%ls", python, script, rest);
    else swprintf(cmd, len, L"\"%ls\" -I%ls", python, rest);

    STARTUPINFOW si = { sizeof si };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return 9009;
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}
