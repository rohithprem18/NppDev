/*
 * Updates: ask GitHub for the latest NppDev release, offer it, download and verify the setup, then
 * let the setup replace this install (it waits until NppDev has closed and starts it again).
 *
 *   - at start: once a day, in the background, only in an installed NppDev (bundleMode)
 *   - Plugins > Dev Tools > Check for Updates... : any time, with an answer either way
 *
 * The setup is checked against the SHA-256 that GitHub publishes for it ("digest"), so only the file
 * this repository released is run. Files downloaded here carry no "from the internet" mark.
 */
#include "devtools.h"
#include <winhttp.h>
#include <bcrypt.h>
#include "version.h"                     /* VER_TEXT: this build's version (written by build.ps1) */
#define WIDE2(x) L##x
#define WIDE(x)  WIDE2(x)

#define REPO_API_HOST   L"api.github.com"
#define REPO_API_PATH   L"/repos/rohithprem18/NppDev/releases/latest"
#define RELEASES_URL    L"https://github.com/rohithprem18/NppDev/releases"
#define ASSET_NAME      "NppDev-Setup.exe"
#define WM_UPDATE_FOUND (WM_APP + 0x70)  /* lParam: Release* (freed by the receiver), wParam: manual */
#define DAY_SECONDS     (24 * 60 * 60)

typedef struct {
    bool ok;
    wchar_t error[256];
    wchar_t version[32];                 /* "1.6" */
    wchar_t url[1024];                   /* the setup's download link */
    char sha256[72];                     /* hex, from GitHub's "digest" */
    LONGLONG size;
    wchar_t notes[2048];                 /* "What's new", plain text */
} Release;

static HWND msgWnd;
static bool checking;

/* ---------------- small helpers ---------------- */

static int compareVersions(const wchar_t *a, const wchar_t *b)        /* like strcmp, "1.10" > "1.9" */
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

static void iniPath(wchar_t *out)                                      /* <plugins config>\DevTools\update.ini */
{
    wchar_t dir[MAX_PATH] = L"";
    SendMessageW(npp._nppHandle, NPPM_GETPLUGINSCONFIGDIR, MAX_PATH, (LPARAM)dir);
    swprintf(out, MAX_PATH, L"%ls\\DevTools", dir);
    CreateDirectoryW(out, NULL);
    wcscat(out, L"\\update.ini");
}

static LONGLONG nowSeconds(void)
{
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    return (LONGLONG)((((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000ULL);
}

/* ---------------- HTTPS ---------------- */

typedef struct { volatile LONGLONG done, total; volatile bool cancel; } Progress;

/* GET a URL: into memory (*body, NUL-terminated) or into a file (hashing it on the way). */
static bool httpGet(const wchar_t *url, char **body, HANDLE file, BCRYPT_HASH_HANDLE hash, Progress *pr, wchar_t *err)
{
    URL_COMPONENTS u = { sizeof u };
    wchar_t host[256], path[2048];
    u.lpszHostName = host; u.dwHostNameLength = 256;
    u.lpszUrlPath = path; u.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url, 0, 0, &u)) { wcscpy(err, L"Bad address"); return false; }
    bool ok = false;
    HINTERNET s = WinHttpOpen(L"NppDev/" WIDE(VER_TEXT), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, u.nPort, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         u.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    if (r) {
        WinHttpAddRequestHeaders(r, L"Accept: application/vnd.github+json", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        if (WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
            DWORD status = 0, n = sizeof status;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &n, NULL);
            if (status == 200) {
                wchar_t len[32]; DWORD ln = sizeof len;
                if (pr && WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, NULL, len, &ln, NULL)) pr->total = _wtoi64(len);
                size_t cap = 1 << 16, used = 0;
                char *buf = body ? malloc(cap) : NULL;
                static char chunk[1 << 16];
                ok = true;
                for (;;) {
                    DWORD got = 0;
                    if (pr && pr->cancel) { ok = false; wcscpy(err, L"Cancelled"); break; }
                    if (!WinHttpReadData(r, chunk, sizeof chunk, &got)) { ok = false; wcscpy(err, L"The download stopped"); break; }
                    if (!got) break;
                    if (body) {
                        if (used + got + 1 > cap) { cap = (used + got + 1) * 2; buf = realloc(buf, cap); }
                        memcpy(buf + used, chunk, got);
                    }
                    if (file) { DWORD put; if (!WriteFile(file, chunk, got, &put, NULL)) { ok = false; wcscpy(err, L"Could not save the file"); break; } }
                    if (hash) BCryptHashData(hash, (PUCHAR)chunk, got, 0);
                    used += got;
                    if (pr) pr->done = (LONGLONG)used;
                }
                if (body) { if (ok) { buf[used] = 0; *body = buf; } else free(buf); }
            } else swprintf(err, 256, L"GitHub answered %lu", status);
        } else swprintf(err, 256, L"No connection to GitHub (error %lu)", GetLastError());
    } else wcscpy(err, L"No connection to GitHub");
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return ok;
}

/* ---------------- the release, from GitHub's JSON ---------------- */

/* The value after "key": in json (from `from` on): a pointer to its first character, or NULL. */
static const char *jsonValue(const char *from, const char *key)
{
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    for (const char *p = strstr(from, pat); p; p = strstr(p + 1, pat)) {
        const char *q = p + strlen(pat);
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q != ':') continue;
        q++;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        return q;
    }
    return NULL;
}

/* A JSON string at p (pointing at its opening quote), unescaped into UTF-16. */
static void jsonString(const char *p, wchar_t *out, int cap)
{
    int n = 0;
    if (!p || *p != '"') { out[0] = 0; return; }
    char *utf8 = malloc(strlen(p) + 1); int m = 0;
    for (p++; *p && *p != '"'; p++) {
        if (*p != '\\') { utf8[m++] = *p; continue; }
        p++;
        switch (*p) {
        case 'n': utf8[m++] = '\n'; break;
        case 'r': break;
        case 't': utf8[m++] = ' '; break;
        case 'u': {                                     /* \uXXXX: as UTF-8 (BMP only) */
            unsigned v = 0; sscanf(p + 1, "%4x", &v); p += 4;
            if (v < 0x80) utf8[m++] = (char)v;
            else if (v < 0x800) { utf8[m++] = (char)(0xC0 | v >> 6); utf8[m++] = (char)(0x80 | (v & 63)); }
            else { utf8[m++] = (char)(0xE0 | v >> 12); utf8[m++] = (char)(0x80 | (v >> 6 & 63)); utf8[m++] = (char)(0x80 | (v & 63)); }
            break;
        }
        case 0: p--; break;
        default: utf8[m++] = *p;                       /* \" \\ \/ */
        }
    }
    utf8[m] = 0;
    n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap);
    if (!n) out[0] = 0;
    out[cap - 1] = 0;
    free(utf8);
}

/* Release notes as plain text: from "What's new" on when there is such a heading, without markdown. */
static void plainNotes(wchar_t *notes)
{
    wchar_t *from = wcsstr(notes, L"What's new");
    if (from) { wchar_t *nl = wcschr(from, L'\n'); memmove(notes, nl ? nl + 1 : from, (wcslen(nl ? nl + 1 : from) + 1) * sizeof(wchar_t)); }
    wchar_t *w = notes;
    for (wchar_t *r = notes; *r; r++)
        if (*r != L'*' && *r != L'`' && *r != L'#') *w++ = *r;
    *w = 0;
    while (w > notes && (w[-1] == L'\n' || w[-1] == L' ')) *--w = 0;
}

static DWORD WINAPI checkThread(LPVOID manual)
{
    Release *rel = calloc(1, sizeof *rel);
    char *json = NULL;
    if (httpGet(L"https://" REPO_API_HOST REPO_API_PATH, &json, NULL, NULL, NULL, rel->error)) {
        wchar_t tag[64];
        jsonString(jsonValue(json, "tag_name"), tag, 64);
        wcscpy(rel->version, tag[0] == L'v' || tag[0] == L'V' ? tag + 1 : tag);
        jsonString(jsonValue(json, "body"), rel->notes, 2048);
        plainNotes(rel->notes);
        const char *assets = jsonValue(json, "assets");
        const char *name = assets ? strstr(assets, "\"" ASSET_NAME "\"") : NULL;
        if (name) {
            wchar_t digest[96];
            jsonString(jsonValue(name, "digest"), digest, 96);
            if (_wcsnicmp(digest, L"sha256:", 7) == 0) WideCharToMultiByte(CP_UTF8, 0, digest + 7, -1, rel->sha256, sizeof rel->sha256, NULL, NULL);
            jsonString(jsonValue(name, "browser_download_url"), rel->url, 1024);
            const char *size = jsonValue(name, "size");
            if (size) rel->size = _atoi64(size);
        }
        if (!rel->version[0]) wcscpy(rel->error, L"GitHub's answer had no version");
        else if (!rel->url[0]) wcscpy(rel->error, L"The latest release has no NppDev-Setup.exe");
        else rel->ok = true;
        free(json);
    }
    PostMessageW(msgWnd, WM_UPDATE_FOUND, (WPARAM)manual, (LPARAM)rel);
    return 0;
}

/* ---------------- downloading, with a progress dialog ---------------- */

typedef struct { Release rel; Progress pr; wchar_t file[MAX_PATH]; volatile bool ok, finished; wchar_t error[256]; HWND dlg; } Download;

static DWORD WINAPI downloadThread(LPVOID arg)
{
    Download *d = arg;
    BCRYPT_ALG_HANDLE alg = NULL; BCRYPT_HASH_HANDLE hash = NULL;
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (alg) BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0);
    HANDLE f = CreateFileW(d->file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) wcscpy(d->error, L"Could not write to the temp folder");
    else {
        d->ok = httpGet(d->rel.url, NULL, f, hash, &d->pr, d->error);
        CloseHandle(f);
        if (d->ok) {
            UCHAR h[32]; char hex[65];
            BCryptFinishHash(hash, h, 32, 0);
            for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", h[i]);
            if (!d->rel.sha256[0]) { d->ok = false; wcscpy(d->error, L"GitHub gave no checksum for the setup, so it can't be verified"); }
            else if (_stricmp(hex, d->rel.sha256) != 0) { d->ok = false; wcscpy(d->error, L"The download doesn't match GitHub's checksum"); }
        }
        if (!d->ok) DeleteFileW(d->file);
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    d->finished = true;
    if (d->dlg) PostMessageW(d->dlg, TDM_CLICK_BUTTON, IDCANCEL, 0);   /* done: close the progress dialog */
    return 0;
}

static HRESULT CALLBACK downloadDialogProc(HWND h, UINT n, WPARAM w, LPARAM l, LONG_PTR ref)
{
    (void)l;
    Download *d = (Download *)ref;
    if (n == TDN_CREATED) {
        d->dlg = h;
        HANDLE t = CreateThread(NULL, 0, downloadThread, d, 0, NULL);
        if (t) CloseHandle(t);
    } else if (n == TDN_TIMER) {
        LONGLONG total = d->pr.total > 0 ? d->pr.total : d->rel.size;
        if (total > 0) {
            int pct = (int)(d->pr.done * 100 / total);
            SendMessageW(h, TDM_SET_PROGRESS_BAR_POS, pct > 100 ? 100 : pct, 0);
            wchar_t text[128];
            swprintf(text, 128, L"%.1f MB of %.1f MB", d->pr.done / 1048576.0, total / 1048576.0);
            SendMessageW(h, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)text);
        }
    } else if (n == TDN_BUTTON_CLICKED && w == IDCANCEL && !d->finished) {
        d->pr.cancel = true;
    }
    return S_OK;
}

static void installUpdate(Release *rel)
{
    Download *d = calloc(1, sizeof *d);
    d->rel = *rel;                                        /* its own copy: the thread may outlive the caller's */
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    wcscat(dir, L"NppDev-Update");
    CreateDirectoryW(dir, NULL);
    swprintf(d->file, MAX_PATH, L"%ls\\NppDev-Setup-%ls.exe", dir, rel->version);

    wchar_t title[96];
    swprintf(title, 96, L"Downloading NppDev %ls", rel->version);
    TASKDIALOGCONFIG tc = { sizeof tc };
    tc.hwndParent = npp._nppHandle;
    tc.dwFlags = TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER | TDF_POSITION_RELATIVE_TO_WINDOW;
    tc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    tc.pszWindowTitle = L"NppDev update";
    tc.pszMainInstruction = title;
    tc.pszContent = L"Starting...";
    tc.pfCallback = downloadDialogProc;
    tc.lpCallbackData = (LONG_PTR)d;
    int button = 0;
    TaskDialogIndirect(&tc, &button, NULL, NULL);
    if (!d->finished) d->pr.cancel = true;               /* closed early: stop the download */
    if (!d->finished || !d->ok) {
        if (d->finished && wcscmp(d->error, L"Cancelled") != 0 && d->error[0]) {
            wchar_t msg[512];
            swprintf(msg, 512, L"The update couldn't be downloaded: %ls.\n\nTry again later, or get it from %ls", d->error, RELEASES_URL);
            MessageBoxW(npp._nppHandle, msg, L"NppDev update", MB_ICONWARNING);
        }
        return;                                           /* d stays: the thread may still be using it */
    }

    /* The setup waits until this NppDev has closed, updates this folder and starts NppDev again. */
    wchar_t app[MAX_PATH], cmd[3 * MAX_PATH + 64];
    GetModuleFileNameW(NULL, app, MAX_PATH);
    *wcsrchr(app, L'\\') = 0;
    swprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" /auto /wait=%lu /D=%ls", d->file, GetCurrentProcessId(), app);
    STARTUPINFOW si = { sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MessageBoxW(npp._nppHandle, L"The setup couldn't be started.", L"NppDev update", MB_ICONWARNING);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    PostMessageW(npp._nppHandle, WM_CLOSE, 0, 0);         /* your open files and settings are kept */
}

/* ---------------- offering it ---------------- */

static void offer(Release *rel, bool manual)
{
    wchar_t ini[MAX_PATH], skipped[32] = L"";
    iniPath(ini);
    GetPrivateProfileStringW(L"update", L"skip", L"", skipped, 32, ini);
    if (!manual && _wcsicmp(skipped, rel->version) == 0) return;

    wchar_t head[128], body[256];
    swprintf(head, 128, L"NppDev %ls is available", rel->version);
    swprintf(body, 256, L"You have NppDev %hs. Updating keeps your settings, theme and open files; NppDev closes and "
             L"opens again by itself.", VER_TEXT);
    TASKDIALOG_BUTTON buttons[] = { { 101, L"Update now\nDownload, verify and install it" }, { 102, L"Later" },
                                    { 103, L"Skip this version" } };
    TASKDIALOGCONFIG tc = { sizeof tc };
    tc.hwndParent = npp._nppHandle;
    tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_POSITION_RELATIVE_TO_WINDOW;
    tc.pszWindowTitle = L"NppDev update";
    tc.pszMainIcon = TD_INFORMATION_ICON;
    tc.pszMainInstruction = head;
    tc.pszContent = body;
    tc.pButtons = buttons;
    tc.cButtons = 3;
    tc.nDefaultButton = 101;
    if (rel->notes[0]) {
        tc.pszExpandedInformation = rel->notes;
        tc.pszExpandedControlText = L"Hide what's new";
        tc.pszCollapsedControlText = L"What's new";
    }
    int button = 0;
    if (FAILED(TaskDialogIndirect(&tc, &button, NULL, NULL))) return;
    if (button == 101) installUpdate(rel);
    else if (button == 103) WritePrivateProfileStringW(L"update", L"skip", rel->version, ini);
}

static LRESULT CALLBACK msgProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_UPDATE_FOUND) {
        Release *rel = (Release *)l;
        bool manual = w != 0;
        checking = false;
        if (!rel->ok) {
            if (manual) {
                wchar_t text[512];
                swprintf(text, 512, L"Couldn't check for updates: %ls.\n\nYou can look at %ls", rel->error, RELEASES_URL);
                MessageBoxW(npp._nppHandle, text, L"NppDev update", MB_ICONWARNING);
            }
        } else {
            wchar_t ini[MAX_PATH], when[32];
            iniPath(ini);
            swprintf(when, 32, L"%lld", nowSeconds());
            WritePrivateProfileStringW(L"update", L"last", when, ini);
            wchar_t mine[32];
            MultiByteToWideChar(CP_UTF8, 0, VER_TEXT, -1, mine, 32);
            if (compareVersions(rel->version, mine) > 0) offer(rel, manual);
            else if (manual) {
                wchar_t text[160];
                swprintf(text, 160, L"You have the latest version: NppDev %ls.", mine);
                MessageBoxW(npp._nppHandle, text, L"NppDev update", MB_ICONINFORMATION);
            }
        }
        free(rel);                                        /* (a running download keeps its own pointer: see installUpdate) */
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

static bool startCheck(bool manual)
{
    if (checking) return true;
    if (!msgWnd) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = msgProc; wc.hInstance = hInst; wc.lpszClassName = L"NppDevUpdate";
        RegisterClassW(&wc);
        msgWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);
        if (!msgWnd) return false;
    }
    HANDLE t = CreateThread(NULL, 0, checkThread, (LPVOID)(INT_PTR)manual, 0, NULL);
    if (!t) return false;
    CloseHandle(t);
    checking = true;
    return true;
}

/* At start (installed NppDev only): at most once a day, quietly unless there is something new. */
void updateCheckAtStartup(void)
{
    if (!bundleMode) return;
    wchar_t old[MAX_PATH], pattern[MAX_PATH];                       /* setups from earlier updates: no longer needed */
    GetTempPathW(MAX_PATH, old);
    wcscat(old, L"NppDev-Update");
    swprintf(pattern, MAX_PATH, L"%ls\\NppDev-Setup-*.exe", old);
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(pattern, &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            wchar_t file[MAX_PATH];
            swprintf(file, MAX_PATH, L"%ls\\%ls", old, fd.cFileName);
            DeleteFileW(file);                                      /* one still running stays */
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    wchar_t ini[MAX_PATH], last[32] = L"";
    iniPath(ini);
    GetPrivateProfileStringW(L"update", L"last", L"0", last, 32, ini);
    if (nowSeconds() - _wtoi64(last) < DAY_SECONDS) return;
    startCheck(false);
}

/* Plugins > Dev Tools > Check for Updates... */
void updateCheckNow(void)
{
    if (!bundleMode) {
        MessageBoxW(npp._nppHandle, L"Updates are for NppDev installed with its setup. This Notepad++ runs the Dev Tools "
                    L"plugin on its own, so it isn't updated from here.\n\nNppDev releases: " RELEASES_URL,
                    L"NppDev update", MB_ICONINFORMATION);
        return;
    }
    if (!startCheck(true)) MessageBoxW(npp._nppHandle, L"Couldn't start the update check.", L"NppDev update", MB_ICONWARNING);
}
