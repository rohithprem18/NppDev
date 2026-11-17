/*
 * NppDev setup - installs the NppDev bundle (a portable Notepad++ with the Dev Tools plugin)
 * for the current user: no administrator rights needed.
 *
 *   NppDev-Setup.exe                     install with a small window
 *   NppDev-Setup.exe /S [/D=folder]      install silently (default folder: %LOCALAPPDATA%\Programs\NppDev)
 *   uninstall.exe /uninstall [/S]        remove it (files you made and installed compilers are left alone)
 *   (internal) /cleanup <pid> <folder>   a temp copy of the uninstaller removes the folder once it has exited
 *
 * An installed NppDev (Settings > Apps entry) is found and updated in place: same folder, settings kept.
 *
 * The files travel inside this exe: [setup program][payload.zip][size: 8 bytes]["NPPDEVPK"].
 * They are unpacked with Windows' own tar.exe. On an upgrade, the user's settings are kept.
 * Build: see build.ps1.
 */
#define COBJMACROS
#define _WIN32_WINNT 0x0A00                 /* Windows 10 */
#include <windows.h>
#include <psapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdbool.h>
#include <stdio.h>
#include <wchar.h>
#include "version.h"          /* VER_TEXT, written by build.ps1 from the VERSION file */

#define APP_NAME     L"NppDev"
#define APP_TITLE    L"NppDev (Notepad++ Dev Edition)"
#define APP_AUTHOR   L"Rohith Prem"
#define APP_URL      L"https://github.com/rohithprem18/NppDev"
#define WIDEN2(x)    L ## x
#define WIDEN(x)     WIDEN2(x)
#define APP_VERSION  WIDEN(VER_TEXT)
#define UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NppDev"
#define WM_STEP      (WM_APP + 1)     /* wParam: percent, lParam: status text (static string) */
#define WM_DONE      (WM_APP + 2)     /* wParam: 1 = installed */

enum { IDC_DIR = 100, IDC_BROWSE, IDC_DESKTOP, IDC_LAUNCH, IDC_PROGRESS, IDC_STATUS, IDC_INSTALL };

/* settings the user may have changed: kept on an upgrade */
static const wchar_t *const userFiles[] = { L"config.xml", L"shortcuts.xml", L"stylers.xml", L"langs.xml", L"contextMenu.xml" };

static wchar_t installDir[MAX_PATH], selfPath[MAX_PATH], failText[512];
static wchar_t oldVersion[32];             /* the NppDev version already installed, "" if none */
static int creepTo, stepAt;                 /* the bar keeps moving towards creepTo during long steps */
static bool silent, wantDesktop = true, wantLaunch = true;
static HWND mainWnd, dirEdit, statusText, progress, installBtn, cancelBtn, browseBtn, desktopBox, launchBox;
static HFONT uiFont;

/* ---------------- small helpers ---------------- */

static bool exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

static void step(int percent, const wchar_t *text)
{
    if (mainWnd) PostMessageW(mainWnd, WM_STEP, percent, (LPARAM)text);
}

static bool fail(const wchar_t *what)
{
    swprintf(failText, 512, L"%ls (error %lu)", what, GetLastError());
    return false;
}

/* Run a program hidden and wait; returns its exit code (or -1). */
static int runHidden(wchar_t *cmd)
{
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}

/* Is NppDev from this folder running? (Its files can't be replaced while it runs.) */
static bool appRunning(void)
{
    wchar_t exe[MAX_PATH], path[MAX_PATH];
    swprintf(exe, MAX_PATH, L"%ls\\notepad++.exe", installDir);
    DWORD pids[4096], bytes;
    if (!K32EnumProcesses(pids, sizeof pids, &bytes)) return false;
    for (DWORD i = 0; i < bytes / sizeof(DWORD); i++) {
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!p) continue;
        DWORD n = MAX_PATH;
        bool same = QueryFullProcessImageNameW(p, 0, path, &n) && _wcsicmp(path, exe) == 0;
        CloseHandle(p);
        if (same) return true;
    }
    return false;
}

/* ---------------- install steps ---------------- */

static bool readAt(HANDLE f, LONGLONG at, void *buf, DWORD n)
{
    LARGE_INTEGER pos;
    DWORD got = 0;
    pos.QuadPart = at;
    return SetFilePointerEx(f, pos, NULL, FILE_BEGIN) && ReadFile(f, buf, n, &got, NULL) && got == n;
}

/* Where our data ends: the end of the file, or - once the setup is code-signed - the start of the
   signature, which signing tools append (after padding the file to a multiple of 8 bytes). */
static LONGLONG dataEnd(HANDLE f, LONGLONG size)
{
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    if (!readAt(f, 0, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return size;
    if (!readAt(f, dos.e_lfanew, &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) return size;
    const IMAGE_DATA_DIRECTORY *sec = &nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
    return sec->VirtualAddress && sec->Size && sec->VirtualAddress < size ? sec->VirtualAddress : size;
}

/* Copy the payload at the end of this exe to a temp zip; returns the stub size (bytes before it). */
static LONGLONG extractPayload(const wchar_t *zip)
{
    HANDLE in = CreateFileW(selfPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (in == INVALID_HANDLE_VALUE) return fail(L"Could not read the setup file"), -1;
    LARGE_INTEGER size, pos;
    GetFileSizeEx(in, &size);
    LONGLONG end = dataEnd(in, size.QuadPart), payload = 0;
    char tail[16];
    DWORD got = 0;
    bool found = false;
    for (int pad = 0; pad < 8; pad++) {                  /* signing may pad with up to 7 zero bytes */
        if (readAt(in, end - pad - 16, tail, 16) && memcmp(tail + 8, "NPPDEVPK", 8) == 0) {
            end -= pad;
            found = true;
            break;
        }
    }
    if (!found) {
        CloseHandle(in);
        SetLastError(ERROR_BAD_FORMAT);
        return fail(L"This setup file is incomplete or damaged"), -1;
    }
    memcpy(&payload, tail, 8);
    LONGLONG stub = end - 16 - payload;
    if (stub <= 0) { CloseHandle(in); SetLastError(ERROR_BAD_FORMAT); return fail(L"This setup file is damaged"), -1; }
    pos.QuadPart = stub;
    SetFilePointerEx(in, pos, NULL, FILE_BEGIN);
    HANDLE out = CreateFileW(zip, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (out == INVALID_HANDLE_VALUE) { CloseHandle(in); return fail(L"Could not write to the temp folder"), -1; }
    static char buf[1 << 20];
    LONGLONG left = payload;
    while (left > 0) {
        DWORD want = left > (LONGLONG)sizeof buf ? sizeof buf : (DWORD)left, put;
        if (!ReadFile(in, buf, want, &got, NULL) || !got || !WriteFile(out, buf, got, &put, NULL)) break;
        left -= got;
        step(5 + (int)(25 * (payload - left) / payload), L"Unpacking...");
    }
    CloseHandle(out);
    CloseHandle(in);
    if (left > 0) return fail(L"Could not unpack the setup file"), -1;
    return stub;
}

static bool unzipTo(const wchar_t *zip, bool upgrade)
{
    wchar_t tar[MAX_PATH], cmd[4 * MAX_PATH + 512];
    GetSystemDirectoryW(tar, MAX_PATH);
    wcscat(tar, L"\\tar.exe");
    if (!exists(tar)) { SetLastError(ERROR_FILE_NOT_FOUND); return fail(L"Windows' tar.exe is missing (Windows 10 1803 or newer is needed)"); }
    int n = swprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" -xf \"%ls\" -C \"%ls\"", tar, zip, installDir);
    if (upgrade)                                           /* program files first, settings only where missing */
        for (size_t i = 0; i < sizeof userFiles / sizeof userFiles[0]; i++)
            n += swprintf(cmd + n, sizeof cmd / sizeof cmd[0] - n, L" --exclude=%ls", userFiles[i]);
    if (runHidden(cmd) != 0) { SetLastError(ERROR_GEN_FAILURE); return fail(L"Unpacking the files failed"); }
    if (upgrade) {
        n = swprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" -xkf \"%ls\" -C \"%ls\"", tar, zip, installDir);
        for (size_t i = 0; i < sizeof userFiles / sizeof userFiles[0]; i++)
            n += swprintf(cmd + n, sizeof cmd / sizeof cmd[0] - n, L" %ls", userFiles[i]);
        runHidden(cmd);                                    /* "already exists" for kept files is fine */
    }
    return true;
}

/* NppLspClient.toml from its template, with this install's devtools folder. */
static bool writeLspSettings(void)
{
    wchar_t tmpl[MAX_PATH], dest[MAX_PATH], dev[MAX_PATH];
    swprintf(tmpl, MAX_PATH, L"%ls\\plugins\\Config\\NppLspClient\\NppLspClient.toml.template", installDir);
    swprintf(dest, MAX_PATH, L"%ls\\plugins\\Config\\NppLspClient\\NppLspClient.toml", installDir);
    swprintf(dev, MAX_PATH, L"%ls\\devtools", installDir);
    HANDLE f = CreateFileW(tmpl, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return fail(L"The IntelliSense settings template is missing");
    DWORD size = GetFileSize(f, NULL), got = 0;
    char *text = HeapAlloc(GetProcessHeap(), 0, size + 1);
    ReadFile(f, text, size, &got, NULL);
    CloseHandle(f);
    text[got] = 0;
    char devUtf8[3 * MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, dev, -1, devUtf8, sizeof devUtf8, NULL, NULL);
    HANDLE out = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (out == INVALID_HANDLE_VALUE) { HeapFree(GetProcessHeap(), 0, text); return fail(L"Could not write the IntelliSense settings"); }
    DWORD put;
    for (char *p = text, *hit; *p; p = hit + 10) {
        hit = strstr(p, "{DEVTOOLS}");
        if (!hit) { WriteFile(out, p, (DWORD)strlen(p), &put, NULL); break; }
        WriteFile(out, p, (DWORD)(hit - p), &put, NULL);
        WriteFile(out, devUtf8, (DWORD)strlen(devUtf8), &put, NULL);
    }
    CloseHandle(out);
    HeapFree(GetProcessHeap(), 0, text);
    return true;
}

/* The Hack font, for the current user only (Windows 10 1809+ supports per-user fonts). */
static void installFonts(void)
{
    static const struct { const wchar_t *file, *name; } fonts[] = {
        { L"Hack-Regular.ttf", L"Hack Regular (TrueType)" }, { L"Hack-Bold.ttf", L"Hack Bold (TrueType)" },
        { L"Hack-Italic.ttf", L"Hack Italic (TrueType)" }, { L"Hack-BoldItalic.ttf", L"Hack Bold Italic (TrueType)" } };
    wchar_t fontDir[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
    if (!SHGetSpecialFolderPathW(NULL, fontDir, CSIDL_LOCAL_APPDATA, TRUE)) return;
    wcscat(fontDir, L"\\Microsoft\\Windows\\Fonts");
    SHCreateDirectoryExW(NULL, fontDir, NULL);
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, NULL, 0,
                        KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) return;
    bool added = false;
    for (size_t i = 0; i < sizeof fonts / sizeof fonts[0]; i++) {
        HKEY machine;
        bool systemWide = false;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_QUERY_VALUE, &machine) == ERROR_SUCCESS) {
            systemWide = RegQueryValueExW(machine, fonts[i].name, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
            RegCloseKey(machine);
        }
        if (systemWide) continue;                            /* already installed for everyone */
        swprintf(src, MAX_PATH, L"%ls\\fonts\\%ls", installDir, fonts[i].file);
        swprintf(dst, MAX_PATH, L"%ls\\%ls", fontDir, fonts[i].file);
        if (!exists(src) || (!exists(dst) && !CopyFileW(src, dst, FALSE))) continue;
        RegSetValueExW(key, fonts[i].name, 0, REG_SZ, (const BYTE *)dst, (DWORD)((wcslen(dst) + 1) * sizeof(wchar_t)));
        if (AddFontResourceW(dst)) added = true;
    }
    RegCloseKey(key);
    if (added) SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0, SMTO_ABORTIFHUNG, 1000, NULL);
}

static bool makeShortcut(int csidl, const wchar_t *target, const wchar_t *dir)
{
    wchar_t lnk[MAX_PATH];
    if (!SHGetSpecialFolderPathW(NULL, lnk, csidl, TRUE)) return false;
    wcscat(lnk, L"\\" APP_NAME L".lnk");
    IShellLinkW *link = NULL;
    IPersistFile *file = NULL;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link))) {
        IShellLinkW_SetPath(link, target);
        IShellLinkW_SetWorkingDirectory(link, dir);
        IShellLinkW_SetDescription(link, L"Notepad++ with Run, IntelliSense and VS Code style folders");
        IShellLinkW_SetIconLocation(link, target, 0);
        if (SUCCEEDED(IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&file))) {
            ok = SUCCEEDED(IPersistFile_Save(file, lnk, TRUE));
            IPersistFile_Release(file);
        }
        IShellLinkW_Release(link);
    }
    return ok;
}

/* uninstall.exe = this setup program without the payload; plus the Settings > Apps entry. */
static bool registerUninstall(LONGLONG stubSize)
{
    wchar_t un[MAX_PATH], cmd[MAX_PATH + 32], icon[MAX_PATH];
    swprintf(un, MAX_PATH, L"%ls\\uninstall.exe", installDir);
    HANDLE in = CreateFileW(selfPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE out = CreateFileW(un, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    bool copied = false;
    if (in != INVALID_HANDLE_VALUE && out != INVALID_HANDLE_VALUE) {
        static char buf[1 << 16];
        LONGLONG left = stubSize;
        DWORD got, put;
        while (left > 0 && ReadFile(in, buf, left > (LONGLONG)sizeof buf ? sizeof buf : (DWORD)left, &got, NULL) && got) {
            WriteFile(out, buf, got, &put, NULL);
            left -= got;
        }
        copied = left == 0;
    }
    if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!copied) return fail(L"Could not write uninstall.exe");

    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, UNINSTALL_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return fail(L"Could not register the uninstaller");
    swprintf(cmd, MAX_PATH + 32, L"\"%ls\" /uninstall", un);
    swprintf(icon, MAX_PATH, L"%ls\\notepad++.exe", installDir);
    struct { const wchar_t *name, *value; } values[] = {
        { L"DisplayName", APP_TITLE }, { L"DisplayVersion", APP_VERSION }, { L"Publisher", APP_AUTHOR },
        { L"InstallLocation", installDir }, { L"UninstallString", cmd }, { L"DisplayIcon", icon },
        { L"URLInfoAbout", APP_URL }, { L"HelpLink", APP_URL } };
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++)
        RegSetValueExW(key, values[i].name, 0, REG_SZ, (const BYTE *)values[i].value, (DWORD)((wcslen(values[i].value) + 1) * sizeof(wchar_t)));
    DWORD one = 1, sizeKb = 150 * 1024;
    RegSetValueExW(key, L"NoModify", 0, REG_DWORD, (const BYTE *)&one, sizeof one);
    RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, (const BYTE *)&one, sizeof one);
    RegSetValueExW(key, L"EstimatedSize", 0, REG_DWORD, (const BYTE *)&sizeKb, sizeof sizeKb);
    RegCloseKey(key);
    return true;
}

static bool install(void)
{
    wchar_t zip[MAX_PATH], exe[MAX_PATH];
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    swprintf(exe, MAX_PATH, L"%ls\\notepad++.exe", installDir);
    bool upgrade = exists(exe);
    step(2, upgrade ? L"Updating NppDev - your settings and files are kept..." : L"Preparing...");
    if (SHCreateDirectoryExW(NULL, installDir, NULL) != ERROR_SUCCESS && !exists(installDir))
        return fail(L"Could not create the install folder");
    GetTempPathW(MAX_PATH, zip);
    swprintf(zip + wcslen(zip), MAX_PATH - wcslen(zip), L"nppdev-payload-%lu.zip", GetCurrentProcessId());
    LONGLONG stub = extractPayload(zip);
    if (stub < 0) return false;
    step(35, upgrade ? L"Updating the program files..." : L"Installing the program files...");
    bool ok = unzipTo(zip, upgrade);
    DeleteFileW(zip);
    if (!ok) return false;
    step(80, L"Setting up IntelliSense...");
    if (!writeLspSettings()) return false;
    step(85, L"Installing the Hack font...");
    installFonts();
    step(92, L"Creating shortcuts...");
    makeShortcut(CSIDL_PROGRAMS, exe, installDir);
    if (wantDesktop) makeShortcut(CSIDL_DESKTOPDIRECTORY, exe, installDir);
    if (!registerUninstall(stub)) return false;
    step(100, L"Done.");
    return true;
}

static DWORD WINAPI installThread(LPVOID unused)
{
    (void)unused;
    bool ok = install();
    PostMessageW(mainWnd, WM_DONE, ok, 0);
    return 0;
}

/* ---------------- uninstall ---------------- */

static int uninstall(void)
{
    /* uninstall.exe lives in the install folder */
    wcscpy(installDir, selfPath);
    *wcsrchr(installDir, L'\\') = 0;
    if (!silent && MessageBoxW(NULL, L"Remove NppDev from this PC?\n\nYour own files, and compilers or tools installed "
                               L"with Check / Install Tools, are not touched.", APP_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES)
        return 1;
    while (appRunning()) {
        if (silent || MessageBoxW(NULL, L"NppDev is running. Close it, then click Retry.", APP_TITLE, MB_RETRYCANCEL | MB_ICONWARNING) != IDRETRY)
            return 2;
    }
    wchar_t lnk[MAX_PATH];
    int places[] = { CSIDL_PROGRAMS, CSIDL_DESKTOPDIRECTORY };
    for (int i = 0; i < 2; i++)
        if (SHGetSpecialFolderPathW(NULL, lnk, places[i], FALSE)) { wcscat(lnk, L"\\" APP_NAME L".lnk"); DeleteFileW(lnk); }
    RegDeleteTreeW(HKEY_CURRENT_USER, UNINSTALL_KEY);
    /* This exe is inside the folder, so it can't delete it itself: a copy in %TEMP% waits until
       this process has ended, then removes the folder (see cleanup). */
    wchar_t tmp[MAX_PATH], cmd[3 * MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    swprintf(tmp + wcslen(tmp), MAX_PATH - wcslen(tmp), L"nppdev-uninstall-%lu.exe", GetCurrentProcessId());
    if (CopyFileW(selfPath, tmp, FALSE)) {
        swprintf(cmd, 3 * MAX_PATH, L"\"%ls\" /cleanup %lu \"%ls\"", tmp, GetCurrentProcessId(), installDir);
        STARTUPINFOW si = { sizeof si };
        PROCESS_INFORMATION pi;
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    if (!silent) MessageBoxW(NULL, L"NppDev was removed.", APP_TITLE, MB_ICONINFORMATION);
    return 0;
}

/* Run from %TEMP% by uninstall: wait for the uninstaller to exit, then remove the NppDev folder
   (only a folder that really is an NppDev install: notepad++.exe and devtools inside). */
static int cleanup(DWORD pid, const wchar_t *dir)
{
    HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (p) { WaitForSingleObject(p, 30000); CloseHandle(p); }
    wchar_t probe[MAX_PATH], from[MAX_PATH + 2];
    swprintf(probe, MAX_PATH, L"%ls\\notepad++.exe", dir);
    bool isApp = exists(probe);
    swprintf(probe, MAX_PATH, L"%ls\\devtools", dir);
    if (!isApp || !exists(probe) || wcslen(dir) < 4) return 1;
    for (int attempt = 0; attempt < 10; attempt++) {
        memset(from, 0, sizeof from);
        wcsncpy(from, dir, MAX_PATH);                        /* a double-NUL terminated list */
        SHFILEOPSTRUCTW op = { 0 };
        op.wFunc = FO_DELETE; op.pFrom = from;
        op.fFlags = FOF_NO_UI;
        if (SHFileOperationW(&op) == 0 && !exists(dir)) return 0;
        Sleep(500);
    }
    return 1;
}

/* Copies of the uninstaller left in %TEMP% by earlier uninstalls (they can't delete themselves). */
static void removeOldCleanupCopies(void)
{
    wchar_t dir[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    swprintf(pattern, MAX_PATH, L"%lsnppdev-uninstall-*.exe", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        swprintf(path, MAX_PATH, L"%ls%ls", dir, fd.cFileName);
        if (_wcsicmp(path, selfPath) != 0) DeleteFileW(path);        /* fails harmlessly if one still runs */
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* The NppDev already installed for this user (Settings > Apps entry): its folder and version. */
static bool findInstalled(wchar_t *dir, wchar_t *version)
{
    DWORD n = MAX_PATH * sizeof(wchar_t);
    if (RegGetValueW(HKEY_CURRENT_USER, UNINSTALL_KEY, L"InstallLocation", RRF_RT_REG_SZ, NULL, dir, &n) != ERROR_SUCCESS) return false;
    n = 32 * sizeof(wchar_t);
    if (RegGetValueW(HKEY_CURRENT_USER, UNINSTALL_KEY, L"DisplayVersion", RRF_RT_REG_SZ, NULL, version, &n) != ERROR_SUCCESS) version[0] = 0;
    wchar_t exe[MAX_PATH];
    swprintf(exe, MAX_PATH, L"%ls\\notepad++.exe", dir);
    return exists(exe);
}

/* -1, 0, 1 like strcmp, for "1.4" vs "1.10" */
static int compareVersions(const wchar_t *a, const wchar_t *b)
{
    for (;;) {
        wchar_t *ea, *eb;
        long x = wcstol(a, &ea, 10), y = wcstol(b, &eb, 10);
        if (x != y) return x < y ? -1 : 1;
        a = ea; b = eb;
        if (*a != L'.' && *b != L'.') return 0;
        if (*a == L'.') a++;
        if (*b == L'.') b++;
    }
}

/* ---------------- window ---------------- */

static HWND child(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, mainWnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)uiFont, TRUE);
    return c;
}

static void browse(void)
{
    BROWSEINFOW bi = { 0 };
    wchar_t name[MAX_PATH];
    bi.hwndOwner = mainWnd;
    bi.pszDisplayName = name;
    bi.lpszTitle = L"Install NppDev into a folder (a NppDev folder is created inside):";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t dir[MAX_PATH];
    if (SHGetPathFromIDListW(pidl, dir)) {
        size_t n = wcslen(dir);
        if (n && dir[n - 1] == L'\\') dir[n - 1] = 0;
        wcscat(dir, L"\\" APP_NAME);
        SetWindowTextW(dirEdit, dir);
    }
    CoTaskMemFree(pidl);
}

static void startInstall(void)
{
    GetWindowTextW(dirEdit, installDir, MAX_PATH);
    size_t n = wcslen(installDir);
    while (n > 3 && installDir[n - 1] == L'\\') installDir[--n] = 0;
    if (n < 3 || installDir[1] != L':') { MessageBoxW(mainWnd, L"Please choose a folder like C:\\Users\\you\\NppDev.", APP_TITLE, MB_ICONWARNING); return; }
    while (appRunning())
        if (MessageBoxW(mainWnd, L"NppDev is running from this folder. Close it, then click Retry.", APP_TITLE, MB_RETRYCANCEL | MB_ICONWARNING) != IDRETRY) return;
    wantDesktop = SendMessageW(desktopBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    wantLaunch = SendMessageW(launchBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(installBtn, FALSE); EnableWindow(cancelBtn, FALSE); EnableWindow(browseBtn, FALSE);
    EnableWindow(dirEdit, FALSE); EnableWindow(desktopBox, FALSE); EnableWindow(launchBox, FALSE);
    SetTimer(mainWnd, 1, 250, NULL);
    HANDLE t = CreateThread(NULL, 0, installThread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

static void launchApp(void)
{
    wchar_t exe[MAX_PATH];
    swprintf(exe, MAX_PATH, L"%ls\\notepad++.exe", installDir);
    ShellExecuteW(NULL, L"open", exe, NULL, installDir, SW_SHOWNORMAL);
}

static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(w) == IDC_BROWSE) browse();
        else if (LOWORD(w) == IDC_INSTALL) startInstall();
        else if (LOWORD(w) == IDCANCEL) DestroyWindow(h);
        return 0;
    case WM_STEP:
        stepAt = (int)w;
        creepTo = w >= 35 && w < 80 ? 78 : (int)w + 2;      /* unpacking is one long step: keep moving */
        SendMessageW(progress, PBM_SETPOS, w, 0);
        SetWindowTextW(statusText, (const wchar_t *)l);
        return 0;
    case WM_TIMER:
        if (stepAt && stepAt < 100) {
            int pos = (int)SendMessageW(progress, PBM_GETPOS, 0, 0);
            if (pos < creepTo) SendMessageW(progress, PBM_SETPOS, pos + 1, 0);
        }
        return 0;
    case WM_DONE:
        if (w) {
            KillTimer(h, 1);
            SendMessageW(progress, PBM_SETPOS, 100, 0);
            SetWindowTextW(statusText, oldVersion[0] ? L"NppDev is updated to " APP_VERSION L". Your settings and files were kept."
                                                     : L"NppDev is installed. On its first start it checks for Python, GCC/G++, Java and "
                                                       L"Node.js and offers to install what is missing.");
            SetWindowTextW(cancelBtn, L"Finish");
            EnableWindow(cancelBtn, TRUE);
            if (wantLaunch) { launchApp(); DestroyWindow(h); }
        } else {
            SetWindowTextW(statusText, failText);
            MessageBoxW(h, failText, L"NppDev setup failed", MB_ICONERROR);
            SetWindowTextW(cancelBtn, L"Close");
            EnableWindow(cancelBtn, TRUE);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdLine, int show)
{
    (void)prev; (void)show;
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool doUninstall = false;
    wchar_t local[MAX_PATH] = L"";
    SHGetSpecialFolderPathW(NULL, local, CSIDL_LOCAL_APPDATA, TRUE);
    swprintf(installDir, MAX_PATH, L"%ls\\Programs\\" APP_NAME, local);
    if (argc == 4 && _wcsicmp(argv[1], L"/cleanup") == 0) return cleanup((DWORD)wcstoul(argv[2], NULL, 10), argv[3]);
    removeOldCleanupCopies();
    wchar_t found[MAX_PATH];
    if (findInstalled(found, oldVersion)) wcscpy(installDir, found);    /* update it where it is */
    else oldVersion[0] = 0;
    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"/S") == 0) silent = true;
        else if (_wcsicmp(argv[i], L"/uninstall") == 0) doUninstall = true;
        else if (_wcsicmp(argv[i], L"/nolaunch") == 0) wantLaunch = false;
    }
    /* /D=folder comes last and runs to the end of the line, spaces included (as in other installers) */
    const wchar_t *d = wcsstr(GetCommandLineW(), L" /D=");
    if (d) {
        wcsncpy(installDir, d + 4, MAX_PATH - 1);
        installDir[MAX_PATH - 1] = 0;
        size_t n = wcslen(installDir);
        while (n && (installDir[n - 1] == L' ' || installDir[n - 1] == L'"' || installDir[n - 1] == L'\\')) installDir[--n] = 0;
        if (installDir[0] == L'"') memmove(installDir, installDir + 1, n * sizeof(wchar_t));
    }
    (void)cmdLine;
    if (doUninstall) return uninstall();
    if (silent) {
        if (appRunning()) return 3;
        bool ok = install();
        if (ok && wantLaunch) launchApp();
        return ok ? 0 : 1;
    }

    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    NONCLIENTMETRICSW ncm = { sizeof ncm };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    uiFont = CreateFontIndirectW(&ncm.lfMessageFont);
    UINT dpi = GetDpiForSystem();
#define S(v) MulDiv((v), dpi, 96)

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"NppDevSetup";
    RegisterClassW(&wc);
    RECT r = { 0, 0, S(520), S(318) };
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    mainWnd = CreateWindowExW(0, wc.lpszClassName, L"NppDev " APP_VERSION L" Setup", WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);

    child(L"STATIC", L"NppDev " APP_VERSION L" - Notepad++ Dev Edition   (by " APP_AUTHOR L")", SS_LEFT, S(20), S(16), S(480), S(22), 0);
    child(L"STATIC", L"Notepad++ with Run (Ctrl+Enter) and an Input / Output panel, IntelliSense for C, C++, Java, "
                     L"Python and JavaScript, snippets (sout + Tab), a VS Code style Explorer, Quick Open, "
                     L"Command Palette, dark and light themes and VS Code keys.",
          SS_LEFT, S(20), S(42), S(480), S(48), 0);
    child(L"STATIC", L"Install folder (no administrator rights needed):", SS_LEFT, S(20), S(100), S(480), S(18), 0);
    dirEdit = child(L"EDIT", installDir, ES_AUTOHSCROLL | WS_TABSTOP, S(20), S(120), S(390), S(24), IDC_DIR);
    browseBtn = child(L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, S(418), S(119), S(82), S(26), IDC_BROWSE);
    desktopBox = child(L"BUTTON", L"Desktop shortcut", BS_AUTOCHECKBOX | WS_TABSTOP, S(20), S(156), S(200), S(20), IDC_DESKTOP);
    launchBox = child(L"BUTTON", L"Start NppDev when done", BS_AUTOCHECKBOX | WS_TABSTOP, S(240), S(156), S(240), S(20), IDC_LAUNCH);
    SendMessageW(desktopBox, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(launchBox, BM_SETCHECK, BST_CHECKED, 0);
    progress = child(PROGRESS_CLASSW, L"", 0, S(20), S(190), S(480), S(16), IDC_PROGRESS);
    statusText = child(L"STATIC", L"", SS_LEFT, S(20), S(212), S(480), S(54), IDC_STATUS);
    int cmp = oldVersion[0] ? compareVersions(oldVersion, APP_VERSION) : 0;
    installBtn = child(L"BUTTON", !oldVersion[0] ? L"Install" : cmp < 0 ? L"Update" : L"Reinstall",
                       BS_DEFPUSHBUTTON | WS_TABSTOP, S(318), S(276), S(88), S(28), IDC_INSTALL);
    if (oldVersion[0]) {                                   /* say what will happen */
        static wchar_t note[256];
        if (cmp < 0) swprintf(note, 256, L"NppDev %ls is installed here. Update replaces it with " APP_VERSION L"; your settings, theme and files are kept.", oldVersion);
        else if (cmp == 0) swprintf(note, 256, L"NppDev %ls is already installed here. Reinstall puts fresh program files back; your settings are kept.", oldVersion);
        else swprintf(note, 256, L"A newer NppDev (%ls) is installed here. Reinstall replaces it with the older " APP_VERSION L".", oldVersion);
        SetWindowTextW(statusText, note);
    }
    cancelBtn = child(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, S(412), S(276), S(88), S(28), IDCANCEL);
    ShowWindow(mainWnd, SW_SHOW);
    SetFocus(installBtn);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (!IsDialogMessageW(mainWnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    return 0;
}
