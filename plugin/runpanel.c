/*
 * Run panel: docked on the right, OneCompiler / competitive-programming style.
 *   Input            stdin for the program (saved per source file)
 *   Expected output  optional; when filled the run is judged Accepted / Wrong answer
 *   Output           streamed program output, status line, clickable file:line links
 * Input and expected output are stored in Notepad++'s plugin config folder
 * (plugins\config\DevTools\io), never next to the source files.
 * Programs are compiled and run by runner.py in devtoolsDir (started through pyrun.exe).
 */
#include "devtools.h"

#define WM_RUN_OUTPUT (WM_APP + 1)        /* wParam = length, lParam = heap bytes */
#define WM_RUN_DONE   (WM_APP + 2)        /* wParam = exit code */
#define WM_RUN_MISSING (WM_APP + 3)       /* the run needs a tool that isn't installed: offer it */
#define WM_TOOLS_DONE (WM_APP + 4)        /* the install started from here finished; wParam = exit code */
#define IDC_RUN     2001
#define IDC_STOP    2002
#define IDC_CLEAR   2003
#define IDC_HDR_IN  2010              /* owner-drawn section headers */
#define IDC_HDR_EXP 2011
#define IDC_HDR_OUT 2012              /* "OUTPUT" + run status */
#define IDC_TITLE   2013
#define SAVE_TIMER 7001

#define C_BORDER RGB(0x30, 0x36, 0x3D)   /* GitHub dark palette */
#define C_MUTED  RGB(0x8B, 0x94, 0x9E)
#define C_DIM    RGB(0x6E, 0x76, 0x81)
#define C_GREEN  RGB(0x3F, 0xB9, 0x50)
#define C_RED    RGB(0xF8, 0x51, 0x49)
#define C_YELLOW RGB(0xD2, 0x99, 0x22)

enum { ST_DEFAULT, ST_RED, ST_GREEN, ST_YELLOW, ST_CYAN, ST_GRAY, ST_LINK };
enum { K_IDLE, K_RUNNING, K_OK, K_GOOD, K_BAD };

static HWND panel, hRun, hStop, hClear, hTitle, hHdrIn, hHdrExp, hHdrOut, sciIn, sciExp, sciOut;
static HFONT hdrFont;
static RECT boxRect[3];                   /* borders drawn around the three boxes */
static bool registered, shown, userClosed;
static wchar_t curFile[MAX_PATH];         /* file whose input / expected output is loaded */
static wchar_t runDir[MAX_PATH];          /* folder used to resolve relative file:line links */
static wchar_t ioDir[MAX_PATH];
static wchar_t statusText[512] = L"Press Ctrl+Enter to run";
static wchar_t runTitle[MAX_PATH];        /* what the Output box shows, e.g. "sum.cpp" */
static int statusKind = K_IDLE;
static HANDLE hJob, hProc, hRead, hThread;
static bool stopped, rerunWhenDone;
static void (*doneCallback)(DWORD);

/* output parsing state */
static int escState, curStyle;
static char escBuf[32]; static int escLen;
static char pend[8192]; static unsigned char pendSty[8192]; static int pendLen;
static char curLine[4096]; static int curLineLen; static intptr_t curLineStart;
static bool statusSeen;
static char missingTool[16];       /* from "@@STATUS missing <tool> ...": gpp, java, python */

static LRESULT sci(HWND h, UINT m, WPARAM w, LPARAM l) { return SendMessageW(h, m, w, l); }

static bool isCodeFile(const wchar_t *p)
{
    const wchar_t *e = wcsrchr(p, L'.');
    if (!e || wcschr(e, L'\\')) return false;
    static const wchar_t *exts[] = { L".c", L".cpp", L".cc", L".cxx", L".java", L".py", L".js", L".mjs", L".cjs" };
    for (size_t i = 0; i < sizeof exts / sizeof exts[0]; i++) if (_wcsicmp(e, exts[i]) == 0) return true;
    return false;
}

static void setStatus(int kind, const wchar_t *text)
{
    statusKind = kind;
    wcsncpy(statusText, text, 511);
    statusText[511] = 0;
    if (hHdrOut) InvalidateRect(hHdrOut, NULL, TRUE);
}

/* ---------------- per-file input / expected output storage ---------------- */

static void ioPaths(const wchar_t *src, wchar_t *in, wchar_t *exp)
{
    uint64_t h = 1469598103934665603ULL;                  /* FNV-1a of the lowercased path */
    for (const wchar_t *s = src; *s; s++) { h ^= (uint64_t)towlower(*s); h *= 1099511628211ULL; }
    swprintf(in, MAX_PATH, L"%ls\\%016llx.input.txt", ioDir, (unsigned long long)h);
    swprintf(exp, MAX_PATH, L"%ls\\%016llx.expected.txt", ioDir, (unsigned long long)h);
}

static void loadInto(HWND s, const wchar_t *file)
{
    char *buf = NULL;
    HANDLE f = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(f, NULL), got = 0;
        if (size != INVALID_FILE_SIZE && (buf = (char *)HeapAlloc(GetProcessHeap(), 0, size + 1)) != NULL) {
            ReadFile(f, buf, size, &got, NULL);
            buf[got] = 0;
        }
        CloseHandle(f);
    }
    sci(s, SCI_SETTEXT, 0, (LPARAM)(buf ? buf : ""));
    sci(s, SCI_EMPTYUNDOBUFFER, 0, 0);
    sci(s, SCI_SETSAVEPOINT, 0, 0);
    if (buf) HeapFree(GetProcessHeap(), 0, buf);
}

/* Write a box to its file (an empty box removes the file). */
static void saveFrom(HWND s, const wchar_t *file, bool force)
{
    if (!force && !sci(s, SCI_GETMODIFY, 0, 0)) return;
    intptr_t n = sci(s, SCI_GETLENGTH, 0, 0);
    if (n == 0) DeleteFileW(file);
    else {
        char *buf = (char *)HeapAlloc(GetProcessHeap(), 0, n + 1);
        if (!buf) return;
        sci(s, SCI_GETTEXT, n + 1, (LPARAM)buf);
        HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, buf, (DWORD)n, &w, NULL); CloseHandle(f); }
        HeapFree(GetProcessHeap(), 0, buf);
    }
    sci(s, SCI_SETSAVEPOINT, 0, 0);
}

static void saveIO(bool force)
{
    if (!panel || !curFile[0]) return;
    wchar_t in[MAX_PATH], exp[MAX_PATH];
    ioPaths(curFile, in, exp);
    saveFrom(sciIn, in, force);
    saveFrom(sciExp, exp, force);
}

/* A source file was renamed in the folder panel: its saved Input / Expected output move with it. */
void runPanelRenamed(const wchar_t *oldPath, const wchar_t *newPath)
{
    if (!ioDir[0]) {                                        /* panel not created yet: work out the folder */
        wchar_t cfg[MAX_PATH] = L"";
        SendMessageW(npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)cfg);
        swprintf(ioDir, MAX_PATH, L"%ls\\DevTools\\io", cfg);
    }
    if (panel && _wcsicmp(curFile, oldPath) == 0) { saveIO(false); wcscpy(curFile, newPath); }
    wchar_t oi[MAX_PATH], oe[MAX_PATH], ni[MAX_PATH], ne[MAX_PATH];
    ioPaths(oldPath, oi, oe);
    ioPaths(newPath, ni, ne);
    MoveFileExW(oi, ni, MOVEFILE_REPLACE_EXISTING);
    MoveFileExW(oe, ne, MOVEFILE_REPLACE_EXISTING);
}

static void loadIO(const wchar_t *src)
{
    if (!panel || _wcsicmp(src, curFile) == 0) return;
    saveIO(false);
    wcscpy(curFile, src);
    wchar_t in[MAX_PATH], exp[MAX_PATH], title[MAX_PATH + 16];
    ioPaths(curFile, in, exp);
    loadInto(sciIn, in);
    loadInto(sciExp, exp);
    swprintf(title, sizeof title / sizeof title[0], L"%ls", leafOf(curFile));
    SetWindowTextW(hTitle, title);
    InvalidateRect(hTitle, NULL, TRUE);
    /* the Output box keeps the last run (its status names the file), so its links stay clickable */
}

/* ---------------- output box: ANSI colors, status line, links ---------------- */

typedef struct { int start, end, line, col; char path[MAX_PATH * 3]; } Link;

static bool digitsAt(const char *s, int n, int i, int *value, int *end)
{
    int v = 0, k = i;
    while (k < n && s[k] >= '0' && s[k] <= '9' && k - i < 9) v = v * 10 + (s[k++] - '0');
    if (k == i) return false;
    *value = v; *end = k;
    return true;
}

/* file:line[:col] (gcc, javac, Java and Node.js stack traces) and  File "x.py", line N  (Python). */
static int findLinks(const char *s, int n, Link *out, int max)
{
    int count = 0;
    static const char *exts[] = { ".c:", ".cpp:", ".cc:", ".cxx:", ".h:", ".hpp:", ".java:", ".py:", ".js:", ".mjs:", ".cjs:" };
    for (int i = 0; i < n && count < max; i++) {
        if (s[i] == 'F' && i + 6 <= n && memcmp(s + i, "File \"", 6) == 0) {      /* Python traceback */
            int p0 = i + 6, q = p0;
            while (q < n && s[q] != '"') q++;
            int line, endd;
            if (q < n && q + 8 < n && memcmp(s + q, "\", line ", 8) == 0 && digitsAt(s, n, q + 8, &line, &endd)
                && q - p0 < (int)sizeof out[0].path) {
                Link *L = &out[count++];
                memcpy(L->path, s + p0, q - p0); L->path[q - p0] = 0;
                L->start = p0; L->end = endd; L->line = line; L->col = 0;
                i = endd;
            }
            continue;
        }
        if (s[i] != '.') continue;
        for (size_t e = 0; e < sizeof exts / sizeof exts[0]; e++) {
            int el = (int)strlen(exts[e]);
            if (i + el > n || memcmp(s + i, exts[e], el) != 0) continue;
            int line, endd, col = 0, endc;
            if (!digitsAt(s, n, i + el, &line, &endd)) break;
            if (endd < n && s[endd] == ':' && digitsAt(s, n, endd + 1, &col, &endc)) endd = endc;
            int p0 = i;
            while (p0 > 0 && !strchr(" \t\"'(<[", s[p0 - 1])) p0--;
            int plen = i + el - 1 - p0;                   /* without the ':' */
            if (plen <= 0 || plen >= (int)sizeof out[0].path) break;
            Link *L = &out[count++];
            memcpy(L->path, s + p0, plen); L->path[plen] = 0;
            L->start = p0; L->end = endd; L->line = line; L->col = col;
            i = endd;
            break;
        }
    }
    return count;
}

static bool resolveLink(const Link *L, wchar_t *full)
{
    wchar_t w[MAX_PATH];
    if (!MultiByteToWideChar(CP_UTF8, 0, L->path, -1, w, MAX_PATH)) return false;
    for (wchar_t *c = w; *c; c++) if (*c == L'/') *c = L'\\';
    if (w[0] && w[1] == L':') wcscpy(full, w);
    else if (swprintf(full, MAX_PATH, L"%ls\\%ls", runDir, w) < 0) return false;
    DWORD a = GetFileAttributesW(full);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void appendStyled(const char *text, const unsigned char *styles, int n)
{
    if (n <= 0) return;
    sci(sciOut, SCI_SETREADONLY, 0, 0);
    intptr_t pos = sci(sciOut, SCI_GETLENGTH, 0, 0);
    sci(sciOut, SCI_APPENDTEXT, n, (LPARAM)text);
    for (int i = 0; i < n; ) {                             /* style runs */
        int j = i;
        while (j < n && styles[j] == styles[i]) j++;
        sci(sciOut, SCI_STARTSTYLING, pos + i, 0);
        sci(sciOut, SCI_SETSTYLING, j - i, styles[i]);
        i = j;
    }
    sci(sciOut, SCI_SETREADONLY, 1, 0);
}

static void flushPending(void)
{
    appendStyled(pend, pendSty, pendLen);
    pendLen = 0;
}

static void statusFromLine(const char *line, int n)
{
    char kind[16] = ""; int k = 0, i = 9;                  /* after "@@STATUS " */
    while (i < n && line[i] != ' ' && k < 15) kind[k++] = line[i++];
    kind[k] = 0;
    if (i < n) i++;
    wchar_t text[512];
    int wl = MultiByteToWideChar(CP_UTF8, 0, line + i, n - i, text, 511);
    text[wl > 0 ? wl : 0] = 0;
    int K = K_BAD;
    wchar_t shown[560 + MAX_PATH];
    const wchar_t *mark = L"\u2717";
    if (!strcmp(kind, "ok")) { K = K_OK; mark = L"\u2713"; }
    else if (!strcmp(kind, "accepted")) { K = K_GOOD; mark = L"\u2713"; }
    else if (!strcmp(kind, "missing")) {                 /* "missing gpp No C/C++ compiler..." */
        int t = 0;
        while (i < n && line[i] != ' ' && t < 15) missingTool[t++] = line[i++];
        missingTool[t] = 0;
        wchar_t *sp = wcschr(text, L' ');
        if (sp) memmove(text, sp + 1, (wcslen(sp + 1) + 1) * sizeof(wchar_t));
    }
    swprintf(shown, sizeof shown / sizeof shown[0], L"%ls %ls   \u00b7   %ls", mark, text, runTitle);
    statusSeen = true;
    setStatus(K, shown);
}

static void endLine(void)
{
    curLine[curLineLen] = 0;
    if (curLineLen >= 9 && memcmp(curLine, "@@STATUS ", 9) == 0) {
        statusFromLine(curLine, curLineLen);
        pendLen = 0;
    } else {
        if (pendLen < (int)sizeof pend) { pend[pendLen] = '\n'; pendSty[pendLen++] = ST_DEFAULT; }
        flushPending();
        Link links[8];
        int nl = findLinks(curLine, curLineLen, links, 8);
        for (int i = 0; i < nl; i++) {
            wchar_t full[MAX_PATH];
            if (!resolveLink(&links[i], full)) continue;
            sci(sciOut, SCI_STARTSTYLING, curLineStart + links[i].start, 0);
            sci(sciOut, SCI_SETSTYLING, links[i].end - links[i].start, ST_LINK);
        }
    }
    curLineLen = 0;
    curLineStart = sci(sciOut, SCI_GETLENGTH, 0, 0);
}

static void applySgr(void)
{
    escBuf[escLen] = 0;
    char *p = escBuf;
    if (!*p) { curStyle = ST_DEFAULT; return; }
    while (*p) {
        int v = atoi(p);
        if (v == 0 || v == 39) curStyle = ST_DEFAULT;
        else if (v == 31 || v == 91) curStyle = ST_RED;
        else if (v == 32 || v == 92) curStyle = ST_GREEN;
        else if (v == 33 || v == 93 || v == 35 || v == 95) curStyle = ST_YELLOW;
        else if (v == 34 || v == 94 || v == 36 || v == 96) curStyle = ST_CYAN;
        else if (v == 90 || v == 37) curStyle = ST_GRAY;
        while (*p && *p != ';') p++;
        if (*p == ';') p++;
    }
}

static void feedOutput(const char *data, int n)
{
    for (int i = 0; i < n; i++) {
        char c = data[i];
        if (escState == 1) { escState = c == '[' ? 2 : 0; escLen = 0; continue; }
        if (escState == 2) {                               /* CSI: parameters, then a final byte */
            if ((c >= '0' && c <= '9') || c == ';') { if (escLen < 30) escBuf[escLen++] = c; continue; }
            if (c == 'm') applySgr();
            escState = 0;                                  /* other CSI (e.g. gcc's erase-line K) ignored */
            continue;
        }
        if (c == 0x1b) { escState = 1; continue; }
        if (c == '\r') continue;
        if (c == '\n') { endLine(); continue; }
        if (curLineLen < (int)sizeof curLine - 1) curLine[curLineLen++] = c;
        if (pendLen == (int)sizeof pend) flushPending();
        pend[pendLen] = c; pendSty[pendLen++] = (unsigned char)curStyle;
    }
    if (pendLen && curLine[0] != '@') flushPending();      /* show partial lines now, hold possible status lines */
    sci(sciOut, SCI_DOCUMENTEND, 0, 0);
}

static void openLinkAt(intptr_t pos)
{
    intptr_t line = sci(sciOut, SCI_LINEFROMPOSITION, pos, 0);
    intptr_t start = sci(sciOut, SCI_POSITIONFROMLINE, line, 0);
    intptr_t len = sci(sciOut, SCI_LINELENGTH, line, 0);
    if (len <= 0 || len > 4000) return;
    char text[4096];
    sci(sciOut, SCI_GETLINE, line, (LPARAM)text);
    Link links[8];
    int nl = findLinks(text, (int)len, links, 8);
    for (int i = 0; i < nl; i++) {
        if (pos - start < links[i].start || pos - start > links[i].end) continue;
        wchar_t full[MAX_PATH];
        if (!resolveLink(&links[i], full)) return;
        SendMessageW(npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)full);
        int which = 0;
        SendMessageW(npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
        HWND ed = which ? npp._scintillaSecondHandle : npp._scintillaMainHandle;
        intptr_t l0 = links[i].line > 0 ? links[i].line - 1 : 0;
        sci(ed, SCI_ENSUREVISIBLE, l0, 0);
        sci(ed, SCI_GOTOLINE, l0, 0);
        if (links[i].col > 0) sci(ed, SCI_GOTOPOS, sci(ed, SCI_FINDCOLUMN, l0, links[i].col - 1), 0);
        sci(ed, SCI_SCROLLCARET, 0, 0);
        SetFocus(ed);
        return;
    }
}

/* ---------------- running a process ---------------- */

static DWORD WINAPI readerThread(LPVOID arg)
{
    (void)arg;
    char buf[8192];
    DWORD got;
    while (ReadFile(hRead, buf, sizeof buf, &got, NULL) && got > 0) {
        char *copy = (char *)HeapAlloc(GetProcessHeap(), 0, got);
        if (!copy) break;
        memcpy(copy, buf, got);
        PostMessageW(panel, WM_RUN_OUTPUT, got, (LPARAM)copy);
    }
    WaitForSingleObject(hProc, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(hProc, &code);
    PostMessageW(panel, WM_RUN_DONE, code, 0);
    return 0;
}

static void ensurePanel(void);

bool runPanelExec(const wchar_t *title, const wchar_t *cmdline, const wchar_t *cwd, void (*onDone)(DWORD))
{
    ensurePanel();
    if (!panel) return false;
    if (hProc) {
        MessageBoxW(npp._nppHandle, L"Something is still running. Press Stop first.", L"Run", MB_ICONINFORMATION);
        return false;
    }
    if (!shown) runPanelShowOutput();
    sci(sciOut, SCI_SETREADONLY, 0, 0);
    sci(sciOut, SCI_CLEARALL, 0, 0);
    sci(sciOut, SCI_SETREADONLY, 1, 0);
    escState = 0; curStyle = ST_DEFAULT; pendLen = 0; curLineLen = 0; curLineStart = 0; statusSeen = false; stopped = false; missingTool[0] = 0;
    wcsncpy(runDir, cwd, MAX_PATH - 1);
    wcsncpy(runTitle, title, MAX_PATH - 1);
    doneCallback = onDone;

    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE hWrite, hNul;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    hNul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    STARTUPINFOW si = { sizeof si };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hNul; si.hStdOutput = hWrite; si.hStdError = hWrite;
    PROCESS_INFORMATION pi;
    wchar_t *cmd = _wcsdup(cmdline);
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, cwd, &si, &pi);
    free(cmd);
    CloseHandle(hWrite);
    CloseHandle(hNul);
    if (!ok) {
        CloseHandle(hRead); hRead = NULL;
        setStatus(K_BAD, L"\u2717 Could not start (is Python installed?)");
        return false;
    }
    hJob = CreateJobObjectW(NULL, NULL);                   /* Stop kills the whole process tree */
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = { 0 };
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &li, sizeof li);
    AssignProcessToJobObject(hJob, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    hProc = pi.hProcess;
    wchar_t st[MAX_PATH + 32];
    swprintf(st, sizeof st / sizeof st[0], L"\u25B6 %ls\u2026", title);
    setStatus(K_RUNNING, st);
    EnableWindow(hStop, TRUE);
    hThread = CreateThread(NULL, 0, readerThread, NULL, 0, NULL);
    return true;
}

static void onDone(DWORD code)
{
    flushPending();
    if (hThread) { WaitForSingleObject(hThread, 2000); CloseHandle(hThread); hThread = NULL; }
    if (hRead) { CloseHandle(hRead); hRead = NULL; }
    if (hProc) { CloseHandle(hProc); hProc = NULL; }
    if (hJob) { CloseHandle(hJob); hJob = NULL; }
    EnableWindow(hStop, FALSE);
    wchar_t st[96 + MAX_PATH];
    if (stopped) { swprintf(st, sizeof st / sizeof st[0], L"\u25A0 Stopped   \u00B7   %ls", runTitle); setStatus(K_BAD, st); }
    else if (!statusSeen) {
        if (code == 0) { swprintf(st, sizeof st / sizeof st[0], L"\u2713 Done   \u00B7   %ls", runTitle); setStatus(K_GOOD, st); }
        else { swprintf(st, sizeof st / sizeof st[0], L"\u2717 Failed (exit code %lu)   \u00B7   %ls", code, runTitle); setStatus(K_BAD, st); }
    }
    void (*cb)(DWORD) = doneCallback;
    doneCallback = NULL;
    if (cb) cb(code);
    if (rerunWhenDone) { rerunWhenDone = false; runPanelRunCurrent(); }
    else if (missingTool[0] && !stopped) PostMessageW(panel, WM_RUN_MISSING, 0, 0);
}

/* The file needs a tool that isn't installed (or is too old): offer to install it, then run again. */
static void offerInstall(void)
{
    static const struct { const char *key; const wchar_t *what; } tools[] = {
        { "gpp", L"a C / C++ compiler (GCC / G++)" }, { "java", L"a Java JDK" }, { "python", L"Python 3.9 or newer" },
        { "node", L"Node.js (to run JavaScript)" } };
    const wchar_t *what = L"a tool";
    for (size_t i = 0; i < sizeof tools / sizeof tools[0]; i++) if (!strcmp(missingTool, tools[i].key)) what = tools[i].what;
    wchar_t msg[512], key[16];
    swprintf(msg, 512, L"Running %ls needs %ls, which isn't installed on this PC.\n\n"
             L"Install it now? It is downloaded and installed for you (this can take a few minutes), "
             L"then your program runs.", runTitle, what);
    MultiByteToWideChar(CP_UTF8, 0, missingTool, -1, key, 16);
    missingTool[0] = 0;
    if (MessageBoxW(npp._nppHandle, msg, L"NppDev", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    wchar_t args[64];
    swprintf(args, 64, L"-Install %ls", key);
    setStatus(K_RUNNING, L"\u25B6 Installing\u2026 (see the Check / Install Tools window)");
    if (!runToolsWindow(args, panel, WM_TOOLS_DONE)) setStatus(K_BAD, L"\u2717 Could not open Check / Install Tools");
}

void runPanelStop(void)
{
    if (!hJob) return;
    stopped = true;
    TerminateJobObject(hJob, 1);
}

static wchar_t pythonExe[MAX_PATH];

void runPanelRunCurrent(void)
{
    wchar_t src[MAX_PATH] = L"";
    SendMessageW(npp._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH, (LPARAM)src);
    ensurePanel();
    if (!panel) return;
    if (!shown) runPanelShowOutput();
    if (!isCodeFile(src)) {
        setStatus(K_BAD, L"\u2717 Open a .c, .cpp, .java, .py or .js file to run it");
        return;
    }
    if (hProc) { rerunWhenDone = true; runPanelStop(); return; }   /* Ctrl+Enter while running = restart */
    SendMessageW(npp._nppHandle, NPPM_SAVECURRENTFILE, 0, 0);
    loadIO(src);
    saveIO(true);

    /* pyrun.exe finds Python on this PC each time (see pyrun.c); without it, python.exe on PATH */
    wchar_t pyrun[MAX_PATH];
    swprintf(pyrun, MAX_PATH, L"%ls\\pyrun.exe", devtoolsDir);
    if (GetFileAttributesW(pyrun) != INVALID_FILE_ATTRIBUTES) wcscpy(pythonExe, pyrun);
    else if (!pythonExe[0] && !SearchPathW(NULL, L"python.exe", NULL, MAX_PATH, pythonExe, NULL))
        wcscpy(pythonExe, L"python.exe");
    bool viaPyrun = wcscmp(pythonExe, pyrun) == 0;
    wchar_t in[MAX_PATH], exp[MAX_PATH], dir[MAX_PATH], cmd[4 * MAX_PATH + 128];
    ioPaths(src, in, exp);
    wcscpy(dir, src);
    *wcsrchr(dir, L'\\') = 0;
    int n = swprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\"%ls \"%ls\\runner.py\" \"%ls\"",
                     pythonExe, viaPyrun ? L"" : L" -I", devtoolsDir, src);
    if (GetFileAttributesW(in) != INVALID_FILE_ATTRIBUTES) n += swprintf(cmd + n, sizeof cmd / sizeof cmd[0] - n, L" --input \"%ls\"", in);
    if (GetFileAttributesW(exp) != INVALID_FILE_ATTRIBUTES) n += swprintf(cmd + n, sizeof cmd / sizeof cmd[0] - n, L" --expected \"%ls\"", exp);
    runPanelExec(leafOf(src), cmd, dir, NULL);
}

/* ---------------- the panel window ---------------- */

static void styleBox(HWND s, bool readOnly, bool lineNumbers)
{
    char font[128]; int size;
    editorFont(font, sizeof font, &size);
    COLORREF fg = editorFore(), bg = editorBack();
    sci(s, SCI_SETILEXER, 0, 0);
    sci(s, SCI_SETCODEPAGE, 65001, 0);
    sci(s, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)font);
    sci(s, SCI_STYLESETSIZE, STYLE_DEFAULT, size);
    sci(s, SCI_STYLESETFORE, STYLE_DEFAULT, fg);
    sci(s, SCI_STYLESETBACK, STYLE_DEFAULT, bg);
    sci(s, SCI_STYLECLEARALL, 0, 0);
    sci(s, SCI_STYLESETFORE, STYLE_LINENUMBER, RGB(0x6E, 0x76, 0x81));
    sci(s, SCI_STYLESETBACK, STYLE_LINENUMBER, bg);
    sci(s, SCI_STYLESETFORE, ST_RED, RGB(0xF8, 0x51, 0x49));
    sci(s, SCI_STYLESETFORE, ST_GREEN, RGB(0x3F, 0xB9, 0x50));
    sci(s, SCI_STYLESETFORE, ST_YELLOW, RGB(0xD2, 0x99, 0x22));
    sci(s, SCI_STYLESETFORE, ST_CYAN, RGB(0x79, 0xC0, 0xFF));
    sci(s, SCI_STYLESETFORE, ST_GRAY, RGB(0x8B, 0x94, 0x9E));
    sci(s, SCI_STYLESETFORE, ST_LINK, RGB(0x79, 0xC0, 0xFF));
    sci(s, SCI_STYLESETUNDERLINE, ST_LINK, 1);
    sci(s, SCI_STYLESETHOTSPOT, ST_LINK, 1);
    sci(s, SCI_SETCARETFORE, fg, 0);
    sci(s, SCI_SETSELBACK, 1, RGB(0x26, 0x4F, 0x78));
    /* same left margin in all three boxes, so text lines up; numbers only where you type */
    sci(s, SCI_SETMARGINTYPEN, 0, lineNumbers ? SC_MARGIN_NUMBER : 0 /* SC_MARGIN_SYMBOL: blank */);
    sci(s, SCI_SETMARGINWIDTHN, 0, 3 * size + 10);
    sci(s, SCI_SETMARGINWIDTHN, 1, 0);
    sci(s, SCI_SETMARGINWIDTHN, 2, 0);
    sci(s, 2155 /* SCI_SETMARGINLEFT */, 0, 2);
    sci(s, 2274 /* SCI_SETSCROLLWIDTH */, 1, 0);           /* horizontal scrollbar only when needed */
    sci(s, 2516 /* SCI_SETSCROLLWIDTHTRACKING */, 1, 0);
    sci(s, SCI_SETTABWIDTH, 4, 0);
    sci(s, SCI_SETREADONLY, readOnly, 0);
}

static void layout(void)
{
    RECT rc; GetClientRect(panel, &rc);
    const int pad = 8, gap = 8, bh = 26, hh = 24;
    int W = rc.right, H = rc.bottom, inner = W - 2 * pad;
    if (inner < 80) inner = 80;

    /* all eleven controls move in one step (DeferWindowPos), so a fast drag of the panel's edge
       doesn't leave half-moved controls behind */
    HDWP dw = BeginDeferWindowPos(11);
    const UINT f = SWP_NOZORDER | SWP_NOACTIVATE;
#define PLACE(hw, X, Y, CX, CY) (dw = dw ? DeferWindowPos(dw, (hw), NULL, (X), (Y), (CX), (CY), f) : NULL)

    /* toolbar row: Run Stop Clear | file name */
    int x = pad;
    PLACE(hRun, x, pad, 70, bh);   x += 70 + 6;
    PLACE(hStop, x, pad, 64, bh);  x += 64 + 6;
    PLACE(hClear, x, pad, 58, bh); x += 58 + 10;
    PLACE(hTitle, x, pad, W - pad - x > 20 ? W - pad - x : 20, bh);

    /* three sections: header, bordered box */
    int y = pad + bh + 10;
    int avail = H - y - 3 * hh - 2 * gap - pad;
    if (avail < 120) avail = 120;
    int h[3] = { avail * 27 / 100, avail * 18 / 100, 0 };
    h[2] = avail - h[0] - h[1];
    HWND hdr[3] = { hHdrIn, hHdrExp, hHdrOut }, box[3] = { sciIn, sciExp, sciOut };
    for (int i = 0; i < 3; i++) {
        PLACE(hdr[i], pad, y, inner, hh);
        y += hh;
        SetRect(&boxRect[i], pad, y, pad + inner, y + h[i]);
        PLACE(box[i], pad + 1, y + 1, inner - 2, h[i] - 2);
        y += h[i] + gap;
    }
#undef PLACE
    if (dw) EndDeferWindowPos(dw);
    RedrawWindow(panel, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);   /* box borders, headers */
}

/* Background Notepad++ paints behind panels (dark mode: its "pure background"). */
static COLORREF panelBack(void)
{
    if (!SendMessageW(npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0)) return GetSysColor(COLOR_3DFACE);
    COLORREF c[12] = { 0 };                                  /* NppDarkMode::Colors */
    if (SendMessageW(npp._nppHandle, NPPMSG + 108 /* NPPM_GETDARKMODECOLORS */, sizeof c, (LPARAM)c)) return c[3];
    return RGB(0x01, 0x04, 0x09);
}

/* Section header: bold muted title, dim hint; the Output header also shows the run status. */
static void drawHeader(DRAWITEMSTRUCT *d)
{
    HBRUSH bg = CreateSolidBrush(panelBack());
    FillRect(d->hDC, &d->rcItem, bg);
    DeleteObject(bg);
    SetBkMode(d->hDC, TRANSPARENT);
    RECT r = d->rcItem;
    r.left += 2;
    const wchar_t *title = d->CtlID == IDC_HDR_IN ? L"INPUT" : d->CtlID == IDC_HDR_EXP ? L"EXPECTED OUTPUT" : L"OUTPUT";
    const wchar_t *hint = d->CtlID == IDC_HDR_IN ? L"stdin for your program"
                        : d->CtlID == IDC_HDR_EXP ? L"optional · fill it to check your answer" : NULL;
    HFONT old = SelectObject(d->hDC, hdrFont);
    SetTextColor(d->hDC, C_MUTED);
    DrawTextW(d->hDC, title, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SIZE sz; GetTextExtentPoint32W(d->hDC, title, (int)wcslen(title), &sz);
    SelectObject(d->hDC, (HFONT)SendMessageW(panel, WM_GETFONT, 0, 0));
    r.left += sz.cx + 12;
    if (hint) {
        SetTextColor(d->hDC, C_DIM);
        DrawTextW(d->hDC, hint, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        SetTextColor(d->hDC, statusKind == K_GOOD ? C_GREEN : statusKind == K_BAD ? C_RED
                           : statusKind == K_RUNNING ? C_YELLOW : C_DIM);
        DrawTextW(d->hDC, statusText, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(d->hDC, old);
}

static void drawTitle(DRAWITEMSTRUCT *d)
{
    HBRUSH bg = CreateSolidBrush(panelBack());
    FillRect(d->hDC, &d->rcItem, bg);
    DeleteObject(bg);
    wchar_t text[MAX_PATH];
    GetWindowTextW(d->hwndItem, text, MAX_PATH);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, editorFore());
    HFONT old = SelectObject(d->hDC, hdrFont);
    DrawTextW(d->hDC, text, -1, &d->rcItem, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(d->hDC, old);
}

static HWND makeControl(const wchar_t *cls, const wchar_t *text, DWORD style, int id, HFONT font)
{
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, panel, (HMENU)(INT_PTR)id, hInst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
    return h;
}

static INT_PTR CALLBACK panelDlgProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_INITDIALOG: {
        panel = h;
        SetWindowLongPtrW(h, GWL_STYLE, GetWindowLongPtrW(h, GWL_STYLE) | WS_CLIPCHILDREN);   /* no erasing over the boxes */
        HFONT font = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
        LOGFONTW lf; GetObjectW(font, sizeof lf, &lf);
        lf.lfWeight = FW_SEMIBOLD;
        hdrFont = CreateFontIndirectW(&lf);
        hRun = makeControl(L"BUTTON", L"\u25B6  Run", BS_PUSHBUTTON, IDC_RUN, font);
        hStop = makeControl(L"BUTTON", L"\u25A0  Stop", BS_PUSHBUTTON | WS_DISABLED, IDC_STOP, font);
        hClear = makeControl(L"BUTTON", L"Clear", BS_PUSHBUTTON, IDC_CLEAR, font);
        hTitle = makeControl(L"STATIC", L"", SS_OWNERDRAW, IDC_TITLE, font);
        hHdrIn = makeControl(L"STATIC", L"", SS_OWNERDRAW, IDC_HDR_IN, font);
        hHdrExp = makeControl(L"STATIC", L"", SS_OWNERDRAW, IDC_HDR_EXP, font);
        hHdrOut = makeControl(L"STATIC", L"", SS_OWNERDRAW, IDC_HDR_OUT, font);
        sciIn = (HWND)SendMessageW(npp._nppHandle, NPPM_CREATESCINTILLAHANDLE, 0, (LPARAM)h);
        sciExp = (HWND)SendMessageW(npp._nppHandle, NPPM_CREATESCINTILLAHANDLE, 0, (LPARAM)h);
        sciOut = (HWND)SendMessageW(npp._nppHandle, NPPM_CREATESCINTILLAHANDLE, 0, (LPARAM)h);
        styleBox(sciIn, false, true);
        styleBox(sciExp, false, true);
        styleBox(sciOut, true, false);
        ShowWindow(sciIn, SW_SHOW);                         /* Notepad++ creates these hidden */
        ShowWindow(sciExp, SW_SHOW);
        ShowWindow(sciOut, SW_SHOW);
        SendMessageW(npp._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME, DMF_INIT, (LPARAM)h);
        SetTimer(h, SAVE_TIMER, 5000, NULL);
        layout();
        return FALSE;
    }
    case WM_SIZE:
        if (sciOut) layout();
        return FALSE;
    case WM_PAINT: {                                        /* 1-px borders around the boxes */
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        HBRUSH b = CreateSolidBrush(C_BORDER);
        for (int i = 0; i < 3; i++) FrameRect(dc, &boxRect[i], b);
        DeleteObject(b);
        EndPaint(h, &ps);
        return TRUE;
    }
    case WM_TIMER:
        if (w == SAVE_TIMER) saveIO(false);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == IDC_RUN) runPanelRunCurrent();
        else if (LOWORD(w) == IDC_STOP) runPanelStop();
        else if (LOWORD(w) == IDC_CLEAR) {
            sci(sciOut, SCI_SETREADONLY, 0, 0);
            sci(sciOut, SCI_CLEARALL, 0, 0);
            sci(sciOut, SCI_SETREADONLY, 1, 0);
            if (statusKind != K_RUNNING) setStatus(K_IDLE, L"Press Ctrl+Enter to run");
        }
        return TRUE;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)l;
        if (d->CtlID == IDC_TITLE) { drawTitle(d); return TRUE; }
        if (d->CtlID >= IDC_HDR_IN && d->CtlID <= IDC_HDR_OUT) { drawHeader(d); return TRUE; }
        return FALSE;
    }
    case WM_RUN_OUTPUT:
        feedOutput((const char *)l, (int)w);
        HeapFree(GetProcessHeap(), 0, (void *)l);
        return TRUE;
    case WM_RUN_DONE:
        onDone((DWORD)w);
        return TRUE;
    case WM_RUN_MISSING:
        offerInstall();
        return TRUE;
    case WM_TOOLS_DONE:                                    /* installed: run the file again */
        if (w == 0) runPanelRunCurrent();
        else setStatus(K_BAD, L"\u2717 The tool wasn't installed (see the Check / Install Tools window)");
        return TRUE;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)l;
        if (nh->hwndFrom == sciOut && nh->code == SCN_HOTSPOTCLICK) { openLinkAt(((SciNotification *)l)->position); return TRUE; }
        if (nh->code == DMN_CLOSE) {                         /* the panel's X button */
            shown = false; userClosed = true;
            SendMessageW(npp._nppHandle, NPPM_SETMENUITEMCHECK, funcCmdId(runPanelFuncIndex), FALSE);
            return TRUE;
        }
        return FALSE;
    }
    }
    return FALSE;
}

static void ensurePanel(void)
{
    if (panel) return;
    wchar_t cfg[MAX_PATH] = L"";
    SendMessageW(npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)cfg);
    swprintf(ioDir, MAX_PATH, L"%ls\\DevTools\\io", cfg);
    SHCreateDirectoryExW(NULL, ioDir, NULL);
    static WORD tmpl[256];
    memset(tmpl, 0, sizeof tmpl);
    dlgBegin(tmpl, WS_CHILD | WS_CLIPCHILDREN | DS_CONTROL, 200, 300, 0, L"Run");
    CreateDialogIndirectParamW(hInst, (DLGTEMPLATE *)dlgAlign(tmpl), npp._nppHandle, panelDlgProc, 0);
}

void runPanelShowOutput(void)
{
    ensurePanel();
    if (!panel) return;
    if (!registered) {
        static tTbData tb;
        memset(&tb, 0, sizeof tb);
        tb.hClient = panel;
        tb.pszName = L"Run";
        tb.dlgID = runPanelFuncIndex;
        tb.uMask = DWS_DF_CONT_RIGHT;
        tb.pszModuleName = L"RecentFolders.dll";
        SendMessageW(npp._nppHandle, NPPM_DMMREGASDCKDLG, 0, (LPARAM)&tb);
        registered = true;
    }
    SendMessageW(npp._nppHandle, NPPM_DMMSHOW, 0, (LPARAM)panel);
    SendMessageW(npp._nppHandle, NPPM_SETMENUITEMCHECK, funcCmdId(runPanelFuncIndex), TRUE);
    shown = true;
    wchar_t src[MAX_PATH] = L"";
    SendMessageW(npp._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH, (LPARAM)src);
    if (isCodeFile(src)) loadIO(src);
}

void runPanelToggle(void)
{
    if (shown) {
        SendMessageW(npp._nppHandle, NPPM_DMMHIDE, 0, (LPARAM)panel);
        SendMessageW(npp._nppHandle, NPPM_SETMENUITEMCHECK, funcCmdId(runPanelFuncIndex), FALSE);
        shown = false;
        userClosed = true;
    } else {
        userClosed = false;
        runPanelShowOutput();
    }
}

void runPanelOnNotify(SciNotifyHeader *n)
{
    if (n->hwndFrom != npp._nppHandle) return;
    if (n->code == NPPN_BUFFERACTIVATED) {
        wchar_t src[MAX_PATH] = L"";
        SendMessageW(npp._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH, (LPARAM)src);
        if (!isCodeFile(src)) return;
        if (!shown && !userClosed) runPanelShowOutput();    /* opens by default for code files */
        else if (panel) loadIO(src);
    } else if ((n->code == NPPN_DARKMODECHANGED || n->code == NPPN_WORDSTYLESUPDATED) && panel) {
        styleBox(sciIn, false, true);                           /* the editor's new colours and font */
        styleBox(sciExp, false, true);
        styleBox(sciOut, true, false);
        SendMessageW(npp._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME, 0x0000000C /* dmfHandleChange */, (LPARAM)panel);
        RedrawWindow(panel, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    } else if (n->code == NPPN_SHUTDOWN) {
        saveIO(false);
        if (hJob) TerminateJobObject(hJob, 1);
    }
}
