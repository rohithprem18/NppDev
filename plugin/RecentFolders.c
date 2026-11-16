/*
 * Dev Tools - Notepad++ plugin (RecentFolders.dll)
 *  - "File > Open Recent Folder" (VS Code style), switching folders in place with each
 *    folder's own open files; the built-in folder picker (Ctrl+Shift+R).
 *  - Works both from a hand-made setup (helpers in %USERPROFILE%\NppRunner) and as part of the
 *    NppDev bundle (a portable Notepad++ with the helpers in <app>\devtools, see installer\).
 *  - Folder as Workspace panel: New File / New Folder buttons, right-click
 *    New File / New Folder / Delete.
 *  - Run panel (runpanel.c), Quick Open and Command Palette (picker.c).
 *  - Removes the WebView cache folders NppLspClient leaves behind (it asks about them at every start).
 *
 * Build (MSYS2 ucrt64):
 *   gcc -O2 -shared -municode -o RecentFolders.dll RecentFolders.c runpanel.c picker.c
 *       -luser32 -lgdi32 -lcomctl32 -lshell32 -lole32 -luuid -static-libgcc
 */
#include "devtools.h"

#define IDM_FILE_EXIT       41011
#define IDM_FILE_OPENFOLDERASWORKSPACE 41022
#define IDM_FILE_OPEN_DEFAULT_VIEWER   41023

#define MAX_SLOTS 10
#define FN_MORE      MAX_SLOTS          /* "More Folders..." */
#define FN_NEW       (MAX_SLOTS + 1)    /* "New File in Folder..." */
#define FN_RUN       (MAX_SLOTS + 2)    /* Ctrl+Enter */
#define FN_STOP      (MAX_SLOTS + 3)
#define FN_PANEL     (MAX_SLOTS + 4)    /* Ctrl+` */
#define FN_QUICKOPEN (MAX_SLOTS + 5)    /* Ctrl+P */
#define FN_PALETTE   (MAX_SLOTS + 6)    /* Ctrl+Shift+P */
#define FN_CLOSEFOLDER (MAX_SLOTS + 7)  /* File > Close Folder */
#define FN_NEWWINDOW (MAX_SLOTS + 8)    /* File > New Window, Ctrl+Shift+N */
#define FN_TOOLS     (MAX_SLOTS + 9)    /* Check / Install Tools... */
#define FN_DUPLINE   (MAX_SLOTS + 10)   /* the editing commands below have keys only in NppDev, where */
#define FN_CARETUP   (MAX_SLOTS + 11)   /* they replace the macros of a hand-made setup (Notepad++ */
#define FN_CARETDOWN (MAX_SLOTS + 12)   /* blocks macro keys in a shortcuts.xml it didn't write) */
#define FN_TRIMSAVE  (MAX_SLOTS + 13)
#define FN_THEMEDARK (MAX_SLOTS + 14)   /* Color Theme: Dark / Light (also in the Command Palette) */
#define FN_THEMELIGHT (MAX_SLOTS + 15)
#define FN_EXPLORER  (MAX_SLOTS + 16)   /* Toggle Explorer: toolbar button, Ctrl+B */
#define FN_COUNT     (MAX_SLOTS + 17)
#define IDM_VIEW_FILEBROWSER           44085   /* View > Folder as Workspace */
#define IDM_FILEBROWSER_REMOVEROOT     3511    /* panel right-click > Remove (selected root) */
#define PLUGIN_NAME L"Dev Tools"
#define IDC_NEWFILE_BTN 32528        /* panel toolbar buttons */
#define IDC_NEWFOLDER_BTN 32529
#define IDC_CTX_NEWFILE   32530      /* panel right-click menu */
#define IDC_CTX_NEWFOLDER 32531
#define IDC_CTX_DELETE    32532
#define IDC_CTX_RENAME    32533
#define TIMER_ID        0x5246
#define STARTUP_TIMER_ID 0x5247     /* every 50 ms right after start, until the workspace folder is shown */
#define LISTONLY_TIMER_ID 0x5248    /* once, a few seconds after start */
#define FIRSTRUN_TIMER_ID 0x5249    /* NppDev: check the tools once the window is up (every start) */
#define WARMUP_TIMER_ID 0x524A      /* once, a few seconds after start: prepare the precompiled C++ header */
#define IDC_NAME_LABEL  1001
#define IDC_NAME_EDIT   1002

HINSTANCE hInst;
NppData npp;
HWND fawTree = NULL;
wchar_t homeDir[MAX_PATH];
int runPanelFuncIndex = FN_PANEL;

static FuncItem funcs[FN_COUNT];
static ShortcutKey newFileKey   = { true, true, false, 'N' };      /* Ctrl+Alt+N */
static ShortcutKey runKey       = { true, false, false, VK_RETURN }; /* Ctrl+Enter */
static ShortcutKey panelKey     = { true, false, false, VK_OEM_3 };  /* Ctrl+` */
static ShortcutKey quickOpenKey = { true, false, false, 'P' };      /* Ctrl+P */
static ShortcutKey paletteKey   = { true, false, true, 'P' };       /* Ctrl+Shift+P */
static ShortcutKey newWindowKey = { true, false, true, 'N' };       /* Ctrl+Shift+N */
static ShortcutKey dupLineKey   = { false, true, true, VK_DOWN };   /* Alt+Shift+Down */
static ShortcutKey caretUpKey   = { true, true, false, VK_UP };     /* Ctrl+Alt+Up */
static ShortcutKey caretDownKey = { true, true, false, VK_DOWN };   /* Ctrl+Alt+Down */
static ShortcutKey trimSaveKey  = { false, true, true, 'S' };       /* Alt+Shift+S */
wchar_t devtoolsDir[MAX_PATH];  /* runner.py, tools, recent-folder lists: <NppDev>\devtools, or %USERPROFILE%\NppRunner */
bool bundleMode;                /* running as the NppDev bundle (a portable Notepad++ with devtools\ next to it) */
static wchar_t folders[MAX_SLOTS][MAX_PATH];
static int folderCount = 0;
static HMENU recentMenu = NULL;
static WNDPROC oldNppProc = NULL;
static wchar_t scriptPath[MAX_PATH], cachePath[MAX_PATH], configPath[MAX_PATH];

static HWND fawPanel = NULL;
#define PANEL_SUBCLASS_ID 0x52464650

/* Run powershell recent-folders.ps1 <args> without a console window.
   -NppPid tells it which Notepad++ window to reopen when several are open. */
static void runScript(const wchar_t *args)
{
    wchar_t cmd[2 * MAX_PATH + 256];
    swprintf(cmd, sizeof cmd / sizeof cmd[0],
             L"powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File \"%ls\" %ls -NppPid %lu",
             scriptPath, args, GetCurrentProcessId());
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    DWORD flags = CREATE_NO_WINDOW;
    if (wcsncmp(args, L"-ListOnly", 9) == 0) flags |= BELOW_NORMAL_PRIORITY_CLASS;   /* background refresh: don't compete with Notepad++ */
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

/* ---- the recent-folders list files (shared with recent-folders.ps1) ----
   recent-folders.txt (UTF-8) holds the list, recent-folders-cache.txt (UTF-16) is what the
   File menu shows, recent-folders-hidden.txt the folders removed from the list. */
#define MAX_MRU 25

static void scriptSibling(const wchar_t *name, wchar_t *out)
{
    wcscpy(out, scriptPath);
    wchar_t *s = wcsrchr(out, L'\\');
    wcscpy(s ? s + 1 : out, name);
}

/* Read a text file (UTF-8 or UTF-16 with BOM) into lines; returns the line count. */
static int readLines(const wchar_t *path, wchar_t lines[][MAX_PATH], int max)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD size = GetFileSize(f, NULL), got = 0;
    char *raw = size != INVALID_FILE_SIZE ? (char *)HeapAlloc(GetProcessHeap(), 0, size + 2) : NULL;
    wchar_t *text = raw ? (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (size + 2) * sizeof(wchar_t)) : NULL;
    int n = 0;
    if (text && ReadFile(f, raw, size, &got, NULL)) {
        raw[got] = raw[got + 1] = 0;
        if (got >= 2 && (BYTE)raw[0] == 0xFF && (BYTE)raw[1] == 0xFE) wcscpy(text, (wchar_t *)(raw + 2));
        else MultiByteToWideChar(CP_UTF8, 0, got >= 3 && (BYTE)raw[0] == 0xEF ? raw + 3 : raw, -1, text, size + 2);
        for (wchar_t *next, *line = wcstok(text, L"\r\n", &next); line && n < max; line = wcstok(NULL, L"\r\n", &next)) {
            while (*line == L' ') line++;
            size_t L = wcslen(line);
            while (L && line[L - 1] == L' ') line[--L] = 0;
            while (L > 3 && line[L - 1] == L'\\') line[--L] = 0;
            if (L && L < MAX_PATH) wcscpy(lines[n++], line);
        }
    }
    if (text) HeapFree(GetProcessHeap(), 0, text);
    if (raw) HeapFree(GetProcessHeap(), 0, raw);
    CloseHandle(f);
    return n;
}

static void writeLines(const wchar_t *path, wchar_t lines[][MAX_PATH], int n, bool utf16)
{
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD put;
    if (utf16) WriteFile(f, "\xFF\xFE", 2, &put, NULL); else WriteFile(f, "\xEF\xBB\xBF", 3, &put, NULL);
    for (int i = 0; i < n; i++) {
        wchar_t line[MAX_PATH + 2];
        int L = swprintf(line, MAX_PATH + 2, L"%ls\r\n", lines[i]);
        if (utf16) WriteFile(f, line, L * sizeof(wchar_t), &put, NULL);
        else {
            char utf8[4 * MAX_PATH + 8];
            int b = WideCharToMultiByte(CP_UTF8, 0, line, L, utf8, sizeof utf8, NULL, NULL);
            WriteFile(f, utf8, b, &put, NULL);
        }
    }
    CloseHandle(f);
}

/* Put a folder at the top of the recent list (and un-hide it) without starting PowerShell. */
static void rememberFolder(const wchar_t *folder)
{
    static wchar_t old[MAX_MRU + 1][MAX_PATH], list[MAX_MRU][MAX_PATH], hidden[256][MAX_PATH];
    wchar_t mru[MAX_PATH], hiddenFile[MAX_PATH], top[MAX_PATH];
    wcscpy(top, folder);
    size_t L = wcslen(top);
    while (L > 3 && top[L - 1] == L'\\') top[--L] = 0;
    scriptSibling(L"recent-folders.txt", mru);
    scriptSibling(L"recent-folders-hidden.txt", hiddenFile);
    int n = 0, count = readLines(mru, old, MAX_MRU + 1);
    wcscpy(list[n++], top);
    for (int i = 0; i < count && n < MAX_MRU; i++)
        if (_wcsicmp(old[i], top) != 0 && isDir(old[i])) wcscpy(list[n++], old[i]);
    writeLines(mru, list, n, false);
    writeLines(cachePath, list, n, true);
    int h = readLines(hiddenFile, hidden, 256), kept = 0;
    for (int i = 0; i < h; i++) if (_wcsicmp(hidden[i], top) != 0) { if (kept != i) wcscpy(hidden[kept], hidden[i]); kept++; }
    if (kept != h) writeLines(hiddenFile, hidden, kept, false);
}

/* The recent folders, most recent first: existing ones that weren't removed from the list. */
int recentFolderList(wchar_t out[][MAX_PATH], int max)
{
    static wchar_t all[MAX_MRU][MAX_PATH], hidden[256][MAX_PATH];
    wchar_t mru[MAX_PATH], hiddenFile[MAX_PATH];
    scriptSibling(L"recent-folders.txt", mru);
    scriptSibling(L"recent-folders-hidden.txt", hiddenFile);
    int count = readLines(mru, all, MAX_MRU), h = readLines(hiddenFile, hidden, 256), n = 0;
    for (int i = 0; i < count && n < max; i++) {
        bool skip = !isDir(all[i]);
        for (int j = 0; j < h && !skip; j++) skip = _wcsicmp(all[i], hidden[j]) == 0;
        for (int j = 0; j < n && !skip; j++) skip = _wcsicmp(all[i], out[j]) == 0;
        if (!skip) wcscpy(out[n++], all[i]);
    }
    return n;
}

/* Take a folder off the recent list (and keep it off, though Notepad++'s history still has it). */
void forgetRecentFolder(const wchar_t *folder)
{
    static wchar_t old[MAX_MRU][MAX_PATH], hidden[257][MAX_PATH];
    wchar_t mru[MAX_PATH], hiddenFile[MAX_PATH];
    scriptSibling(L"recent-folders.txt", mru);
    scriptSibling(L"recent-folders-hidden.txt", hiddenFile);
    int count = readLines(mru, old, MAX_MRU), n = 0;
    for (int i = 0; i < count; i++) if (_wcsicmp(old[i], folder) != 0) { if (n != i) wcscpy(old[n], old[i]); n++; }
    writeLines(mru, old, n, false);
    writeLines(cachePath, old, n, true);
    int h = readLines(hiddenFile, hidden, 256);
    wcscpy(hidden[h++], folder);
    writeLines(hiddenFile, hidden, h, false);
}

/* Read the UTF-16 cache written by the script (one folder per line). */
static void loadFolders(void)
{
    folderCount = 0;
    HANDLE f = CreateFileW(cachePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    static wchar_t buf[MAX_SLOTS * 4 * MAX_PATH];
    DWORD got = 0;
    ReadFile(f, buf, sizeof buf - sizeof(wchar_t), &got, NULL);
    CloseHandle(f);
    size_t n = got / sizeof(wchar_t);
    buf[n] = 0;
    wchar_t *p = (n && buf[0] == 0xFEFF) ? buf + 1 : buf;
    while (*p && folderCount < MAX_SLOTS) {
        wchar_t *end = wcspbrk(p, L"\r\n");
        size_t len = end ? (size_t)(end - p) : wcslen(p);
        if (len > 0 && len < MAX_PATH) {
            wmemcpy(folders[folderCount], p, len);
            folders[folderCount][len] = 0;
            folderCount++;
        }
        if (!end) break;
        p = end;
        while (*p == L'\r' || *p == L'\n') p++;
    }
}

/* Rebuild the submenu items from the cache. */
static void rebuildMenu(void)
{
    if (!recentMenu) return;
    loadFolders();
    while (GetMenuItemCount(recentMenu) > 0) DeleteMenu(recentMenu, 0, MF_BYPOSITION);
    size_t homeLen = wcslen(homeDir);
    for (int i = 0; i < folderCount; i++) {
        wchar_t text[2 * MAX_PATH + 8], *t = text;
        t += swprintf(t, 8, L"&%d: ", (i + 1) % 10);
        const wchar_t *shown = folders[i];
        if (homeLen && _wcsnicmp(shown, homeDir, homeLen) == 0 && (shown[homeLen] == L'\\' || !shown[homeLen])) {
            *t++ = L'~';                                     /* VS Code style: ~\Documents\... */
            shown += homeLen;
        }
        for (const wchar_t *s = shown; *s; s++) {          /* '&' would become a mnemonic */
            if (*s == L'&') *t++ = L'&';
            *t++ = *s;
        }
        *t = 0;
        AppendMenuW(recentMenu, MF_STRING, funcs[i]._cmdID, text);
    }
    if (folderCount == 0) AppendMenuW(recentMenu, MF_STRING | MF_GRAYED, 0, L"(no recent folders yet)");
    AppendMenuW(recentMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(recentMenu, MF_STRING, funcs[FN_MORE]._cmdID, L"More Folders...\tCtrl+Shift+R");
}

static bool openFolderInPlace(const wchar_t *folder);
static void syncExplorerButton(void);
static void openToolsWindow(bool startup);
static void warmUpRunner(void);

/* Switch this window to a folder: instantly in place, or by reopening Notepad++ if that fails. */
static void openFolder(const wchar_t *folder)
{
    if (openFolderInPlace(folder)) { rememberFolder(folder); return; }
    wchar_t args[MAX_PATH + 16];
    swprintf(args, sizeof args / sizeof args[0], L"-Open \"%ls\"", folder);
    runScript(args);
}

static void openSlot(int i)
{
    if (i >= folderCount) return;
    wchar_t folder[MAX_PATH];
    wcscpy(folder, folders[i]);
    openFolder(folder);
}

#define SLOT(n) static void __cdecl slot##n(void) { openSlot(n); }
SLOT(0) SLOT(1) SLOT(2) SLOT(3) SLOT(4) SLOT(5) SLOT(6) SLOT(7) SLOT(8) SLOT(9)
static void __cdecl moreFolders(void) { recentFolderPicker(); }
static const PFUNCPLUGINCMD slotFuncs[MAX_SLOTS] = { slot0, slot1, slot2, slot3, slot4, slot5, slot6, slot7, slot8, slot9 };

/* ================= New File (Folder as Workspace) ================= */

bool isDir(const wchar_t *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }

const wchar_t *leafOf(const wchar_t *p)
{
    const wchar_t *s = wcsrchr(p, L'\\');
    return s ? s + 1 : p;
}

/* Decode the few XML entities Notepad++ writes in config.xml. */
static void xmlDecode(wchar_t *s)
{
    static const struct { const wchar_t *ent; wchar_t ch; } map[] = {
        { L"&amp;", L'&' }, { L"&apos;", L'\'' }, { L"&quot;", L'"' }, { L"&lt;", L'<' }, { L"&gt;", L'>' } };
    wchar_t *r = s, *w = s;
    while (*r) {
        bool done = false;
        if (*r == L'&')
            for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
                size_t n = wcslen(map[i].ent);
                if (wcsncmp(r, map[i].ent, n) == 0) { *w++ = map[i].ch; r += n; done = true; break; }
            }
        if (!done) *w++ = *r++;
    }
    *w = 0;
}

/* Find the full path of a Folder as Workspace root from its display name.
   Results are cached for 10 s. */
static bool findRootPathUncached(const wchar_t *rootName, wchar_t *out);

bool findRootPath(const wchar_t *rootName, wchar_t *out)
{
    static struct { wchar_t name[MAX_PATH], path[MAX_PATH]; DWORD at; } cache[8];
    DWORD now = GetTickCount();
    for (int i = 0; i < 8; i++)
        if (cache[i].name[0] && now - cache[i].at < 10000 && _wcsicmp(cache[i].name, rootName) == 0) {
            wcscpy(out, cache[i].path);
            return true;
        }
    if (!findRootPathUncached(rootName, out)) return false;
    int slot = 0;
    for (int i = 1; i < 8; i++) if (cache[i].at < cache[slot].at || !cache[i].name[0]) slot = i;
    wcscpy(cache[slot].name, rootName);
    wcscpy(cache[slot].path, out);
    cache[slot].at = now ? now : 1;
    return true;
}

int workspaceRoots(wchar_t roots[][MAX_PATH], int max)
{
    int n = 0;
    if (!fawTree || !IsWindow(fawTree)) return 0;
    for (HTREEITEM it = TreeView_GetRoot(fawTree); it && n < max; it = TreeView_GetNextSibling(fawTree, it))
        if (itemPath(it, roots[n]) && isDir(roots[n])) n++;
    return n;
}

static bool findRootPathUncached(const wchar_t *rootName, wchar_t *out)
{
    loadFolders();                                           /* recent folders (current one first) */
    for (int i = 0; i < folderCount; i++)
        if (_wcsicmp(leafOf(folders[i]), rootName) == 0 && isDir(folders[i])) { wcscpy(out, folders[i]); return true; }

    /* <root foldername="..."/> entries saved in config.xml */
    HANDLE f = CreateFileW(configPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD size = GetFileSize(f, NULL), got = 0;
        char *utf8 = size != INVALID_FILE_SIZE ? (char *)HeapAlloc(GetProcessHeap(), 0, size + 1) : NULL;
        if (utf8 && ReadFile(f, utf8, size, &got, NULL)) {
            utf8[got] = 0;
            int wn = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
            wchar_t *wide = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, wn * sizeof(wchar_t));
            if (wide) {
                MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wn);
                for (wchar_t *p = wide; (p = wcsstr(p, L"<root foldername=\"")) != NULL; ) {
                    p += 18;
                    wchar_t *q = wcschr(p, L'"');
                    if (!q || q - p >= MAX_PATH) break;
                    wchar_t cand[MAX_PATH];
                    wmemcpy(cand, p, q - p); cand[q - p] = 0;
                    xmlDecode(cand);
                    size_t L = wcslen(cand);
                    while (L > 3 && cand[L - 1] == L'\\') cand[--L] = 0;
                    if (_wcsicmp(leafOf(cand), rootName) == 0 && isDir(cand)) {
                        wcscpy(out, cand);
                        HeapFree(GetProcessHeap(), 0, wide); HeapFree(GetProcessHeap(), 0, utf8); CloseHandle(f);
                        return true;
                    }
                    p = q;
                }
                HeapFree(GetProcessHeap(), 0, wide);
            }
        }
        if (utf8) HeapFree(GetProcessHeap(), 0, utf8);
        CloseHandle(f);
    }

    /* an ancestor of the current file's folder with that name */
    wchar_t dir[MAX_PATH] = L"";
    SendMessageW(npp._nppHandle, NPPM_GETCURRENTDIRECTORY, MAX_PATH, (LPARAM)dir);
    for (size_t L = wcslen(dir); L > 0; ) {
        if (_wcsicmp(leafOf(dir), rootName) == 0 && isDir(dir)) { wcscpy(out, dir); return true; }
        wchar_t *s = wcsrchr(dir, L'\\');
        if (!s) break;
        *s = 0; L = wcslen(dir);
    }
    return false;
}

/* Full path of a Folder as Workspace tree item (file or folder). */
bool itemPath(HTREEITEM it, wchar_t *out)
{
    if (!fawTree || !IsWindow(fawTree) || !it) return false;
    wchar_t parts[64][MAX_PATH];
    int n = 0;
    for (HTREEITEM cur = it; cur && n < 64; cur = TreeView_GetParent(fawTree, cur)) {
        TVITEMW tv = { 0 };
        tv.mask = TVIF_TEXT; tv.hItem = cur; tv.pszText = parts[n]; tv.cchTextMax = MAX_PATH;
        if (!TreeView_GetItem(fawTree, &tv)) return false;
        n++;
    }
    if (n == 0 || !findRootPath(parts[n - 1], out)) return false;
    for (int i = n - 2; i >= 0; i--) {
        if (wcslen(out) + 1 + wcslen(parts[i]) >= MAX_PATH) return false;
        wcscat(out, L"\\");
        wcscat(out, parts[i]);
    }
    return true;
}

/* Folder to create something in: the item itself if it is a folder, else its folder;
   no item = the panel's selection (or first root); fallback = current file's folder. */
static void targetFolderFor(HTREEITEM it, wchar_t *out)
{
    out[0] = 0;
    if (fawTree && IsWindow(fawTree) && !it) {
        it = TreeView_GetSelection(fawTree);
        if (!it) it = TreeView_GetRoot(fawTree);
    }
    wchar_t path[MAX_PATH];
    if (itemPath(it, path)) {
        while (!isDir(path)) {                             /* a file: use its folder */
            wchar_t *s = wcsrchr(path, L'\\');
            if (!s) break;
            *s = 0;
        }
        if (isDir(path)) { wcscpy(out, path); return; }
    }
    SendMessageW(npp._nppHandle, NPPM_GETCURRENTDIRECTORY, MAX_PATH, (LPARAM)out);
}

/* ---- small "name" dialog built in memory ---- */
typedef struct { const wchar_t *prefix, *folder, *initial; wchar_t name[MAX_PATH]; } NameDlgData;

static INT_PTR CALLBACK nameDlgProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(h, GWLP_USERDATA, l);
        NameDlgData *d = (NameDlgData *)l;
        wchar_t label[MAX_PATH + 64];
        swprintf(label, sizeof label / sizeof label[0], L"%ls  %ls", d->prefix, d->folder);
        SetDlgItemTextW(h, IDC_NAME_LABEL, label);
        SendDlgItemMessageW(h, IDC_NAME_EDIT, EM_LIMITTEXT, MAX_PATH - 1, 0);
        if (d->initial && d->initial[0]) {                 /* rename: select the name, not the extension */
            SetDlgItemTextW(h, IDC_NAME_EDIT, d->initial);
            const wchar_t *dot = wcsrchr(d->initial, L'.');
            int end = dot && dot > d->initial ? (int)(dot - d->initial) : (int)wcslen(d->initial);
            SendDlgItemMessageW(h, IDC_NAME_EDIT, EM_SETSEL, 0, end);
        }
        SendMessageW(npp._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME, DMF_INIT, (LPARAM)h);
        SetFocus(GetDlgItem(h, IDC_NAME_EDIT));
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            NameDlgData *d = (NameDlgData *)GetWindowLongPtrW(h, GWLP_USERDATA);
            GetDlgItemTextW(h, IDC_NAME_EDIT, d->name, MAX_PATH);
            EndDialog(h, 1);
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) { EndDialog(h, 0); return TRUE; }
        break;
    }
    return FALSE;
}

WORD *dlgPutStr(WORD *p, const wchar_t *s) { while ((*p++ = *s++) != 0) {} return p; }
WORD *dlgAlign(WORD *p) { return (WORD *)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3); }

WORD *dlgPutItem(WORD *p, DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const wchar_t *text)
{
    p = dlgAlign(p);
    DLGITEMTEMPLATE *it = (DLGITEMTEMPLATE *)p;
    it->style = style | WS_CHILD | WS_VISIBLE; it->dwExtendedStyle = 0;
    it->x = x; it->y = y; it->cx = cx; it->cy = cy; it->id = id;
    p = (WORD *)(it + 1);
    *p++ = 0xFFFF; *p++ = cls;                 /* predefined class atom: 0x80 button, 0x81 edit, 0x82 static, 0x83 listbox */
    p = dlgPutStr(p, text);
    *p++ = 0;                                  /* no creation data */
    return p;
}

/* Dialog header (Segoe UI 9) at the DWORD-aligned start of tmpl; returns where items go. */
WORD *dlgBegin(WORD *tmpl, DWORD style, short cx, short cy, WORD count, const wchar_t *title)
{
    WORD *p = dlgAlign(tmpl);
    DLGTEMPLATE *dt = (DLGTEMPLATE *)p;
    dt->style = style | DS_SETFONT; dt->dwExtendedStyle = 0;
    dt->cdit = count; dt->x = 0; dt->y = 0; dt->cx = cx; dt->cy = cy;
    p = (WORD *)(dt + 1);
    *p++ = 0; *p++ = 0;                        /* no menu, default class */
    p = dlgPutStr(p, title);
    *p++ = 9;                                  /* font size */
    return dlgPutStr(p, L"Segoe UI");
}

COLORREF editorFore(void) { return (COLORREF)SendMessageW(npp._nppHandle, NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR, 0, 0); }
COLORREF editorBack(void) { return (COLORREF)SendMessageW(npp._nppHandle, NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR, 0, 0); }

/* Font of the main editor (UTF-8 name, point size), so panels match the theme. */
void editorFont(char *name, int cb, int *size)
{
    name[0] = 0;
    if (cb >= 64) SendMessageA(npp._scintillaMainHandle, SCI_STYLEGETFONT, STYLE_DEFAULT, (LPARAM)name);
    if (!name[0]) strcpy(name, "Consolas");
    *size = (int)SendMessageW(npp._scintillaMainHandle, SCI_STYLEGETSIZE, STYLE_DEFAULT, 0);
    if (*size < 6 || *size > 40) *size = 10;
}

static bool askNameEx(const wchar_t *title, const wchar_t *prefix, const wchar_t *folder,
                      const wchar_t *initial, const wchar_t *okText, wchar_t *name)
{
    static WORD tmpl[1024];
    memset(tmpl, 0, sizeof tmpl);
    WORD *p = dlgBegin(tmpl, DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU, 280, 74, 4, title);
    p = dlgPutItem(p, SS_LEFT | SS_PATHELLIPSIS, 8, 8, 264, 10, IDC_NAME_LABEL, 0x0082, L"");
    p = dlgPutItem(p, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 8, 24, 264, 14, IDC_NAME_EDIT, 0x0081, L"");
    p = dlgPutItem(p, BS_DEFPUSHBUTTON | WS_TABSTOP, 160, 50, 54, 15, IDOK, 0x0080, okText);
    p = dlgPutItem(p, BS_PUSHBUTTON | WS_TABSTOP, 218, 50, 54, 15, IDCANCEL, 0x0080, L"Cancel");

    NameDlgData d = { prefix, folder, initial, L"" };
    if (DialogBoxIndirectParamW(hInst, (DLGTEMPLATE *)dlgAlign(tmpl), npp._nppHandle, nameDlgProc, (LPARAM)&d) != 1)
        return false;
    wcscpy(name, d.name);
    return true;
}

static bool askName(const wchar_t *title, const wchar_t *prefix, const wchar_t *folder, wchar_t *name)
{
    return askNameEx(title, prefix, folder, NULL, L"Create", name);
}

/* Starter code for C, C++ and Java (UTF-8) with "@|" marking where the cursor goes; false = no template. */
static bool starterText(const wchar_t *path, char *text, int cap, int *cursor)
{
    const wchar_t *ext = wcsrchr(path, L'.');
    const wchar_t *base = leafOf(path);
    if (!ext || ext < base) return false;
    char cls[MAX_PATH] = "";
    WideCharToMultiByte(CP_UTF8, 0, base, (int)(ext - base), cls, sizeof cls - 1, NULL, NULL);
    if (_wcsicmp(ext, L".java") == 0)
        snprintf(text, cap,
                 "import java.util.*;\r\n\r\npublic class %s {\r\n    public static void main(String[] args) {\r\n"
                 "        Scanner sc = new Scanner(System.in);\r\n        @|\r\n    }\r\n}\r\n", cls);
    else if (_wcsicmp(ext, L".c") == 0)
        snprintf(text, cap, "#include <stdio.h>\r\n\r\nint main(void) {\r\n    @|\r\n    return 0;\r\n}\r\n");
    else if (!_wcsicmp(ext, L".cpp") || !_wcsicmp(ext, L".cc") || !_wcsicmp(ext, L".cxx"))
        snprintf(text, cap, "#include <bits/stdc++.h>\r\nusing namespace std;\r\n\r\nint main() {\r\n"
                 "    ios::sync_with_stdio(false);\r\n    cin.tie(nullptr);\r\n\r\n    @|\r\n    return 0;\r\n}\r\n");
    else return false;      /* .js starts empty: Run provides input / next() itself (devtools\js-prelude.js) */
    char *mark = strstr(text, "@|");
    *cursor = mark ? (int)(mark - text) : 0;
    if (mark) memmove(mark, mark + 2, strlen(mark + 2) + 1);
    return true;
}

static void writeStarter(HANDLE f, const wchar_t *path)
{
    char text[1024]; int cursor; DWORD wrote;
    if (starterText(path, text, sizeof text, &cursor)) WriteFile(f, text, (DWORD)strlen(text), &wrote, NULL);
}

/* Put the template into an empty C / C++ / Java / JavaScript file the first time it is shown or saved
   (files made in Explorer, File > New + Save As, ...). Files seen before are left alone,
   so emptying a file on purpose sticks. */
static void templateIfEmpty(void)
{
    static uint32_t seen[512]; static int seenCount;
    wchar_t path[MAX_PATH] = L"";
    SendMessageW(npp._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH, (LPARAM)path);
    if (!wcschr(path, L'\\')) return;                         /* untitled "new 1" */
    uint32_t h = 2166136261u;
    for (const wchar_t *s = path; *s; s++) { h ^= (uint32_t)towlower(*s); h *= 16777619u; }
    for (int i = 0; i < seenCount; i++) if (seen[i] == h) return;
    if (seenCount < 512) seen[seenCount++] = h;
    int which = 0;
    SendMessageW(npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    HWND ed = which ? npp._scintillaSecondHandle : npp._scintillaMainHandle;
    if (SendMessageW(ed, SCI_GETLENGTH, 0, 0) != 0) return;
    char text[1024]; int cursor;
    if (!starterText(path, text, sizeof text, &cursor)) return;
    SendMessageW(ed, SCI_SETTEXT, 0, (LPARAM)text);
    SendMessageW(ed, SCI_GOTOPOS, cursor, 0);
}

/* Trim a typed name, allow sub/folder/name with / or \; NULL if empty or invalid. */
static wchar_t *cleanName(wchar_t *name, const wchar_t *title)
{
    wchar_t *s = name; while (*s == L' ') s++;
    size_t L = wcslen(s); while (L && (s[L - 1] == L' ' || s[L - 1] == L'.')) s[--L] = 0;
    for (wchar_t *c = s; *c; c++) if (*c == L'/') *c = L'\\';
    while (*s == L'\\') s++;
    if (!*s) return NULL;
    if (wcspbrk(s, L":*?\"<>|") || wcsstr(s, L"..")) {
        MessageBoxW(npp._nppHandle, L"That name contains characters that are not allowed in file names.", title, MB_ICONWARNING);
        return NULL;
    }
    return s;
}

static bool needFolder(const wchar_t *folder, const wchar_t *title)
{
    if (folder[0] && isDir(folder)) return true;
    MessageBoxW(npp._nppHandle, L"Open a folder first (File > Open Folder as Workspace).", title, MB_ICONINFORMATION);
    return false;
}

static void newFileIn(HTREEITEM it)
{
    wchar_t folder[MAX_PATH], name[MAX_PATH], full[MAX_PATH];
    targetFolderFor(it, folder);
    if (!needFolder(folder, L"New File")) return;
    if (!askName(L"New File", L"New file in:", folder, name)) return;
    wchar_t *s = cleanName(name, L"New File");
    if (!s || swprintf(full, MAX_PATH, L"%ls\\%ls", folder, s) < 0) return;

    wchar_t parent[MAX_PATH];
    wcscpy(parent, full);
    wchar_t *slash = wcsrchr(parent, L'\\');
    if (slash) { *slash = 0; SHCreateDirectoryExW(NULL, parent, NULL); }

    if (s[wcslen(s) - 1] == L'\\' || isDir(full)) {      /* just a folder */
        SHCreateDirectoryExW(NULL, full, NULL);
        return;
    }
    HANDLE f = CreateFileW(full, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        writeStarter(f, full);
        CloseHandle(f);
    } else if (GetLastError() != ERROR_FILE_EXISTS) {
        wchar_t msg[MAX_PATH + 64];
        swprintf(msg, sizeof msg / sizeof msg[0], L"Could not create:\n%ls", full);
        MessageBoxW(npp._nppHandle, msg, L"New File", MB_ICONERROR);
        return;
    }
    SendMessageW(npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)full);   /* existing file: just open it */
}

static void __cdecl newFileInFolder(void) { newFileIn(NULL); }

static HWND fawPanelWindow(void) { return fawPanel && IsWindow(fawPanel) ? fawPanel : NULL; }

/* File > New Window (VS Code style): a second, independent Notepad++ window on the current
   virtual desktop. -multiInst gets around Notepad++'s single window; -nosession keeps it empty
   and stops it from overwriting the main window's list of open files. */
static void __cdecl newWindow(void)
{
    wchar_t exe[MAX_PATH], cmd[MAX_PATH + 48];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    swprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" -multiInst -nosession", exe);
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

/* File > Close Folder (VS Code style): save and close the folder's files (remembered for next
   time) and remove every root from Folder as Workspace; the panel stays, showing "Open Folder".
   Notepad++ saves the (now empty) list at exit, so it reopens without a folder. */
static bool currentWorkspace(wchar_t *out);
static void stashFiles(const wchar_t *folder, bool save, bool close);

static void __cdecl closeFolder(void)
{
    HWND panel = fawPanelWindow();
    if (!panel || !fawTree || !TreeView_GetRoot(fawTree)) return;   /* no folder open */
    wchar_t cur[MAX_PATH];
    if (currentWorkspace(cur)) stashFiles(cur, true, true);        /* like VS Code: its files close with it */
    for (int guard = 0; guard < 32; guard++) {
        HTREEITEM root = TreeView_GetRoot(fawTree);
        if (!root) break;
        TreeView_SelectItem(fawTree, root);
        SendMessageW(panel, WM_COMMAND, IDM_FILEBROWSER_REMOVEROOT, 0);
        if (TreeView_GetRoot(fawTree) == root) break;                /* nothing happened: stop */
    }
    explorerRefresh();
}

void closeFolderCommand(void) { closeFolder(); }

static void newFolderIn(HTREEITEM it)
{
    wchar_t folder[MAX_PATH], name[MAX_PATH], full[MAX_PATH];
    targetFolderFor(it, folder);
    if (!needFolder(folder, L"New Folder")) return;
    if (!askName(L"New Folder", L"New folder in:", folder, name)) return;
    wchar_t *s = cleanName(name, L"New Folder");
    if (!s || swprintf(full, MAX_PATH, L"%ls\\%ls", folder, s) < 0) return;
    if (GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(npp._nppHandle, L"A file or folder with that name already exists.", L"New Folder", MB_ICONINFORMATION);
        return;
    }
    int rc = SHCreateDirectoryExW(NULL, full, NULL);
    if (rc != ERROR_SUCCESS) {
        wchar_t msg[MAX_PATH + 64];
        swprintf(msg, sizeof msg / sizeof msg[0], L"Could not create folder:\n%ls", full);
        MessageBoxW(npp._nppHandle, msg, L"New Folder", MB_ICONERROR);
    }
}

/* the Explorer header's New File / New Folder buttons (explorer.c) */
void explorerNewFile(void) { newFileIn(NULL); }
void explorerNewFolder(void) { newFolderIn(NULL); }

/* ---- delete (to the Recycle Bin) ---- */
#define NPPM_GETNBOPENFILES        (NPPMSG + 7)
#define NPPM_SWITCHTOFILE          (NPPMSG + 37)
#define NPPM_GETFULLPATHFROMBUFFERID (NPPMSG + 58)
#define NPPM_GETBUFFERIDFROMPOS    (NPPMSG + 59)
#define IDM_FILE_CLOSE             41003

static bool isUnder(const wchar_t *file, const wchar_t *path, bool dir)
{
    size_t n = wcslen(path);
    if (_wcsnicmp(file, path, n) != 0) return false;
    return dir ? file[n] == L'\\' : file[n] == 0;
}

/* Close Notepad++ tabs showing the file (or anything inside the folder).
   false if one stayed open, e.g. the user cancelled a "save changes?" question.
   closed / closedCount (optional) receive the paths that were closed. */
static wchar_t closedDocs[512][MAX_PATH];
static int closedDocCount;

static bool closeOpenDocs(const wchar_t *path, bool dir)
{
    static wchar_t open[512][MAX_PATH];
    int count = 0;
    closedDocCount = 0;
    for (int view = 0; view < 2; view++) {
        int nb = (int)SendMessageW(npp._nppHandle, NPPM_GETNBOPENFILES, 0, view == 0 ? 1 : 2);   /* PRIMARY / SECOND_VIEW */
        for (int pos = 0; pos < nb && count < 512; pos++) {
            LRESULT id = SendMessageW(npp._nppHandle, NPPM_GETBUFFERIDFROMPOS, pos, view);
            if (id && SendMessageW(npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, id, (LPARAM)open[count]) > 0
                   && isUnder(open[count], path, dir))
                count++;
        }
    }
    for (int i = 0; i < count; i++) {
        if (!SendMessageW(npp._nppHandle, NPPM_SWITCHTOFILE, 0, (LPARAM)open[i])) continue;
        SendMessageW(npp._nppHandle, WM_COMMAND, IDM_FILE_CLOSE, 0);
        if (SendMessageW(npp._nppHandle, NPPM_SWITCHTOFILE, 0, (LPARAM)open[i])) return false;
        wcscpy(closedDocs[closedDocCount++], open[i]);
    }
    return true;
}

/* ---- open files per folder (VS Code style) ----
   Switching folders saves the current folder's files, writes which ones were open (with cursor and
   scroll position) to sessions\<hash>.txt in the plugin config folder, closes them, then reopens
   the list saved for the new folder. Untitled "new 1" tabs are left alone. */
#define NPPM_ACTIVATEDOC           (NPPMSG + 28)
#define NPPM_GETCURRENTBUFFERID    (NPPMSG + 60)
#define NPPN_BEFORESHUTDOWN        1019
#define SCI_GETCURRENTPOS          2008
#define SCI_GETFIRSTVISIBLELINE    2152
#define SCI_SETEMPTYSELECTION      2556
#define SCI_SETFIRSTVISIBLELINE    2613

static bool filesStashed;   /* this window's files were closed for a switch that then failed: nothing to save at exit */

static bool sessionFileFor(const wchar_t *folder, wchar_t *out)
{
    wchar_t cfg[MAX_PATH] = L"";
    SendMessageW(npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)cfg);
    if (!cfg[0]) return false;
    uint32_t h = 2166136261u;
    size_t L = wcslen(folder);
    while (L > 3 && folder[L - 1] == L'\\') L--;
    for (size_t i = 0; i < L; i++) { h ^= (uint32_t)towlower(folder[i]); h *= 16777619u; }
    if (swprintf(out, MAX_PATH, L"%ls\\RecentFolders\\sessions", cfg) < 0) return false;
    SHCreateDirectoryExW(NULL, out, NULL);
    return swprintf(out + wcslen(out), MAX_PATH - wcslen(out), L"\\%08x.txt", h) >= 0;
}

/* The folder shown in the Folder as Workspace panel, if exactly one. */
static bool currentWorkspace(wchar_t *out)
{
    wchar_t roots[2][MAX_PATH];
    if (workspaceRoots(roots, 2) != 1) return false;
    wcscpy(out, roots[0]);
    return true;
}

/* The files stashFiles found (closeStashed closes them) and the ones restoreFiles opened. */
static void closeStashed(void);
static wchar_t stashed[512][MAX_PATH], restored[512][MAX_PATH];
static int stashedCount, restoredCount;

/* Remember the open files for `folder` (NULL = don't remember); with `save`, save the modified
   ones; with `close`, close them all. At exit nothing is saved: Notepad++ keeps unsaved changes itself. */
/* Cursor, scroll and modified state of each tab, noted while you work in it, so switching folders
   doesn't have to bring every tab to the front to read them. */
#define NPPM_GETCURRENTDOCINDEX    (NPPMSG + 23)
#define SCN_SAVEPOINTREACHED       2002
#define SCN_SAVEPOINTLEFT          2003
#define SCN_UPDATEUI               2007
#define SCI_GETDOCPOINTER          2357
static struct { LRESULT id; intptr_t caret, first; bool dirty; } seen[512];
static int seenCount, seenNext;
static LRESULT shownBuf;     /* the tab in front, and the Scintilla document it uses */
static LRESULT shownDoc;

static int seenSlot(LRESULT id, bool add)
{
    for (int i = 0; i < seenCount; i++) if (seen[i].id == id) return i;
    if (!add) return -1;
    int i = seenCount < 512 ? seenCount++ : (seenNext++ % 512);
    seen[i].id = id;
    return i;
}

static HWND currentEditor(void)
{
    int which = 0;
    SendMessageW(npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    return which ? npp._scintillaSecondHandle : npp._scintillaMainHandle;
}

static void noteShownTab(void)
{
    HWND ed = currentEditor();
    if (!shownBuf || SendMessageW(ed, SCI_GETDOCPOINTER, 0, 0) != shownDoc) return;   /* mid-switch: not that tab's text */
    int i = seenSlot(shownBuf, true);
    seen[i].caret = SendMessageW(ed, SCI_GETCURRENTPOS, 0, 0);
    seen[i].first = SendMessageW(ed, SCI_GETFIRSTVISIBLELINE, 0, 0);
    seen[i].dirty = SendMessageW(ed, SCI_GETMODIFY, 0, 0) != 0;
}

static void trackTabs(SciNotifyHeader *n)
{
    if (n->code == NPPN_BUFFERACTIVATED && n->hwndFrom == npp._nppHandle) {
        shownBuf = (LRESULT)n->idFrom;
        shownDoc = SendMessageW(currentEditor(), SCI_GETDOCPOINTER, 0, 0);
        noteShownTab();
    } else if ((n->code == SCN_UPDATEUI || n->code == SCN_SAVEPOINTLEFT || n->code == SCN_SAVEPOINTREACHED)
               && n->hwndFrom == currentEditor())
        noteShownTab();
}

static void stashFiles(const wchar_t *folder, bool save, bool close)
{
    static intptr_t caret[512], first[512];
    int active = -1;
    bool moved = false;
    stashedCount = 0;
    LRESULT activeBuf = SendMessageW(npp._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);
    for (int view = 0; view < 2; view++) {
        HWND ed = view ? npp._scintillaSecondHandle : npp._scintillaMainHandle;
        int nb = (int)SendMessageW(npp._nppHandle, NPPM_GETNBOPENFILES, 0, view == 0 ? 1 : 2);   /* PRIMARY / SECOND_VIEW */
        for (int pos = 0; pos < nb && stashedCount < 512; pos++) {
            LRESULT id = SendMessageW(npp._nppHandle, NPPM_GETBUFFERIDFROMPOS, pos, view);
            if (!id || SendMessageW(npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, id, (LPARAM)stashed[stashedCount]) <= 0
                    || !wcschr(stashed[stashedCount], L'\\')) continue;           /* untitled: stays open */
            int s = seenSlot(id, false);
            bool inFront = SendMessageW(npp._nppHandle, NPPM_GETCURRENTDOCINDEX, 0, view) == pos;   /* shown in its view's editor */
            bool dirty = inFront ? SendMessageW(ed, SCI_GETMODIFY, 0, 0) != 0 : s < 0 || seen[s].dirty;
            if ((save && dirty) || (!inFront && s < 0)) {             /* to save it or read it, it has to be the active tab */
                SendMessageW(npp._nppHandle, NPPM_ACTIVATEDOC, view, pos);
                if (id != activeBuf) moved = true;
                if (save && SendMessageW(ed, SCI_GETMODIFY, 0, 0)) SendMessageW(npp._nppHandle, NPPM_SAVECURRENTFILE, 0, 0);
            }
            if (inFront || s < 0 || (save && dirty)) {
                caret[stashedCount] = SendMessageW(ed, SCI_GETCURRENTPOS, 0, 0);
                first[stashedCount] = SendMessageW(ed, SCI_GETFIRSTVISIBLELINE, 0, 0);
            } else {                                                  /* noted earlier and saved: leave it where it is */
                caret[stashedCount] = seen[s].caret;
                first[stashedCount] = seen[s].first;
            }
            if (id == activeBuf) active = stashedCount;
            stashedCount++;
        }
    }
    wchar_t file[MAX_PATH];
    if (folder && sessionFileFor(folder, file)) {
        HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            wchar_t line[MAX_PATH + 64];
            DWORD put;
            int n = swprintf(line, sizeof line / sizeof line[0], L"\xFEFF%ls\r\n", folder);
            WriteFile(f, line, n * sizeof(wchar_t), &put, NULL);
            for (int i = 0; i < stashedCount; i++) {   /* "*|caret|first line|path", * = the active file */
                n = swprintf(line, sizeof line / sizeof line[0], L"%lc|%lld|%lld|%ls\r\n",
                             i == active ? L'*' : L' ', (long long)caret[i], (long long)first[i], stashed[i]);
                WriteFile(f, line, n * sizeof(wchar_t), &put, NULL);
            }
            CloseHandle(f);
        }
    }
    if (close) {
        restoredCount = 0;
        closeStashed();
    } else if (activeBuf && moved) {
        wchar_t cur[MAX_PATH];
        if (SendMessageW(npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, activeBuf, (LPARAM)cur) > 0)
            SendMessageW(npp._nppHandle, NPPM_SWITCHTOFILE, 0, (LPARAM)cur);
    }
}

/* Close the stashed files, except ones the new folder just reopened. Closing them only after the
   new folder's files are open means Notepad++ never runs out of tabs and flashes an empty "new 1". */
static void closeStashed(void)
{
    bool curClosed = false;
    wchar_t cur[MAX_PATH] = L"";
    SendMessageW(npp._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH, (LPARAM)cur);
    for (int i = 0; i < stashedCount; i++) {
        bool keep = false;
        for (int j = 0; j < restoredCount && !keep; j++) keep = _wcsicmp(stashed[i], restored[j]) == 0;
        if (keep) continue;
        if (_wcsicmp(stashed[i], cur) == 0) curClosed = true;
        if (SendMessageW(npp._nppHandle, NPPM_SWITCHTOFILE, 0, (LPARAM)stashed[i]))
            SendMessageW(npp._nppHandle, WM_COMMAND, IDM_FILE_CLOSE, 0);
    }
    if (!curClosed && cur[0]) SendMessageW(npp._nppHandle, NPPM_SWITCHTOFILE, 0, (LPARAM)cur);   /* back to the new folder's active file */
    stashedCount = 0;
}

/* Reopen the files last open in `folder` (skipping ones that no longer exist). */
static void restoreFiles(const wchar_t *folder)
{
    wchar_t file[MAX_PATH], active[MAX_PATH] = L"";
    restoredCount = 0;
    if (!sessionFileFor(folder, file)) return;
    HANDLE f = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(f, NULL), got = 0;
    wchar_t *text = size != INVALID_FILE_SIZE ? (wchar_t *)HeapAlloc(GetProcessHeap(), 0, size + sizeof(wchar_t)) : NULL;
    if (text && ReadFile(f, text, size, &got, NULL)) {
        text[got / sizeof(wchar_t)] = 0;
        wchar_t *next, *line = wcstok(text, L"\r\n", &next);           /* first line: the folder */
        while ((line = wcstok(NULL, L"\r\n", &next)) != NULL) {
            wchar_t mark = line[0], *p = line + 1;
            if (*p++ != L'|') continue;
            long long caret = wcstoll(p, &p, 10);
            if (*p++ != L'|') continue;
            long long first = wcstoll(p, &p, 10);
            if (*p++ != L'|' || GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES || isDir(p)) continue;
            if (!SendMessageW(npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)p)) continue;
            if (restoredCount < 512) wcscpy(restored[restoredCount++], p);
            int which = 0;
            SendMessageW(npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
            HWND ed = which ? npp._scintillaSecondHandle : npp._scintillaMainHandle;
            SendMessageW(ed, SCI_SETEMPTYSELECTION, (WPARAM)caret, 0);
            SendMessageW(ed, SCI_SETFIRSTVISIBLELINE, (WPARAM)first, 0);
            if (mark == L'*') wcscpy(active, p);
        }
        if (active[0]) SendMessageW(npp._nppHandle, NPPM_SWITCHTOFILE, 0, (LPARAM)active);
    }
    if (text) HeapFree(GetProcessHeap(), 0, text);
    CloseHandle(f);
}

/* Rename a file or folder from the Folder as Workspace panel (VS Code style).
   Open tabs of it are closed first (Notepad++ asks about unsaved changes) and reopened at the new path. */
static void renameItem(HTREEITEM it)
{
    if (!fawTree || !it) return;
    if (!TreeView_GetParent(fawTree, it)) {
        MessageBoxW(npp._nppHandle, L"The top folder of the workspace can't be renamed from here.", L"Rename", MB_ICONINFORMATION);
        return;
    }
    wchar_t path[MAX_PATH], folder[MAX_PATH], name[MAX_PATH], target[MAX_PATH], msg[2 * MAX_PATH + 96];
    if (!itemPath(it, path) || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(npp._nppHandle, L"Could not find that file on disk.", L"Rename", MB_ICONWARNING);
        return;
    }
    bool dir = isDir(path);
    wcscpy(folder, path);
    *wcsrchr(folder, L'\\') = 0;
    if (!askNameEx(L"Rename", dir ? L"Rename folder in:" : L"Rename file in:", folder, leafOf(path), L"Rename", name)) return;
    wchar_t *s = cleanName(name, L"Rename");
    if (!s) return;
    if (wcschr(s, L'\\')) { MessageBoxW(npp._nppHandle, L"A new name can't contain \\ or /.", L"Rename", MB_ICONINFORMATION); return; }
    if (wcscmp(s, leafOf(path)) == 0) return;                                  /* unchanged */
    if (swprintf(target, MAX_PATH, L"%ls\\%ls", folder, s) < 0) return;
    bool caseOnly = _wcsicmp(target, path) == 0;
    if (!caseOnly && GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES) {
        swprintf(msg, sizeof msg / sizeof msg[0], L"\"%ls\" already exists in this folder.", s);
        MessageBoxW(npp._nppHandle, msg, L"Rename", MB_ICONINFORMATION);
        return;
    }
    if (!closeOpenDocs(path, dir)) return;                                     /* user kept a modified tab */
    int reopen = closedDocCount;
    static wchar_t reopenPaths[512][MAX_PATH];
    for (int i = 0; i < reopen; i++) wcscpy(reopenPaths[i], closedDocs[i]);

    if (!MoveFileExW(path, target, 0)) {
        swprintf(msg, sizeof msg / sizeof msg[0], L"Could not rename:\n%ls\n\nIs it open in another program?", path);
        MessageBoxW(npp._nppHandle, msg, L"Rename", MB_ICONERROR);
        for (int i = 0; i < reopen; i++) SendMessageW(npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)reopenPaths[i]);
        return;
    }
    size_t oldLen = wcslen(path);
    for (int i = 0; i < reopen; i++) {                                         /* same tabs, new paths */
        wchar_t moved[MAX_PATH];
        if (swprintf(moved, MAX_PATH, L"%ls%ls", target, reopenPaths[i] + oldLen) < 0) continue;
        if (!dir) runPanelRenamed(reopenPaths[i], moved);
        SendMessageW(npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)moved);
    }
    if (!dir && reopen == 0) runPanelRenamed(path, target);
}

/* ---- Open Folder (replaces File > Open Folder as Workspace / Ctrl+Shift+O) ----
   Windows' folder picker lists only folders, so a folder that holds only files looks empty.
   This dialog shows files too; "Open Folder" opens the folder you are in (or the one you picked). */
#define PICK_PLACEHOLDER L"(this folder)"

typedef struct { IFileDialogEventsVtbl *lpVtbl; } PickEvents;
static HRESULT STDMETHODCALLTYPE pe_QueryInterface(IFileDialogEvents *t, REFIID r, void **o)
{
    if (IsEqualIID(r, &IID_IUnknown) || IsEqualIID(r, &IID_IFileDialogEvents)) { *o = t; return S_OK; }
    *o = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pe_AddRef(IFileDialogEvents *t) { (void)t; return 1; }
static ULONG STDMETHODCALLTYPE pe_Release(IFileDialogEvents *t) { (void)t; return 1; }
static HRESULT STDMETHODCALLTYPE pe_OnFileOk(IFileDialogEvents *t, IFileDialog *d) { (void)t; (void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE pe_OnFolderChanging(IFileDialogEvents *t, IFileDialog *d, IShellItem *f) { (void)t; (void)d; (void)f; return S_OK; }
static HRESULT STDMETHODCALLTYPE pe_OnFolderChange(IFileDialogEvents *t, IFileDialog *d)
{
    (void)t;
    IFileDialog_SetFileName(d, PICK_PLACEHOLDER);                 /* after going into a folder, "Open Folder" opens it */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pe_OnSelectionChange(IFileDialogEvents *t, IFileDialog *d) { (void)t; (void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE pe_OnShareViolation(IFileDialogEvents *t, IFileDialog *d, IShellItem *i, FDE_SHAREVIOLATION_RESPONSE *r) { (void)t; (void)d; (void)i; *r = FDESVR_DEFAULT; return S_OK; }
static HRESULT STDMETHODCALLTYPE pe_OnTypeChange(IFileDialogEvents *t, IFileDialog *d) { (void)t; (void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE pe_OnOverwrite(IFileDialogEvents *t, IFileDialog *d, IShellItem *i, FDE_OVERWRITE_RESPONSE *r) { (void)t; (void)d; (void)i; *r = FDEOR_DEFAULT; return S_OK; }
static IFileDialogEventsVtbl pickEventsVtbl = { pe_QueryInterface, pe_AddRef, pe_Release, pe_OnFileOk, pe_OnFolderChanging,
                                                pe_OnFolderChange, pe_OnSelectionChange, pe_OnShareViolation, pe_OnTypeChange, pe_OnOverwrite };

static bool pickFolder(wchar_t *out)
{
    IFileOpenDialog *d = NULL;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **)&d))) return false;
    FILEOPENDIALOGOPTIONS o = 0;
    IFileOpenDialog_GetOptions(d, &o);
    IFileOpenDialog_SetOptions(d, (o | FOS_FORCEFILESYSTEM | FOS_NOVALIDATE | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR) & ~FOS_FILEMUSTEXIST);
    IFileOpenDialog_SetTitle(d, L"Open Folder");
    IFileOpenDialog_SetOkButtonLabel(d, L"Open Folder");
    IFileOpenDialog_SetFileNameLabel(d, L"Folder:");
    COMDLG_FILTERSPEC all = { L"All files", L"*.*" };
    IFileOpenDialog_SetFileTypes(d, 1, &all);
    IFileOpenDialog_SetFileName(d, PICK_PLACEHOLDER);
    wchar_t start[MAX_PATH] = L"";                                /* start in the open workspace, else the current file's folder */
    wchar_t roots[1][MAX_PATH];
    if (workspaceRoots(roots, 1)) wcscpy(start, roots[0]);
    else SendMessageW(npp._nppHandle, NPPM_GETCURRENTDIRECTORY, MAX_PATH, (LPARAM)start);
    IShellItem *si = NULL;
    if (start[0] && SUCCEEDED(SHCreateItemFromParsingName(start, NULL, &IID_IShellItem, (void **)&si))) { IFileOpenDialog_SetFolder(d, si); IShellItem_Release(si); }
    PickEvents ev = { &pickEventsVtbl };
    DWORD cookie = 0;
    IFileOpenDialog_Advise(d, (IFileDialogEvents *)&ev, &cookie);
    bool ok = false;
    if (SUCCEEDED(IFileOpenDialog_Show(d, npp._nppHandle))) {
        IShellItem *r = NULL;
        if (SUCCEEDED(IFileOpenDialog_GetResult(d, &r))) {
            PWSTR p = NULL;
            if (SUCCEEDED(IShellItem_GetDisplayName(r, SIGDN_FILESYSPATH, &p))) { wcsncpy(out, p, MAX_PATH - 1); out[MAX_PATH - 1] = 0; CoTaskMemFree(p); ok = true; }
            IShellItem_Release(r);
        }
    }
    IFileOpenDialog_Unadvise(d, cookie);
    IFileOpenDialog_Release(d);
    if (!ok) return false;
    if (!isDir(out)) { wchar_t *s = wcsrchr(out, L'\\'); if (s) *s = 0; }   /* a file or the placeholder: its folder */
    return isDir(out);
}

void openFolderCommand(void)
{
    wchar_t folder[MAX_PATH];
    if (pickFolder(folder)) openFolder(folder);
}

void openRecentFolder(const wchar_t *folder) { openFolder(folder); }

/* Welcome page "New Folder...": pick where, name it, create it and open it as the workspace. */
void createFolderCommand(void)
{
    wchar_t parent[MAX_PATH], name[MAX_PATH], full[MAX_PATH];
    if (!pickFolder(parent)) return;
    if (!askName(L"New Folder", L"Create a new folder in:", parent, name)) return;
    wchar_t *s = cleanName(name, L"New Folder");
    if (!s || swprintf(full, MAX_PATH, L"%ls\\%ls", parent, s) < 0) return;
    if (!isDir(full) && SHCreateDirectoryExW(NULL, full, NULL) != ERROR_SUCCESS) {
        wchar_t msg[MAX_PATH + 64];
        swprintf(msg, sizeof msg / sizeof msg[0], L"Could not create folder:\n%ls", full);
        MessageBoxW(npp._nppHandle, msg, L"New Folder", MB_ICONERROR);
        return;
    }
    openFolder(full);
}

static void deleteItem(HTREEITEM it)
{
    if (!fawTree || !it) return;
    if (!TreeView_GetParent(fawTree, it)) {
        MessageBoxW(npp._nppHandle, L"The top folder of the workspace can't be deleted from here.\n"
                    L"Use \"Remove\" to take it out of the panel instead.", L"Delete", MB_ICONINFORMATION);
        return;
    }
    wchar_t path[MAX_PATH + 2] = L"", msg[MAX_PATH + 160];
    if (!itemPath(it, path) || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(npp._nppHandle, L"Could not find that file on disk.", L"Delete", MB_ICONWARNING);
        return;
    }
    bool dir = isDir(path);
    swprintf(msg, sizeof msg / sizeof msg[0], L"Delete \"%ls\"%ls?\n\nIt will be moved to the Recycle Bin.",
             leafOf(path), dir ? L" and everything in it" : L"");
    if (MessageBoxW(npp._nppHandle, msg, L"Delete", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    if (!closeOpenDocs(path, dir)) return;

    path[wcslen(path) + 1] = 0;                          /* SHFileOperation wants a double-NUL list */
    SHFILEOPSTRUCTW op = { 0 };
    op.hwnd = npp._nppHandle;
    op.wFunc = FO_DELETE;
    op.pFrom = path;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted) {
        swprintf(msg, sizeof msg / sizeof msg[0], L"Could not delete:\n%ls\n\nIs it open in another program?", path);
        MessageBoxW(npp._nppHandle, msg, L"Delete", MB_ICONERROR);
    }
}

/* ---- toolbar buttons and right-click menu in the Folder as Workspace panel ---- */

/* Toolbar icons drawn in the panel's icon size: a page (or folder) with a plus.
   mirror: the panel's toolbar uses a right-to-left layout, which flips images. */
enum { ICON_FILE, ICON_FOLDER };
HICON makeNewItemIcon(int cx, int cy, bool dark, bool mirror, int kind)
{
    bool folder = kind == ICON_FOLDER;
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = cx; bi.bmiHeader.biHeight = -cy;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    DWORD *px = NULL;
    HBITMAP color = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0);
    if (!color) return NULL;
    memset(px, 0, cx * cy * 4);
    DWORD ink = dark ? 0xFFC8C8C8 : 0xFF404040;        /* premultiplied ARGB, opaque */
    DWORD plus = dark ? 0xFF8FD18F : 0xFF1E8C1E;       /* green plus */
    double k = cx / 16.0;
    int t = k >= 1.5 ? 2 : 1;
#define PX(x, y, c) do { int _x = (x), _y = (y); for (int a = 0; a < t; a++) for (int b = 0; b < t; b++) { \
        int _px = mirror ? cx - 1 - (_x + a) : _x + a; \
        if (_px >= 0 && _px < cx && _y + b >= 0 && _y + b < cy) px[(_y + b) * cx + _px] = (c); } } while (0)
    if (folder) {
        int x0 = (int)(1 * k), x1 = (int)(12 * k), y0 = (int)(3 * k), y1 = (int)(13 * k);
        int tab = (int)(5 * k), body = (int)(5 * k);
        for (int x = x0; x <= x0 + tab; x++) PX(x, y0, ink);              /* tab top */
        PX(x0 + tab + 1, y0 + 1, ink);                                     /* tab slope */
        for (int x = x0 + tab + 2; x <= x1; x++) PX(x, body, ink);         /* body top */
        for (int y = body; y <= y1; y++) PX(x1, y, ink);                  /* right */
        for (int x = x0; x <= x1; x++) PX(x, y1, ink);                    /* bottom */
        for (int y = y0; y <= y1; y++) PX(x0, y, ink);                    /* left */
    } else {
        int x0 = (int)(2 * k), x1 = (int)(11 * k), y0 = (int)(1 * k), y1 = (int)(14 * k), fold = (int)(3 * k);
        for (int x = x0; x <= x1 - fold; x++) PX(x, y0, ink);             /* top */
        for (int i = 0; i <= fold; i++) PX(x1 - fold + i, y0 + i, ink);    /* folded corner */
        for (int y = y0 + fold; y <= y1; y++) PX(x1, y, ink);             /* right */
        for (int x = x0; x <= x1; x++) PX(x, y1, ink);                    /* bottom */
        for (int y = y0; y <= y1; y++) PX(x0, y, ink);                    /* left */
    }
    int pcx = (int)(12 * k), pcy = (int)(11 * k), arm = (int)(3.5 * k);   /* plus, bottom-right */
    for (int i = -arm; i <= arm; i++) { PX(pcx + i, pcy, plus); PX(pcx, pcy + i, plus); }
#undef PX
    int maskBytes = ((cx + 15) / 16) * 2 * cy;                          /* 1bpp rows are WORD aligned */
    void *maskBits = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, maskBytes);
    HBITMAP mask = CreateBitmap(cx, cy, 1, 1, maskBits);
    ICONINFO ii = { TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    HeapFree(GetProcessHeap(), 0, maskBits);
    DeleteObject(color);
    return icon;
}

static HTREEITEM ctxItem = NULL;      /* item under the mouse at the last right-click */
static bool inContextMenu = false;    /* Notepad++ is showing its panel context menu */

/* Add our entries to Notepad++'s own panel context menu (once per menu). */
static void extendContextMenu(HMENU m)
{
    if (!inContextMenu) return;
    if (GetMenuState(m, IDC_CTX_DELETE, MF_BYCOMMAND) == (UINT)-1) {
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING, IDC_CTX_NEWFILE, L"New File...");
        AppendMenuW(m, MF_STRING, IDC_CTX_NEWFOLDER, L"New Folder...");
        AppendMenuW(m, MF_STRING, IDC_CTX_RENAME, L"Rename...\tF2");
        AppendMenuW(m, MF_STRING, IDC_CTX_DELETE, L"Delete\tDel");
    }
    bool isRoot = ctxItem && fawTree && !TreeView_GetParent(fawTree, ctxItem);   /* the workspace folder itself */
    EnableMenuItem(m, IDC_CTX_DELETE, MF_BYCOMMAND | (isRoot ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(m, IDC_CTX_RENAME, MF_BYCOMMAND | (isRoot ? MF_GRAYED : MF_ENABLED));
}

static bool handleOurCommand(UINT id)
{
    switch (id) {
    case IDC_NEWFILE_BTN:   newFileIn(NULL); return true;
    case IDC_NEWFOLDER_BTN: newFolderIn(NULL); return true;
    case IDC_CTX_NEWFILE:   newFileIn(ctxItem); return true;
    case IDC_CTX_NEWFOLDER: newFolderIn(ctxItem); return true;
    case IDC_CTX_DELETE:    deleteItem(ctxItem); return true;
    case IDC_CTX_RENAME:    renameItem(ctxItem); return true;
    }
    return false;
}

#define WM_OPEN_CLICKED (WM_APP + 0x51)
static wchar_t clickedFile[MAX_PATH];

/* Installed with SetWindowSubclass so it runs before Notepad++'s own dark-mode hook,
   which handles the tree's custom draw itself and never passes it on. */
static LRESULT CALLBACK panelProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    (void)id; (void)ref;
    if (msg == WM_COMMAND && HIWORD(w) == 0 && handleOurCommand(LOWORD(w))) return 0;
    if (msg == WM_INITMENUPOPUP) extendContextMenu((HMENU)w);
    if (msg == WM_SIZE) { LRESULT r = DefSubclassProc(h, msg, w, l); explorerLayout(); return r; }   /* after its own layout */
    if (msg == WM_CONTEXTMENU && (HWND)w == fawTree) {     /* the tree asks for its menu after NM_RCLICK */
        if (l == -1) ctxItem = TreeView_GetSelection(fawTree);   /* menu key / Shift+F10 */
        else {
            TVHITTESTINFO ht = { 0 };
            ht.pt.x = GET_X_LPARAM(l); ht.pt.y = GET_Y_LPARAM(l);
            ScreenToClient(fawTree, &ht.pt);
            HTREEITEM hit = TreeView_HitTest(fawTree, &ht);
            if (hit) ctxItem = hit;
        }
        inContextMenu = true;
        LRESULT r = DefSubclassProc(h, msg, w, l);
        inContextMenu = false;
        return r;
    }
    if (msg == WM_OPEN_CLICKED) {                           /* posted by a single click on a file */
        if (clickedFile[0]) SendMessageW(npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)clickedFile);
        clickedFile[0] = 0;
        return 0;
    }
    if (msg == WM_NOTIFY) {
        NMHDR *nh = (NMHDR *)l;
        if (nh->hwndFrom == fawTree && nh->code == NM_CUSTOMDRAW)          /* file-type icons, see explorer.c */
            return explorerCustomDraw((NMTVCUSTOMDRAW *)l, DefSubclassProc(h, msg, w, l));
        /* Like VS Code: one click opens a file, or opens / closes a folder (Notepad++ wants two) */
        if (nh->hwndFrom == fawTree && (nh->code == NM_CLICK || nh->code == NM_DBLCLK)) {
            TVHITTESTINFO ht = { 0 };
            DWORD pos = GetMessagePos();
            ht.pt.x = GET_X_LPARAM(pos); ht.pt.y = GET_Y_LPARAM(pos);
            ScreenToClient(fawTree, &ht.pt);
            HTREEITEM it = TreeView_HitTest(fawTree, &ht);
            if (it && !(ht.flags & TVHT_ONITEMBUTTON) && (ht.flags & (TVHT_ONITEM | TVHT_ONITEMRIGHT | TVHT_ONITEMINDENT))) {
                wchar_t path[MAX_PATH];
                bool folder = !TreeView_GetParent(fawTree, it) || TreeView_GetChild(fawTree, it)
                              || (itemPath(it, path) && isDir(path));
                if (nh->code == NM_DBLCLK) {
                    if (folder) return 1;                           /* the first click already toggled it */
                } else if (folder) {
                    TreeView_SelectItem(fawTree, it);
                    TreeView_Expand(fawTree, it, TVE_TOGGLE);
                    return 1;
                } else if (itemPath(it, path)) {
                    TreeView_SelectItem(fawTree, it);
                    wcscpy(clickedFile, path);
                    PostMessageW(h, WM_OPEN_CLICKED, 0, 0);         /* after the click is over */
                    return 1;                                       /* no rename-on-click either */
                }
            }
        }
        if (nh->hwndFrom == fawTree && nh->code == NM_RCLICK) {
            TVHITTESTINFO ht = { 0 };
            GetCursorPos(&ht.pt);
            ScreenToClient(fawTree, &ht.pt);
            ctxItem = TreeView_HitTest(fawTree, &ht);
            inContextMenu = true;                          /* Notepad++ shows its menu inside this call */
            LRESULT r = DefSubclassProc(h, msg, w, l);
            inContextMenu = false;
            return r;
        }
        if (nh->hwndFrom == fawTree && nh->code == TVN_KEYDOWN && ((NMTVKEYDOWN *)l)->wVKey == VK_DELETE) {
            ctxItem = TreeView_GetSelection(fawTree);
            deleteItem(ctxItem);
            return 1;
        }
        if (nh->hwndFrom == fawTree && nh->code == TVN_KEYDOWN && ((NMTVKEYDOWN *)l)->wVKey == VK_F2) {   /* if F2 is not bound elsewhere */
            ctxItem = TreeView_GetSelection(fawTree);
            renameItem(ctxItem);
            return 1;
        }
    }
    return DefSubclassProc(h, msg, w, l);
}

/* ---- error messages next to code: keep the red underline, hide the inline text ---- */
#define SCI_EOLANNOTATIONSETVISIBLE 2745
#define SCI_EOLANNOTATIONGETVISIBLE 2746
static WNDPROC oldSciProc[2];

static LRESULT CALLBACK sciProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == SCI_EOLANNOTATIONSETVISIBLE) w = 0;          /* EOLANNOTATION_HIDDEN */
    return CallWindowProcW(h == npp._scintillaMainHandle ? oldSciProc[0] : oldSciProc[1], h, msg, w, l);
}

static void hideInlineDiagnostics(void)
{
    HWND sci[2] = { npp._scintillaMainHandle, npp._scintillaSecondHandle };
    for (int i = 0; i < 2; i++) {
        if (!sci[i]) continue;
        if (!oldSciProc[i]) oldSciProc[i] = (WNDPROC)SetWindowLongPtrW(sci[i], GWLP_WNDPROC, (LONG_PTR)sciProc);
        if (SendMessageW(sci[i], SCI_EOLANNOTATIONGETVISIBLE, 0, 0) != 0)      /* set via direct calls too */
            SendMessageW(sci[i], SCI_EOLANNOTATIONSETVISIBLE, 0, 0);
        /* The LSP plugin marks errors with INDIC_POINT (a small triangle under the error);
           show them as a squiggly underline instead (its per-severity colors are kept). */
        for (int ind = 8; ind <= 35; ind++) {
            LRESULT st = SendMessageW(sci[i], 2081 /* SCI_INDICGETSTYLE */, ind, 0);
            if (st == 18 /* INDIC_POINT */ || st == 19 /* INDIC_POINTCHARACTER */ || st == 22 /* INDIC_POINT_TOP */)
                SendMessageW(sci[i], 2080 /* SCI_INDICSETSTYLE */, ind, 1 /* INDIC_SQUIGGLE */);
        }
    }
}

static BOOL CALLBACK findPanelChild(HWND h, LPARAM l)
{
    wchar_t cls[32], text[32];
    GetClassNameW(h, cls, 32);
    if (wcscmp(cls, L"#32770") == 0 && GetWindowTextW(h, text, 32) && wcscmp(text, L"File Browser") == 0) {
        *(HWND *)l = h;
        return FALSE;
    }
    return TRUE;
}

static BOOL CALLBACK findPanelTop(HWND h, LPARAM l)
{
    if (!findPanelChild(h, l)) return FALSE;
    EnumChildWindows(h, findPanelChild, l);
    return *(HWND *)l == NULL;
}

/* Called every second: restyle the Folder as Workspace panel when it exists (explorer.c). */
static void hookWorkspacePanel(void)
{
    HWND panel = NULL;
    EnumChildWindows(npp._nppHandle, findPanelChild, (LPARAM)&panel);
    if (!panel) EnumThreadWindows(GetCurrentThreadId(), findPanelTop, (LPARAM)&panel);   /* floating panel */
    if (!panel) return;

    HWND tb = FindWindowExW(panel, NULL, L"ToolbarWindow32", NULL);
    fawTree = FindWindowExW(panel, NULL, L"SysTreeView32", NULL);
    if (!tb) return;

    if (panel != fawPanel) {
        fawPanel = panel;
        SetWindowSubclass(panel, panelProc, PANEL_SUBCLASS_ID, 0);
    }
    explorerAttach(panel, tb, fawTree);                           /* VS Code style header, icons, empty view */
}

/* Switch the Folder as Workspace panel to one folder without restarting Notepad++:
   remove the current roots, then hand the folder to Notepad++ as a drag-and-drop, which it
   opens in the panel (with "drop a folder to open all files" off, the default).
   false = didn't work (panel shows something else); the caller then reopens Notepad++. */
static bool openFolderInPlace(const wchar_t *folder)
{
    if (!isDir(folder)) return false;
    wchar_t cur[MAX_PATH];
    bool hasCur = currentWorkspace(cur);
    if (hasCur && _wcsicmp(cur, folder) == 0) return true;        /* already open */
    if (hasCur) stashFiles(cur, true, false);                     /* save this folder's files; closed once the new ones are open */
    filesStashed = hasCur;
    HWND panel = fawPanelWindow();
    if (panel && fawTree)
        for (int guard = 0; guard < 32; guard++) {
            HTREEITEM root = TreeView_GetRoot(fawTree);
            if (!root) break;
            TreeView_SelectItem(fawTree, root);
            SendMessageW(panel, WM_COMMAND, IDM_FILEBROWSER_REMOVEROOT, 0);
            if (TreeView_GetRoot(fawTree) == root) break;
        }
    size_t len = wcslen(folder);
    HGLOBAL g = GlobalAlloc(GHND, sizeof(DROPFILES) + (len + 2) * sizeof(wchar_t));   /* zeroed: list ends in two NULs */
    if (!g) return false;
    DROPFILES *df = (DROPFILES *)GlobalLock(g);
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    wcscpy((wchar_t *)((BYTE *)df + sizeof(DROPFILES)), folder);
    GlobalUnlock(g);
    SendMessageW(npp._nppHandle, WM_DROPFILES, (WPARAM)g, 0);    /* Notepad++ frees it (DragFinish) */
    hookWorkspacePanel();                                         /* the panel may have just been created */
    if (!fawTree || !IsWindow(fawTree)) return false;
    HTREEITEM root = TreeView_GetRoot(fawTree);
    wchar_t text[MAX_PATH] = L"";
    TVITEMW tv = { 0 };
    tv.mask = TVIF_TEXT; tv.hItem = root; tv.pszText = text; tv.cchTextMax = MAX_PATH;
    if (!root || !TreeView_GetItem(fawTree, &tv) || _wcsicmp(text, leafOf(folder)) != 0) return false;
    TreeView_Expand(fawTree, root, TVE_EXPAND);                   /* like VS Code: show the folder's files */
    filesStashed = false;
    restoreFiles(folder);                                         /* the files open there last time */
    closeStashed();
    explorerRefresh();
    return true;
}

/* Tabs backed by a file (not untitled "new 1"), in both views. */
static int openFileCount(void)
{
    int n = 0;
    wchar_t path[MAX_PATH];
    for (int view = 0; view < 2; view++) {
        int nb = (int)SendMessageW(npp._nppHandle, NPPM_GETNBOPENFILES, 0, view == 0 ? 1 : 2);   /* PRIMARY / SECOND_VIEW */
        for (int pos = 0; pos < nb; pos++) {
            LRESULT id = SendMessageW(npp._nppHandle, NPPM_GETBUFFERIDFROMPOS, pos, view);
            if (id && SendMessageW(npp._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, id, (LPARAM)path) > 0 && wcschr(path, L'\\')) n++;
        }
    }
    return n;
}

/* Once Notepad++ has started and the panel shows its folder: expand the folder, and open the
   files remembered for it when started with that folder (notepad++ -openFoldersAsWorkspace,
   e.g. the folder picker's fallback: replaces the last session's files) or when nothing but an
   empty "new 1" is open. */
static int startupSwapTicks;
static bool startedWithFolder;

static void startupSwap(void)
{
    wchar_t cur[MAX_PATH], file[MAX_PATH];
    if (!startupSwapTicks) return;
    hookWorkspacePanel();
    if (!currentWorkspace(cur)) { startupSwapTicks--; return; }
    startupSwapTicks = 0;
    TreeView_Expand(fawTree, TreeView_GetRoot(fawTree), TVE_EXPAND);   /* like VS Code: show the folder's files */
    explorerRefresh();
    if (!sessionFileFor(cur, file) || GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES) return;   /* nothing saved yet */
    if (startedWithFolder) {
        stashFiles(NULL, true, false);
        restoreFiles(cur);
        closeStashed();
    } else if (openFileCount() == 0) restoreFiles(cur);
}

#define COPYDATA_OPEN_FOLDER 0x4E505046   /* 'NPPF': recent-folders.ps1 asks this window to open a folder */

/* ================= menus ================= */

static UINT lspRenameCmd;   /* NppLspClient's "Rename" (F2): in the folder panel, F2 renames the file instead */
static HMENU lspMenu;       /* Plugins > NppLspClient: its commands for the VS Code keys (keyHookProc) */
static UINT pickerRunCmd;   /* Run > "Recent Folders..." (Ctrl+Shift+R): shows our built-in picker instead of PowerShell's */
#ifdef SNIP_DEBUG
static bool expandSnippet(HWND ed);
#endif

static LRESULT CALLBACK nppProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
#ifdef SNIP_DEBUG
    if (msg == WM_APP + 0x60) return expandSnippet(npp._scintillaMainHandle);   /* tests only: Tab without a keyboard */
#endif
    /* The strips between panels that nothing covers yet while an edge is dragged (see welcome.c):
       in dark mode, the dark panel background instead of a light grey flash. */
    if (msg == WM_ERASEBKGND && SendMessageW(h, NPPM_ISDARKMODEENABLED, 0, 0)) {
        COLORREF c[12] = { 0 };                                     /* NppDarkMode::Colors, [3] = pure background */
        COLORREF back = SendMessageW(h, NPPMSG + 108 /* NPPM_GETDARKMODECOLORS */, sizeof c, (LPARAM)c) ? c[3] : RGB(0x0d, 0x11, 0x17);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH b = CreateSolidBrush(back);
        FillRect((HDC)w, &rc, b);                                   /* the children are clipped out */
        DeleteObject(b);
        return 1;
    }
    if (msg == WM_INITMENUPOPUP && (HMENU)w == recentMenu) rebuildMenu();
    if (msg == WM_INITMENUPOPUP) extendContextMenu((HMENU)w);         /* in case the menu's owner is the main window */
    if (msg == WM_COMMAND && HIWORD(w) == 0 && LOWORD(w) >= IDC_CTX_NEWFILE && LOWORD(w) <= IDC_CTX_RENAME
        && handleOurCommand(LOWORD(w))) return 0;
    if (msg == WM_COMMAND && LOWORD(w) == IDM_FILE_OPENFOLDERASWORKSPACE) { openFolderCommand(); return 0; }
    if (msg == WM_COMMAND && LOWORD(w) == 41001 /* IDM_FILE_NEW: Ctrl+N, toolbar, menu */ && welcomeNewFileRequested()) return 0;
    if (msg == WM_COMMAND && LOWORD(w) == 41003 /* IDM_FILE_CLOSE: Ctrl+W */
            && SendMessageW(h, NPPMSG + 7 /* NPPM_GETNBOPENFILES */, 0, 1 /* PRIMARY_VIEW */) == 1) welcomeCloseRequested();
    if (msg == WM_COMMAND && pickerRunCmd && LOWORD(w) == pickerRunCmd) { recentFolderPicker(); return 0; }
    if (msg == WM_COPYDATA && ((COPYDATASTRUCT *)l)->dwData == COPYDATA_OPEN_FOLDER) {
        COPYDATASTRUCT *cd = (COPYDATASTRUCT *)l;
        wchar_t folder[MAX_PATH] = L"";
        size_t n = cd->cbData / sizeof(wchar_t);
        if (n >= MAX_PATH) n = MAX_PATH - 1;
        wmemcpy(folder, (const wchar_t *)cd->lpData, n);
        folder[n] = 0;
        if (!openFolderInPlace(folder)) return FALSE;              /* the script reopens Notepad++ */
        rememberFolder(folder);
        return TRUE;
    }
    if (msg == WM_COMMAND && lspRenameCmd && LOWORD(w) == lspRenameCmd && fawTree && GetFocus() == fawTree) {
        ctxItem = TreeView_GetSelection(fawTree);
        renameItem(ctxItem);
        return 0;
    }
    if (msg == WM_TIMER && w == TIMER_ID) { hookWorkspacePanel(); hideInlineDiagnostics(); syncExplorerButton(); return 0; }
    if (msg == WM_TIMER && w == STARTUP_TIMER_ID) { startupSwap(); if (!startupSwapTicks) KillTimer(h, STARTUP_TIMER_ID); return 0; }
    if (msg == WM_TIMER && w == LISTONLY_TIMER_ID) { KillTimer(h, LISTONLY_TIMER_ID); runScript(L"-ListOnly"); return 0; }
    if (msg == WM_TIMER && w == FIRSTRUN_TIMER_ID) { KillTimer(h, FIRSTRUN_TIMER_ID); openToolsWindow(true); return 0; }
    if (msg == WM_TIMER && w == WARMUP_TIMER_ID) { KillTimer(h, WARMUP_TIMER_ID); warmUpRunner(); return 0; }
    return CallWindowProcW(oldNppProc, h, msg, w, l);
}

static int menuPosOf(HMENU m, UINT id)
{
    int count = GetMenuItemCount(m);
    for (int i = 0; i < count; i++) if (GetMenuItemID(m, i) == id) return i;
    return -1;
}

/* File menu: "Open Recent Folder >" right under "Open Folder as Workspace...", and
   "New File in Folder..." just above Exit; then drop our entry from the Plugins menu.
   Notepad++ keeps its recent-files section at a fixed menu position, so the number of
   items above it must not change (otherwise Print Now, the recent files, ... disappear).
   Open Recent Folder therefore takes the slot of "Open in Default Viewer", which moves
   down next to New File in Folder. */
static void installMenu(void)
{
    HMENU mainMenu = (HMENU)SendMessageW(npp._nppHandle, NPPM_GETMENUHANDLE, NPPMAINMENU, 0);
    HMENU fileMenu = mainMenu ? GetSubMenu(mainMenu, 0) : NULL;
    if (!fileMenu) return;
    recentMenu = CreatePopupMenu();

    wchar_t viewerText[128] = L"";
    UINT viewerState = 0;
    int viewer = menuPosOf(fileMenu, IDM_FILE_OPEN_DEFAULT_VIEWER);
    bool swapped = viewer >= 0 && menuPosOf(fileMenu, IDM_FILE_OPENFOLDERASWORKSPACE) >= 0
                   && GetMenuStringW(fileMenu, viewer, viewerText, 128, MF_BYPOSITION) > 0;
    if (swapped) {
        viewerState = GetMenuState(fileMenu, viewer, MF_BYPOSITION) & (MF_GRAYED | MF_DISABLED);
        RemoveMenu(fileMenu, viewer, MF_BYPOSITION);
        InsertMenuW(fileMenu, menuPosOf(fileMenu, IDM_FILE_OPENFOLDERASWORKSPACE) + 1,
                    MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)recentMenu, L"Open Recent Folder");
    }

    int pos = menuPosOf(fileMenu, IDM_FILE_EXIT);
    if (pos < 0) pos = GetMenuItemCount(fileMenu);
    InsertMenuW(fileMenu, pos++, MF_BYPOSITION | MF_STRING, funcs[FN_NEWWINDOW]._cmdID, L"New Window\tCtrl+Shift+N");
    InsertMenuW(fileMenu, pos++, MF_BYPOSITION | MF_STRING, funcs[FN_NEW]._cmdID, L"New File in Folder...\tCtrl+Alt+N");
    InsertMenuW(fileMenu, pos++, MF_BYPOSITION | MF_STRING, funcs[FN_CLOSEFOLDER]._cmdID, L"Close Folder");
    if (swapped)
        InsertMenuW(fileMenu, pos++, MF_BYPOSITION | MF_STRING | viewerState, IDM_FILE_OPEN_DEFAULT_VIEWER, viewerText);
    else    /* unexpected menu layout: keep everything below the recent-files section */
        InsertMenuW(fileMenu, pos++, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)recentMenu, L"Open Recent Folder");
    InsertMenuW(fileMenu, pos, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    rebuildMenu();

    /* Plugins > Dev Tools keeps the real commands; the 10 recent-folder slots only live in File > Open Recent Folder */
    HMENU pluginMenu = (HMENU)SendMessageW(npp._nppHandle, NPPM_GETMENUHANDLE, NPPPLUGINMENU, 0);
    if (pluginMenu) {
        int n = GetMenuItemCount(pluginMenu);
        for (int i = 0; i < n; i++) {
            wchar_t name[128] = L"";
            HMENU sub = GetSubMenu(pluginMenu, i);
            if (!sub) continue;
            GetMenuStringW(pluginMenu, i, name, 128, MF_BYPOSITION);
            if (wcscmp(name, PLUGIN_NAME) == 0)
                for (int k = 0; k < MAX_SLOTS; k++) DeleteMenu(sub, funcs[k]._cmdID, MF_BYCOMMAND);
            if (wcscmp(name, L"NppLspClient") == 0) lspMenu = sub;          /* VS Code keys (keyHookProc) */
            if (wcscmp(name, L"NppLspClient") == 0)                          /* find its "Rename" command */
                for (int k = 0; k < GetMenuItemCount(sub); k++) {
                    wchar_t item[64] = L"";
                    if (GetMenuStringW(sub, k, item, 64, MF_BYPOSITION) && wcsncmp(item, L"Rename", 6) == 0) lspRenameCmd = GetMenuItemID(sub, k);
                }
        }
    }
    for (int i = 0; i < GetMenuItemCount(mainMenu) && !pickerRunCmd; i++) {   /* Run > Recent Folders... */
        HMENU sub = GetSubMenu(mainMenu, i);
        for (int k = 0; sub && k < GetMenuItemCount(sub); k++) {
            wchar_t item[64] = L"";
            if (GetMenuStringW(sub, k, item, 64, MF_BYPOSITION) && wcsncmp(item, L"Recent Folders...", 17) == 0) pickerRunCmd = GetMenuItemID(sub, k);
        }
    }
    oldNppProc = (WNDPROC)SetWindowLongPtrW(npp._nppHandle, GWLP_WNDPROC, (LONG_PTR)nppProc);
    SetTimer(npp._nppHandle, TIMER_ID, 1000, NULL);
    hookWorkspacePanel();
    hideInlineDiagnostics();
}

/* ---- small editing commands (keys in NppDev only, see FN_DUPLINE) ---- */
#define SCI_LINEDUPLICATE      2404
#define SCI_LINEDOWNRECTEXTEND 2426    /* what the "Add Cursor" macros did */
#define SCI_LINEUPRECTEXTEND   2427
#define IDM_EDIT_TRIMTRAILING  42024

static void __cdecl duplicateLine(void)  { SendMessageW(currentEditor(), SCI_LINEDUPLICATE, 0, 0); }
static void __cdecl addCursorAbove(void) { SendMessageW(currentEditor(), SCI_LINEUPRECTEXTEND, 0, 0); }
static void __cdecl addCursorBelow(void) { SendMessageW(currentEditor(), SCI_LINEDOWNRECTEXTEND, 0, 0); }
static void __cdecl trimAndSave(void)
{
    SendMessageW(npp._nppHandle, WM_COMMAND, IDM_EDIT_TRIMTRAILING, 0);
    SendMessageW(npp._nppHandle, NPPM_SAVECURRENTFILE, 0, 0);
}

/* The Check / Install Tools window (devtools\tools-setup.ps1 [args]). With notify, msg is posted
   to it when the window closes, with its exit code (0 = the asked-for tools are installed). */
typedef struct { HANDLE proc; HWND notify; UINT msg; } ToolsWait;

static DWORD WINAPI waitForTools(LPVOID p)
{
    ToolsWait *tw = p;
    DWORD code = 1;
    WaitForSingleObject(tw->proc, INFINITE);
    GetExitCodeProcess(tw->proc, &code);
    CloseHandle(tw->proc);
    if (IsWindow(tw->notify)) PostMessageW(tw->notify, tw->msg, code, 0);
    free(tw);
    return 0;
}

bool runToolsWindow(const wchar_t *args, HWND notify, UINT msg)
{
    wchar_t cmd[2 * MAX_PATH + 200];
    swprintf(cmd, sizeof cmd / sizeof cmd[0],
             L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"%ls\\tools-setup.ps1\" %ls",
             devtoolsDir, args ? args : L"");
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    ToolsWait *tw = notify ? malloc(sizeof *tw) : NULL;
    HANDLE t = NULL;
    if (tw) {
        *tw = (ToolsWait){ pi.hProcess, notify, msg };
        t = CreateThread(NULL, 0, waitForTools, tw, 0, NULL);
    }
    if (t) CloseHandle(t);
    else { CloseHandle(pi.hProcess); free(tw); }
    return true;
}

static void openToolsWindow(bool startup) { runToolsWindow(startup ? L"-StartupCheck" : L"", NULL, 0); }

static void __cdecl checkTools(void) { openToolsWindow(false); }

/* runner.py --warmup, in the background at low priority: precompiles <bits/stdc++.h> once per
   compiler, so C++ runs don't spend ~2 s re-reading it (nothing happens when it's already done). */
static void warmUpRunner(void)
{
    wchar_t cmd[3 * MAX_PATH + 64], pyrun[MAX_PATH];
    swprintf(pyrun, MAX_PATH, L"%ls\\pyrun.exe", devtoolsDir);
    if (GetFileAttributesW(pyrun) == INVALID_FILE_ATTRIBUTES) return;
    swprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" \"%ls\\runner.py\" --warmup", pyrun, devtoolsDir);
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}


/* ---- Color Theme: Dark / Light ----
   Notepad++ has no message to switch dark mode, so this uses its own switch: the "Light mode" /
   "Dark mode" choice on the Dark Mode page of Preferences, with the dialog kept out of sight. Notepad++
   then switches everything, including the editor theme it remembers for each mode. */
#define IDM_SETTING_PREFERENCE 48011
#define IDC_RADIO_LIGHTMODE    7131
#define IDC_RADIO_DARKMODE     7132
#define IDC_RADIO_FOLLOWWIN    7133

static HWND foundChild;
static BOOL CALLBACK findButtonId(HWND h, LPARAM id)
{
    wchar_t cls[16] = L"";
    if (GetDlgCtrlID(h) == (int)id && GetClassNameW(h, cls, 16) && _wcsicmp(cls, L"Button") == 0) { foundChild = h; return FALSE; }
    return TRUE;
}

static HWND foundPref;
static BOOL CALLBACK findPrefDialog(HWND h, LPARAM l)
{
    (void)l;
    wchar_t cls[16] = L"";
    if (!GetClassNameW(h, cls, 16) || wcscmp(cls, L"#32770") != 0) return TRUE;
    foundChild = NULL;
    EnumChildWindows(h, findButtonId, IDC_RADIO_DARKMODE);
    if (foundChild) { foundPref = h; return FALSE; }
    return TRUE;
}

/* While Preferences is being created: make it a layered window with no content, so it never shows. */
static HHOOK cbtHook;
static HWND hiddenPref;
static LRESULT CALLBACK hidePrefProc(int code, WPARAM w, LPARAM l)
{
    if (code == HCBT_CREATEWND && !hiddenPref) {
        CREATESTRUCTW *cs = ((CBT_CREATEWNDW *)l)->lpcs;
        if (!(cs->style & WS_CHILD)) { cs->dwExStyle |= WS_EX_LAYERED; hiddenPref = (HWND)w; }
    }
    return CallNextHookEx(cbtHook, code, w, l);
}

static void setColorTheme(bool dark)
{
    if ((SendMessageW(npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0) == dark) return;   /* already */
    foundPref = NULL;
    EnumThreadWindows(GetCurrentThreadId(), findPrefDialog, 0);
    bool created = false;
    if (!foundPref) {                                               /* first time: Notepad++ creates it */
        hiddenPref = NULL;
        cbtHook = SetWindowsHookExW(WH_CBT, hidePrefProc, NULL, GetCurrentThreadId());
        SendMessageW(npp._nppHandle, WM_COMMAND, IDM_SETTING_PREFERENCE, 0);
        if (cbtHook) UnhookWindowsHookEx(cbtHook);
        cbtHook = NULL;
        EnumThreadWindows(GetCurrentThreadId(), findPrefDialog, 0);
        created = true;
    }
    if (!foundPref) {
        MessageBoxW(npp._nppHandle, L"Choose the theme in Settings > Preferences > Dark Mode.", L"Color Theme", MB_ICONINFORMATION);
        return;
    }
    bool wasShown = !created && IsWindowVisible(foundPref);
    foundChild = NULL;
    EnumChildWindows(foundPref, findButtonId, dark ? IDC_RADIO_DARKMODE : IDC_RADIO_LIGHTMODE);
    HWND radio = foundChild, page = radio ? GetParent(radio) : NULL;
    if (page) {
        CheckDlgButton(page, IDC_RADIO_LIGHTMODE, dark ? BST_UNCHECKED : BST_CHECKED);
        CheckDlgButton(page, IDC_RADIO_DARKMODE, dark ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(page, IDC_RADIO_FOLLOWWIN, BST_UNCHECKED);
        SendMessageW(page, WM_COMMAND, MAKEWPARAM(dark ? IDC_RADIO_DARKMODE : IDC_RADIO_LIGHTMODE, BN_CLICKED), (LPARAM)radio);
    }
    if (!wasShown) ShowWindow(foundPref, SW_HIDE);
    if (created && foundPref == hiddenPref)                         /* a normal window again for next time */
        SetWindowLongPtrW(foundPref, GWL_EXSTYLE, GetWindowLongPtrW(foundPref, GWL_EXSTYLE) & ~WS_EX_LAYERED);
    hiddenPref = NULL;
}

static void colorThemeDark(void) { setColorTheme(true); }
static void colorThemeLight(void) { setColorTheme(false); }

/* Tick the current one in Plugins > Dev Tools. */
static void markColorTheme(void)
{
    bool dark = SendMessageW(npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0;
    SendMessageW(npp._nppHandle, NPPM_SETMENUITEMCHECK, funcs[FN_THEMEDARK]._cmdID, dark);
    SendMessageW(npp._nppHandle, NPPM_SETMENUITEMCHECK, funcs[FN_THEMELIGHT]._cmdID, !dark);
}

/* ---- Toggle Explorer: a toolbar button (pressed while the Explorer shows), Ctrl+B, the Command
   Palette. Showing it again brings back the folder that was open. ---- */
static wchar_t lastFolder[MAX_PATH];

static bool explorerShown(void)
{
    HWND p = fawPanelWindow();
    return p && IsWindowVisible(p);
}

static void ensureExplorerButton(void);

/* Every second and after a toggle: keep the button state and the last open folder up to date. */
static void syncExplorerButton(void)
{
    static int last = -1;
    int shown = explorerShown();
    wchar_t cur[MAX_PATH];
    if (shown && currentWorkspace(cur)) wcscpy(lastFolder, cur);
    if (shown != last && funcs[FN_EXPLORER]._cmdID) {
        last = shown;
        SendMessageW(npp._nppHandle, NPPM_SETMENUITEMCHECK, funcs[FN_EXPLORER]._cmdID, shown);
    }
    ensureExplorerButton();
}

static void toggleExplorer(void)
{
    if (explorerShown()) {
        syncExplorerButton();                                       /* note the folder first */
        SendMessageW(npp._nppHandle, WM_COMMAND, IDM_VIEW_FILEBROWSER, 0);
    } else {
        SendMessageW(npp._nppHandle, WM_COMMAND, IDM_VIEW_FILEBROWSER, 0);
        hookWorkspacePanel();
        if (fawTree && !TreeView_GetRoot(fawTree) && lastFolder[0] && isDir(lastFolder)) openFolderInPlace(lastFolder);
        explorerRefresh();
        if (fawTree && IsWindowVisible(fawTree)) SetFocus(fawTree);
    }
    syncExplorerButton();
}

/* The button's picture, VS Code's Explorer icon: two pages. */
static void drawFilesGlyph(HDC dc, int s, COLORREF ink)
{
    HPEN pen = CreatePen(PS_SOLID, s >= 24 ? s / 14 : 1, ink);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
#define P(x, y) { (x) * s / 16, (y) * s / 16 }
    POINT back[] = { P(6, 3), P(6, 1), P(11, 1), P(14, 4), P(14, 11), P(11, 11) };
    POINT front[] = { P(2, 5), P(7, 5), P(10, 8), P(10, 15), P(2, 15), P(2, 5) };
    POINT fold[] = { P(7, 5), P(7, 8), P(10, 8) };
#undef P
    Polyline(dc, back, 6);
    Polyline(dc, front, 6);
    Polyline(dc, fold, 3);
    SelectObject(dc, op); SelectObject(dc, ob);
    DeleteObject(pen);
}

static HICON glyphIcon(int s, COLORREF ink)
{
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = s; bi.bmiHeader.biHeight = -s;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!color) { DeleteDC(dc); return NULL; }
    HGDIOBJ old = SelectObject(dc, color);
    memset(bits, 0, (size_t)s * s * 4);
    drawFilesGlyph(dc, s, RGB(255, 255, 255));                     /* white on black = the alpha */
    GdiFlush();
    DWORD *px = (DWORD *)bits;
    for (int i = 0; i < s * s; i++) {
        DWORD a = px[i] & 0xFF;
        px[i] = (a << 24) | ((DWORD)GetRValue(ink) << 16) | ((DWORD)GetGValue(ink) << 8) | GetBValue(ink);
    }
    SelectObject(dc, old);
    DeleteDC(dc);
    int stride = ((s + 15) / 16) * 2;
    void *zero = calloc((size_t)stride, (size_t)s);
    HBITMAP mask = CreateBitmap(s, s, 1, 1, zero);
    free(zero);
    ICONINFO ii = { TRUE, 0, 0, mask, color };
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return ic;
}

/* The Explorer button, first on Notepad++'s toolbar (where VS Code has it). Put there directly:
   Notepad++ 8.9 doesn't show buttons registered with NPPM_ADDTOOLBARICON_FORDARKMODE here. Notepad++
   rebuilds its toolbar when the theme or icon set changes, so this is checked every second. */
static void ensureExplorerButton(void)
{
    UINT cmd = funcs[FN_EXPLORER]._cmdID;
    HWND rebar = FindWindowExW(npp._nppHandle, NULL, L"ReBarWindow32", NULL);
    HWND tb = rebar ? FindWindowExW(rebar, NULL, L"ToolbarWindow32", NULL) : NULL;
    if (!tb || !cmd) return;
    if (SendMessageW(tb, TB_COMMANDTOINDEX, cmd, 0) >= 0) {
        SendMessageW(tb, TB_CHECKBUTTON, cmd, MAKELONG(explorerShown(), 0));
        return;
    }
    HIMAGELIST il = (HIMAGELIST)SendMessageW(tb, TB_GETIMAGELIST, 0, 0);
    if (!il) return;
    int cx = 16, cy = 16;
    ImageList_GetIconSize(il, &cx, &cy);
    bool dark = SendMessageW(npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0;
    HICON ic = glyphIcon(cx, dark ? RGB(0xC5, 0xCB, 0xD3) : RGB(0x33, 0x33, 0x33));
    if (!ic) return;
    int img = ImageList_AddIcon(il, ic);
    HIMAGELIST dis = (HIMAGELIST)SendMessageW(tb, TB_GETDISABLEDIMAGELIST, 0, 0);
    if (dis && ImageList_GetImageCount(dis) == img) ImageList_AddIcon(dis, ic);
    HIMAGELIST hot = (HIMAGELIST)SendMessageW(tb, TB_GETHOTIMAGELIST, 0, 0);
    if (hot && ImageList_GetImageCount(hot) == img) ImageList_AddIcon(hot, ic);
    DestroyIcon(ic);
    if (img < 0) return;
    TBBUTTON b[2] = { { 0 }, { 0 } };
    b[0].iBitmap = img; b[0].idCommand = (int)cmd; b[0].fsState = TBSTATE_ENABLED | (explorerShown() ? TBSTATE_CHECKED : 0);
    b[0].fsStyle = BTNS_BUTTON | BTNS_CHECK;
    b[1].fsStyle = BTNS_SEP;
    SendMessageW(tb, TB_INSERTBUTTONW, 0, (LPARAM)&b[1]);           /* separator, then the button before it */
    SendMessageW(tb, TB_INSERTBUTTONW, 0, (LPARAM)&b[0]);
    SendMessageW(tb, TB_AUTOSIZE, 0, 0);
    SIZE sz = { 0, 0 };
    SendMessageW(tb, TB_GETMAXSIZE, 0, (LPARAM)&sz);
    int bands = (int)SendMessageW(rebar, RB_GETBANDCOUNT, 0, 0);
    for (int i = 0; i < bands; i++) {                               /* make room for it in the rebar */
        REBARBANDINFOW bi = { sizeof bi };
        bi.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_IDEALSIZE | RBBIM_SIZE;
        if (!SendMessageW(rebar, RB_GETBANDINFOW, i, (LPARAM)&bi) || bi.hwndChild != tb) continue;
        bi.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE | RBBIM_SIZE;
        bi.cxMinChild = sz.cx; bi.cxIdeal = sz.cx; bi.cx = sz.cx;
        SendMessageW(rebar, RB_SETBANDINFOW, i, (LPARAM)&bi);
    }
}

/* ---- exported plugin API ---- */
static void cleanStaleWebViewFolders(void);

/* Parent folder of a path, n levels up (in place). */
static void upDirs(wchar_t *p, int n)
{
    while (n-- > 0) { wchar_t *s = wcsrchr(p, L'\\'); if (s) *s = 0; }
}

__declspec(dllexport) void setInfo(NppData data)
{
    npp = data;
    cleanStaleWebViewFolders();
    wchar_t home[MAX_PATH], probe[MAX_PATH], cfg[MAX_PATH] = L"";
    if (!GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH)) wcscpy(home, L"C:\\Users\\Public");
    wcscpy(homeDir, home);
    /* NppDev: <app>\plugins\RecentFolders\RecentFolders.dll with the helpers in <app>\devtools */
    GetModuleFileNameW(hInst, devtoolsDir, MAX_PATH);
    upDirs(devtoolsDir, 3);
    wcscat(devtoolsDir, L"\\devtools");
    swprintf(probe, MAX_PATH, L"%ls\\runner.py", devtoolsDir);
    bundleMode = GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES;
    if (!bundleMode) swprintf(devtoolsDir, MAX_PATH, L"%ls\\NppRunner", home);
    swprintf(scriptPath, MAX_PATH, L"%ls\\recent-folders.ps1", devtoolsDir);
    swprintf(cachePath, MAX_PATH, L"%ls\\recent-folders-cache.txt", devtoolsDir);
    /* Notepad++'s settings folder (%APPDATA%\Notepad++, or the program folder when portable) */
    SendMessageW(npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)cfg);
    upDirs(cfg, 2);
    swprintf(configPath, MAX_PATH, L"%ls\\config.xml", cfg);
    for (int i = 0; i < MAX_SLOTS; i++) {
        swprintf(funcs[i]._itemName, 64, L"Recent folder %d", i + 1);
        funcs[i]._pFunc = slotFuncs[i];
    }
    wcscpy(funcs[FN_MORE]._itemName, L"More Folders...");
    funcs[FN_MORE]._pFunc = moreFolders;
    wcscpy(funcs[FN_NEW]._itemName, L"New File in Folder...");
    funcs[FN_NEW]._pFunc = newFileInFolder;
    funcs[FN_NEW]._pShKey = &newFileKey;
    wcscpy(funcs[FN_RUN]._itemName, L"Run (Input / Output panel)");
    funcs[FN_RUN]._pFunc = runPanelRunCurrent;
    funcs[FN_RUN]._pShKey = &runKey;
    wcscpy(funcs[FN_STOP]._itemName, L"Stop Running Program");
    funcs[FN_STOP]._pFunc = runPanelStop;
    wcscpy(funcs[FN_PANEL]._itemName, L"Show / Hide Run Panel");
    funcs[FN_PANEL]._pFunc = runPanelToggle;
    funcs[FN_PANEL]._pShKey = &panelKey;
    wcscpy(funcs[FN_QUICKOPEN]._itemName, L"Quick Open File...");
    funcs[FN_QUICKOPEN]._pFunc = quickOpen;
    funcs[FN_QUICKOPEN]._pShKey = &quickOpenKey;
    wcscpy(funcs[FN_PALETTE]._itemName, L"Command Palette...");
    funcs[FN_PALETTE]._pFunc = commandPalette;
    funcs[FN_PALETTE]._pShKey = &paletteKey;
    wcscpy(funcs[FN_CLOSEFOLDER]._itemName, L"Close Folder");
    funcs[FN_CLOSEFOLDER]._pFunc = closeFolder;
    wcscpy(funcs[FN_NEWWINDOW]._itemName, L"New Window");
    funcs[FN_NEWWINDOW]._pFunc = newWindow;
    funcs[FN_NEWWINDOW]._pShKey = &newWindowKey;
    wcscpy(funcs[FN_TOOLS]._itemName, L"Check / Install Tools...");
    funcs[FN_TOOLS]._pFunc = checkTools;
    wcscpy(funcs[FN_DUPLINE]._itemName, L"Duplicate Line");
    funcs[FN_DUPLINE]._pFunc = duplicateLine;
    wcscpy(funcs[FN_CARETUP]._itemName, L"Add Cursor Above");
    funcs[FN_CARETUP]._pFunc = addCursorAbove;
    wcscpy(funcs[FN_CARETDOWN]._itemName, L"Add Cursor Below");
    funcs[FN_CARETDOWN]._pFunc = addCursorBelow;
    wcscpy(funcs[FN_TRIMSAVE]._itemName, L"Trim Trailing Space and Save");
    funcs[FN_TRIMSAVE]._pFunc = trimAndSave;
    wcscpy(funcs[FN_THEMEDARK]._itemName, L"Color Theme: Dark");
    funcs[FN_THEMEDARK]._pFunc = colorThemeDark;
    wcscpy(funcs[FN_THEMELIGHT]._itemName, L"Color Theme: Light");
    funcs[FN_THEMELIGHT]._pFunc = colorThemeLight;
    wcscpy(funcs[FN_EXPLORER]._itemName, L"Toggle Explorer\tCtrl+B");
    funcs[FN_EXPLORER]._pFunc = toggleExplorer;
    if (bundleMode) {
        funcs[FN_DUPLINE]._pShKey = &dupLineKey;
        funcs[FN_CARETUP]._pShKey = &caretUpKey;
        funcs[FN_CARETDOWN]._pShKey = &caretDownKey;
        funcs[FN_TRIMSAVE]._pShKey = &trimSaveKey;
    }
}

int funcCmdId(int index) { return index >= 0 && index < FN_COUNT ? funcs[index]._cmdID : 0; }

/* ---- NppLspClient's leftover WebView folders ----
   NppLspClient keeps a WebView2 cache in plugins\config\NppLspClient\<process id> and can't
   delete it at exit (the browser component still holds it), so every start asks
   "An old Webview configuration directory was found. Should it be deleted?".
   We delete those folders quietly: at load for dead processes, and after exit for our own. */
static wchar_t lspConfigDir[MAX_PATH];

static bool isRunningNotepad(DWORD pid)
{
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return false;
    wchar_t exe[MAX_PATH]; DWORD n = MAX_PATH;
    bool yes = QueryFullProcessImageNameW(p, 0, exe, &n) && _wcsicmp(leafOf(exe), L"notepad++.exe") == 0;
    DWORD code = 0;
    if (yes && GetExitCodeProcess(p, &code) && code != STILL_ACTIVE) yes = false;
    CloseHandle(p);
    return yes;
}

/* Deleting old WebView folders can take a while (tens of MB), so it runs in the background
   instead of holding up Notepad++'s start. */
static DWORD WINAPI cleanStaleWebViewThread(LPVOID unused)
{
    (void)unused;
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);   /* low CPU and disk priority */
    wchar_t pat[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%ls\\*", lspConfigDir);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !fd.cFileName[0]) continue;
        if (wcsspn(fd.cFileName, L"0123456789") != wcslen(fd.cFileName)) continue;   /* only <process id> folders */
        DWORD pid = (DWORD)_wtoi(fd.cFileName);
        if (pid == GetCurrentProcessId() || isRunningNotepad(pid)) continue;
        wchar_t dir[MAX_PATH + 2] = L"";
        if (swprintf(dir, MAX_PATH, L"%ls\\%ls", lspConfigDir, fd.cFileName) < 0) continue;
        dir[wcslen(dir) + 1] = 0;                                  /* double-NUL list */
        SHFILEOPSTRUCTW op = { 0 };
        op.wFunc = FO_DELETE; op.pFrom = dir;
        op.fFlags = FOF_NO_UI;                                     /* cache data: delete, no Recycle Bin */
        SHFileOperationW(&op);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return 0;
}

static void cleanStaleWebViewFolders(void)
{
    wchar_t cfg[MAX_PATH] = L"";
    SendMessageW(npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)cfg);
    if (!cfg[0] || swprintf(lspConfigDir, MAX_PATH, L"%ls\\NppLspClient", cfg) < 0) return;
    HANDLE t = CreateThread(NULL, 0, cleanStaleWebViewThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

/* At shutdown our own folder is still in use; remove it a few seconds after Notepad++ has exited. */
static void cleanOwnWebViewFolderAfterExit(void)
{
    if (!lspConfigDir[0]) return;
    wchar_t dir[MAX_PATH], cmd[2 * MAX_PATH + 128];
    swprintf(dir, MAX_PATH, L"%ls\\%lu", lspConfigDir, GetCurrentProcessId());
    if (!isDir(dir)) return;
    swprintf(cmd, sizeof cmd / sizeof cmd[0], L"cmd.exe /c ping -n 9 127.0.0.1 >nul & rd /s /q \"%ls\"", dir);
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
}

__declspec(dllexport) const wchar_t *getName(void) { return PLUGIN_NAME; }

__declspec(dllexport) FuncItem *getFuncsArray(int *n) { *n = FN_COUNT; return funcs; }

/* Ctrl+Shift+R opens the recent-folder picker. Caught here, before Notepad++'s own shortcuts:
   the Run menu command that has this key may be blocked by Notepad++'s shortcuts.xml check. */
static HHOOK keyHook;

/* VS Code keys for IntelliSense and editing, in the editor. NppLspClient commands are found by
   their menu name; Notepad++ commands by id. Ctrl+K starts a chord, as in VS Code. */
enum { MC = 1, MS = 2, MA = 4 };
typedef struct { UINT key; int mods; const wchar_t *lspItem; UINT cmd; bool lspLangOnly; const wchar_t *keys; } VsKey;
static const VsKey vsKeys[] = {
    { VK_SPACE, MC,      L"Manual complete",           0, true,  L"Ctrl+Space" },       /* Trigger Suggest */
    { 'M',      MC | MS, L"Toggle diagnostics window", 0, false, L"Ctrl+Shift+M" },     /* Problems */
    { 'T',      MC,      L"Toggle symbols window",     0, false, L"Ctrl+T" },           /* symbols */
    { VK_LEFT,  MA,      L"Jump back home",            0, false, L"Alt+Left" },         /* Go Back */
    { 'H',      MS | MA, L"View call hierarchy",       0, false, L"Shift+Alt+H" },
};
static const VsKey chordKeys[] = {                                 /* Ctrl+K, then ... */
    { 'C', MC, NULL, 42035 /* Single Line Comment */,   false, L"Ctrl+K Ctrl+C" },
    { 'U', MC, NULL, 42036 /* Single Line Uncomment */, false, L"Ctrl+K Ctrl+U" },
    { 'F', MC, L"Format selected text", 0,              false, L"Ctrl+K Ctrl+F" },
    { '0', MC, NULL, 44010 /* Fold All */,              false, L"Ctrl+K Ctrl+0" },
    { 'J', MC, NULL, 44029 /* Unfold All */,            false, L"Ctrl+K Ctrl+J" },
    { 'X', MC, NULL, 42024 /* Trim Trailing Space */,   false, L"Ctrl+K Ctrl+X" },
};
static DWORD chordStart;                                            /* Ctrl+K pressed at (ms), 0 = no chord */

static UINT lspCommand(const wchar_t *name)
{
    size_t n = wcslen(name);
    for (int k = 0; lspMenu && k < GetMenuItemCount(lspMenu); k++) {
        wchar_t item[128] = L"";
        if (GetMenuStringW(lspMenu, k, item, 128, MF_BYPOSITION) && wcsncmp(item, name, n) == 0 && (!item[n] || item[n] == L'\t'))
            return GetMenuItemID(lspMenu, k);
    }
    return 0;
}

static bool lspLanguage(void)
{
    int lang = 0;
    SendMessageW(npp._nppHandle, NPPMSG + 5 /* NPPM_GETCURRENTLANGTYPE */, 0, (LPARAM)&lang);
    return lang == 2 || lang == 3 || lang == 6 || lang == 22 || lang == 19 || lang == 58;   /* C, C++, Java, Python, JavaScript */
}

/* Show the VS Code keys next to the commands in the menus (and so in the Command Palette). */
static void labelVsKeys(void)
{
    HMENU bar = GetMenu(npp._nppHandle);
    for (int pass = 0; pass < 2; pass++) {
        const VsKey *t = pass ? chordKeys : vsKeys;
        int n = pass ? (int)(sizeof chordKeys / sizeof *chordKeys) : (int)(sizeof vsKeys / sizeof *vsKeys);
        for (int i = 0; i < n; i++) {
            UINT id = t[i].lspItem ? lspCommand(t[i].lspItem) : t[i].cmd;
            wchar_t text[160] = L"";
            if (!id || !GetMenuStringW(bar, id, text, 128, MF_BYCOMMAND)) continue;
            wchar_t *tab = wcschr(text, L'\t');
            if (tab) *tab = 0;
            swprintf(text + wcslen(text), 160 - wcslen(text), L"\t%ls", t[i].keys);
            MENUITEMINFOW mi = { sizeof mi };
            mi.fMask = MIIM_STRING; mi.dwTypeData = text;
            SetMenuItemInfoW(bar, id, FALSE, &mi);
        }
    }
}

/* ---- Snippets, VS Code style: type a word, press Tab. ----
   In the expansion, \n starts a new line at the current indentation, \t is one indent step, $0 is
   where the caret goes. Only whole words right before the caret count (not "obj.sout"). */
typedef struct { const char *word, *text; } Snippet;
static const Snippet javaSnippets[] = {
    { "sout", "System.out.println($0);" }, { "souf", "System.out.printf($0);" }, { "serr", "System.err.println($0);" },
    { "psvm", "public static void main(String[] args) {\n\t$0\n}" }, { "main", "public static void main(String[] args) {\n\t$0\n}" },
    { "fori", "for (int i = 0; i < $0; i++) {\n\t\n}" }, { "forr", "for (int i = $0; i >= 0; i--) {\n\t\n}" },
    { "fore", "for (var x : $0) {\n\t\n}" }, { "iter", "for (var x : $0) {\n\t\n}" },
    { "sc", "Scanner sc = new Scanner(System.in);$0" },
    { "br", "BufferedReader br = new BufferedReader(new InputStreamReader(System.in));$0" },
    { "if", "if ($0) {\n\t\n}" }, { "ifelse", "if ($0) {\n\t\n} else {\n\t\n}" }, { "while", "while ($0) {\n\t\n}" },
    { "try", "try {\n\t$0\n} catch (Exception e) {\n\te.printStackTrace();\n}" },
    { "tc", "int t = sc.nextInt();\nwhile (t-- > 0) {\n\t$0\n}" }, { NULL, NULL } };
static const Snippet cppSnippets[] = {
    { "cout", "cout << $0 << \"\\n\";" }, { "cin", "cin >> $0;" }, { "cerr", "cerr << $0 << \"\\n\";" },
    { "main", "int main() {\n\t$0\n\treturn 0;\n}" },
    { "fori", "for (int i = 0; i < $0; i++) {\n\t\n}" }, { "forr", "for (int i = $0; i >= 0; i--) {\n\t\n}" },
    { "fore", "for (auto &x : $0) {\n\t\n}" }, { "if", "if ($0) {\n\t\n}" }, { "ifelse", "if ($0) {\n\t\n} else {\n\t\n}" },
    { "while", "while ($0) {\n\t\n}" }, { "tc", "int t;\ncin >> t;\nwhile (t--) {\n\t$0\n}" },
    { "fastio", "ios::sync_with_stdio(false);\ncin.tie(nullptr);$0" },
    { "vi", "vector<int> $0;" }, { "vll", "vector<long long> $0;" }, { "pii", "pair<int, int> $0;" },
    { "struct", "struct $0 {\n\t\n};" }, { "class", "class $0 {\npublic:\n\t\n};" }, { NULL, NULL } };
static const Snippet cSnippets[] = {
    { "printf", "printf(\"$0\\n\");" }, { "scanf", "scanf(\"%d\", &$0);" }, { "main", "int main(void) {\n\t$0\n\treturn 0;\n}" },
    { "fori", "for (int i = 0; i < $0; i++) {\n\t\n}" }, { "forr", "for (int i = $0; i >= 0; i--) {\n\t\n}" },
    { "if", "if ($0) {\n\t\n}" }, { "ifelse", "if ($0) {\n\t\n} else {\n\t\n}" }, { "while", "while ($0) {\n\t\n}" },
    { "tc", "int t;\nscanf(\"%d\", &t);\nwhile (t--) {\n\t$0\n}" }, { "struct", "struct $0 {\n\t\n};" }, { NULL, NULL } };
static const Snippet pySnippets[] = {
    { "pr", "print($0)" }, { "main", "def main():\n\t$0\n\n\nif __name__ == \"__main__\":\n\tmain()" },
    { "ifmain", "if __name__ == \"__main__\":\n\t$0" }, { "def", "def $0():\n\tpass" },
    { "fori", "for i in range($0):\n\t" }, { "fore", "for x in $0:\n\t" }, { "inp", "input()$0" },
    { "ints", "list(map(int, input().split()))$0" }, { "mapint", "map(int, input().split())$0" },
    { "tc", "for _ in range(int(input())):\n\t$0" }, { "class", "class $0:\n\tdef __init__(self):\n\t\tpass" },
    { "try", "try:\n\t$0\nexcept Exception as e:\n\tprint(e)" }, { "with", "with open($0) as f:\n\t" }, { NULL, NULL } };
static const Snippet jsSnippets[] = {
    { "log", "console.log($0);" }, { "clg", "console.log($0);" }, { "cerr", "console.error($0);" },
    { "fori", "for (let i = 0; i < $0; i++) {\n\t\n}" }, { "forr", "for (let i = $0; i >= 0; i--) {\n\t\n}" },
    { "fore", "for (const x of $0) {\n\t\n}" }, { "forin", "for (const key in $0) {\n\t\n}" },
    { "fn", "function $0() {\n\t\n}" }, { "af", "($0) => {\n\t\n}" }, { "if", "if ($0) {\n\t\n}" },
    { "ifelse", "if ($0) {\n\t\n} else {\n\t\n}" }, { "while", "while ($0) {\n\t\n}" },
    { "try", "try {\n\t$0\n} catch (e) {\n\tconsole.error(e);\n}" }, { NULL, NULL } };

static const Snippet *snippetsForLang(int lang)
{
    switch (lang) {
    case 6: return javaSnippets;
    case 3: return cppSnippets;
    case 2: return cSnippets;
    case 22: return pySnippets;
    case 19: case 58: return jsSnippets;
    }
    return NULL;
}

/* Tab in the editor: expand the snippet word before the caret; false = not a snippet (Tab as usual). */
static bool expandSnippet(HWND ed)
{
#define SCI(m, w, l) SendMessageW(ed, (m), (WPARAM)(w), (LPARAM)(l))
    if (SCI(2102 /* SCI_AUTOCACTIVE */, 0, 0) || !SCI(2650 /* SCI_GETSELECTIONEMPTY */, 0, 0) || SCI(2570 /* SCI_GETSELECTIONS */, 0, 0) > 1)
        return false;
    int lang = 0;
    SendMessageW(npp._nppHandle, NPPMSG + 5 /* NPPM_GETCURRENTLANGTYPE */, 0, (LPARAM)&lang);
    const Snippet *list = snippetsForLang(lang);
    if (!list) return false;
    intptr_t pos = SCI(2008 /* SCI_GETCURRENTPOS */, 0, 0), start = pos;
    char word[24];
    while (start > 0 && pos - start < (intptr_t)sizeof word - 1) {
        int c = (int)SCI(2007 /* SCI_GETCHARAT */, start - 1, 0) & 0xFF;
        if (!(isalnum(c) || c == '_')) break;
        start--;
    }
    if (start == pos) return false;
    if (start > 0) {                                      /* a whole word: not obj.sout or 3sout */
        int before = (int)SCI(2007, start - 1, 0) & 0xFF;
        if (before == '.' || isalnum(before) || before == '_') return false;
    }
    for (intptr_t i = start; i < pos; i++) word[i - start] = (char)SCI(2007, i, 0);
    word[pos - start] = 0;
    const Snippet *s = NULL;
    for (int i = 0; list[i].word; i++) if (strcmp(list[i].word, word) == 0) { s = &list[i]; break; }
    if (!s) return false;

    /* the line's own indentation, one indent step, and the file's line ending */
    intptr_t line = SCI(2166 /* SCI_LINEFROMPOSITION */, pos, 0), lineStart = SCI(2167 /* SCI_POSITIONFROMLINE */, line, 0);
    char lead[128]; int nlead = 0;
    for (intptr_t i = lineStart; i < start && nlead < (int)sizeof lead - 1; i++) {
        char c = (char)SCI(2007, i, 0);
        if (c != ' ' && c != '\t') break;
        lead[nlead++] = c;
    }
    lead[nlead] = 0;
    char step[17] = "\t";
    bool spaces = nlead ? lead[0] == ' ' : !SCI(2125 /* SCI_GETUSETABS */, 0, 0);   /* the line's own style wins */
    if (spaces) {
        int w = (int)SCI(2123 /* SCI_GETINDENT */, 0, 0);
        if (w <= 0) w = (int)SCI(2121 /* SCI_GETTABWIDTH */, 0, 0);
        if (w <= 0 || w > 16) w = 4;
        memset(step, ' ', w); step[w] = 0;
    }
    int eolMode = (int)SCI(2030 /* SCI_GETEOLMODE */, 0, 0);
    const char *eol = eolMode == 0 ? "\r\n" : eolMode == 1 ? "\r" : "\n";

    char out[1024]; size_t n = 0; intptr_t caret = -1;
    for (const char *p = s->text; *p && n < sizeof out - 160; p++) {
        if (p[0] == '$' && p[1] == '0') { caret = (intptr_t)n; p++; continue; }
        if (*p == '\n') { n += (size_t)snprintf(out + n, sizeof out - n, "%s%s", eol, lead); continue; }
        if (*p == '\t') { n += (size_t)snprintf(out + n, sizeof out - n, "%s", step); continue; }
        out[n++] = *p;
    }
    out[n] = 0;
    SCI(2078 /* SCI_BEGINUNDOACTION */, 0, 0);
    SCI(2686 /* SCI_SETTARGETRANGE */, start, pos);
    SCI(2194 /* SCI_REPLACETARGET */, (WPARAM)n, out);
    SCI(2025 /* SCI_GOTOPOS */, start + (caret >= 0 ? caret : (intptr_t)n), 0);
    SCI(2079 /* SCI_ENDUNDOACTION */, 0, 0);
    return true;
#undef SCI
}

static bool modsMatch(int mods)
{
    return (GetKeyState(VK_CONTROL) < 0) == ((mods & MC) != 0) && (GetKeyState(VK_SHIFT) < 0) == ((mods & MS) != 0)
        && (GetKeyState(VK_MENU) < 0) == ((mods & MA) != 0);
}

static LRESULT CALLBACK keyHookProc(int code, WPARAM w, LPARAM l)
{
    if (code != HC_ACTION) return CallNextHookEx(keyHook, code, w, l);
    bool down = !(l & 0x80000000), repeat = (l & 0x40000000) != 0;
    /* Ctrl+Shift+R opens the recent-folder picker. Caught here, before Notepad++'s own shortcuts:
       the Run menu command that has this key may be blocked by Notepad++'s shortcuts.xml check. */
    if (w == 'R' && modsMatch(MC | MS)) {
        if (down && !repeat) PostMessageW(npp._nppHandle, WM_COMMAND, funcs[FN_MORE]._cmdID, 0);
        return 1;                                                   /* Notepad++ never sees the key */
    }
    HWND f = GetFocus();
    if (w == 'B' && modsMatch(MC) && f && (GetAncestor(f, GA_ROOT) == npp._nppHandle || IsChild(fawPanelWindow(), f))) {
        if (down && !repeat) PostMessageW(npp._nppHandle, WM_COMMAND, funcs[FN_EXPLORER]._cmdID, 0);   /* Toggle Explorer */
        return 1;
    }
    if (!f || (f != npp._scintillaMainHandle && f != npp._scintillaSecondHandle)) { chordStart = 0; return CallNextHookEx(keyHook, code, w, l); }
    if (w == VK_CONTROL || w == VK_SHIFT || w == VK_MENU) return CallNextHookEx(keyHook, code, w, l);
    if (chordStart && GetTickCount() - chordStart < 3000) {         /* second key of Ctrl+K ... */
        if (!down) return 1;
        chordStart = 0;
        for (size_t i = 0; i < sizeof chordKeys / sizeof *chordKeys; i++)
            if (chordKeys[i].key == w && (GetKeyState(VK_SHIFT) >= 0) && GetKeyState(VK_MENU) >= 0) {
                UINT id = chordKeys[i].lspItem ? lspCommand(chordKeys[i].lspItem) : chordKeys[i].cmd;
                if (id) PostMessageW(npp._nppHandle, WM_COMMAND, id, 0);
                return 1;
            }
        return 1;                                                   /* unknown chord: ignored, like VS Code */
    }
    chordStart = 0;
    if (w == VK_TAB && modsMatch(0) && down) {                     /* sout + Tab, fori + Tab, ... */
        if (expandSnippet(f)) return 1;
        return CallNextHookEx(keyHook, code, w, l);
    }
    if (w == 'K' && modsMatch(MC)) {
        if (down && !repeat) chordStart = GetTickCount();
        return 1;
    }
    for (size_t i = 0; i < sizeof vsKeys / sizeof *vsKeys; i++) {
        if (vsKeys[i].key != w || !modsMatch(vsKeys[i].mods)) continue;
        if (vsKeys[i].lspLangOnly && !lspLanguage()) break;         /* other files: Notepad++'s own completion */
        UINT id = lspCommand(vsKeys[i].lspItem);
        if (!id) break;
        if (down && !repeat) PostMessageW(npp._nppHandle, WM_COMMAND, id, 0);
        return 1;
    }
    return CallNextHookEx(keyHook, code, w, l);
}

__declspec(dllexport) void beNotified(SciNotifyHeader *notify)
{
    trackTabs(notify);
    if (notify->code == NPPN_READY && notify->hwndFrom == npp._nppHandle) {
        installMenu();
        labelVsKeys();
        markColorTheme();
        keyHook = SetWindowsHookExW(WH_KEYBOARD, keyHookProc, NULL, GetCurrentThreadId());
        SetTimer(npp._nppHandle, LISTONLY_TIMER_ID, 5000, NULL);   /* later: refresh the recent list from Notepad++'s history */
        wchar_t *cl = _wcsdup(GetCommandLineW());
        startedWithFolder = cl && wcsstr(_wcslwr(cl), L"-openfoldersasworkspace");
        free(cl);
        startupSwapTicks = 60;                                      /* up to 3 s */
        SetTimer(npp._nppHandle, STARTUP_TIMER_ID, 50, NULL);
        if (bundleMode) SetTimer(npp._nppHandle, FIRSTRUN_TIMER_ID, 2500, NULL);   /* the window opens only if a tool is missing */
        SetTimer(npp._nppHandle, WARMUP_TIMER_ID, 4000, NULL);
    }
    if (notify->code == NPPN_BEFORESHUTDOWN && notify->hwndFrom == npp._nppHandle && !filesStashed) {
        wchar_t cur[MAX_PATH];
        if (currentWorkspace(cur)) stashFiles(cur, false, false); /* remember this folder's files for next time */
    }
    if ((notify->code == NPPN_BUFFERACTIVATED || notify->code == NPPN_FILESAVED) && notify->hwndFrom == npp._nppHandle)
        templateIfEmpty();
    if (notify->code == NPPN_SHUTDOWN && notify->hwndFrom == npp._nppHandle) {
        if (keyHook) UnhookWindowsHookEx(keyHook);
        cleanOwnWebViewFolderAfterExit();
    }
    if ((notify->code == NPPN_DARKMODECHANGED || notify->code == NPPN_WORDSTYLESUPDATED) && notify->hwndFrom == npp._nppHandle) {
        markColorTheme();
        explorerRefresh();                                          /* new colours in the Explorer */
    }
    runPanelOnNotify(notify);
    welcomeOnNotify(notify);
}

__declspec(dllexport) LRESULT messageProc(UINT msg, WPARAM w, LPARAM l) { (void)msg; (void)w; (void)l; return TRUE; }

__declspec(dllexport) BOOL isUnicode(void) { return TRUE; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) hInst = h;
    return TRUE;
}
