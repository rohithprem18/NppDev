/*
 * VS Code style pickers:
 *   Quick Open (Ctrl+P)        fuzzy-find a file in the workspace folders; "name:42" jumps to line 42
 *   Command Palette (Ctrl+Shift+P)  fuzzy-find any Notepad++ menu command and run it
 *   Open Recent Folder (Ctrl+Shift+R)  fuzzy-find a recent folder and switch to it; Del removes one
 */
#include "devtools.h"

#define IDC_PICK_EDIT 3001
#define IDC_PICK_LIST 3002
#define MAX_SHOWN 300

typedef struct { wchar_t *label, *detail, *match; UINT_PTR data; bool dim, gone; int score; } PickItem;

static PickItem *items;
static int itemCount, itemCap;
static int *order, orderCount;
static int chosen;
static bool pathMode;                     /* Quick Open: match the file name first */
static WNDPROC oldEditProc;
static HWND pickDlg, pickEdit, pickList;
static wchar_t pickHint[128], pickQuery[256];
static bool (*pickRemove)(int item);       /* Del on an item: true = it was removed */

static wchar_t *dupw(const wchar_t *s) { return _wcsdup(s ? s : L""); }

static void addItem(const wchar_t *label, const wchar_t *detail, const wchar_t *match, UINT_PTR data, bool dim)
{
    if (itemCount == itemCap) {
        itemCap = itemCap ? itemCap * 2 : 512;
        items = (PickItem *)realloc(items, itemCap * sizeof *items);
    }
    PickItem *it = &items[itemCount++];
    it->label = dupw(label); it->detail = dupw(detail); it->match = dupw(match);
    it->data = data; it->dim = dim; it->gone = false; it->score = 0;
}

static void freeItems(void)
{
    for (int i = 0; i < itemCount; i++) { free(items[i].label); free(items[i].detail); free(items[i].match); }
    free(items); items = NULL; itemCount = itemCap = 0;
    free(order); order = NULL; orderCount = 0;
}

/* Subsequence match with bonuses for word starts and consecutive letters; -1 = no match. */
static int fuzzy(const wchar_t *q, const wchar_t *s)
{
    if (!*q) return 1;
    int score = 0, qi = 0, prev = -2;
    for (int i = 0; s[i] && q[qi]; i++) {
        if (q[qi] == L' ') { qi++; i--; continue; }
        if (towlower(s[i]) != towlower(q[qi])) continue;
        int bonus = 1;
        if (i == 0 || wcschr(L" \\/_-.>:›", s[i - 1]) || (iswupper(s[i]) && iswlower(s[i - 1]))) bonus += 8;
        if (prev == i - 1) bonus += 5;
        score += bonus; prev = i; qi++;
    }
    while (q[qi] == L' ') qi++;
    return q[qi] ? -1 : score;
}

static int cmpScore(const void *a, const void *b)
{
    const PickItem *x = &items[*(const int *)a], *y = &items[*(const int *)b];
    if (x->score != y->score) return y->score - x->score;
    return (int)wcslen(x->match) - (int)wcslen(y->match);
}

static void splitLineSuffix(const wchar_t *q, wchar_t *text, int *line)
{
    wcsncpy(text, q, 255); text[255] = 0;
    *line = 0;
    wchar_t *c = wcsrchr(text, L':');
    if (pathMode && c && c[1] && wcsspn(c + 1, L"0123456789") == wcslen(c + 1)) { *line = _wtoi(c + 1); *c = 0; }
}

static void refilter(void)
{
    wchar_t q[256], text[256];
    int line;
    GetWindowTextW(pickEdit, q, 256);
    splitLineSuffix(q, text, &line);
    bool hasSlash = wcspbrk(text, L"\\/") != NULL;
    orderCount = 0;
    for (int i = 0; i < itemCount; i++) {
        int s;
        if (items[i].gone) continue;
        if (pathMode && !hasSlash) {
            int a = fuzzy(text, items[i].label), b = fuzzy(text, items[i].match);
            s = a >= 0 ? a * 2 + 5 : b;
        } else s = fuzzy(text, items[i].match);
        if (s < 0) continue;
        items[i].score = s;
        order[orderCount++] = i;
    }
    if (text[0]) qsort(order, orderCount, sizeof *order, cmpScore);
    SendMessageW(pickList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(pickList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < orderCount && i < MAX_SHOWN; i++) SendMessageW(pickList, LB_ADDSTRING, 0, (LPARAM)order[i]);
    SendMessageW(pickList, LB_SETCURSEL, 0, 0);
    SendMessageW(pickList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(pickList, NULL, TRUE);
}

static LRESULT CALLBACK editProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_KEYDOWN && (w == VK_DOWN || w == VK_UP || w == VK_NEXT || w == VK_PRIOR)) {
        int n = (int)SendMessageW(pickList, LB_GETCOUNT, 0, 0), cur = (int)SendMessageW(pickList, LB_GETCURSEL, 0, 0);
        int step = (w == VK_DOWN) ? 1 : (w == VK_UP) ? -1 : (w == VK_NEXT) ? 10 : -10;
        int next = cur + step;
        if (next < 0) next = 0;
        if (next >= n) next = n - 1;
        SendMessageW(pickList, LB_SETCURSEL, next, 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && w == VK_DELETE && pickRemove && (GetWindowTextLengthW(h) == 0 || GetKeyState(VK_SHIFT) < 0)) {
        int sel = (int)SendMessageW(pickList, LB_GETCURSEL, 0, 0);       /* Del with an empty search box (or Shift+Del) */
        int item = sel >= 0 ? (int)SendMessageW(pickList, LB_GETITEMDATA, sel, 0) : -1;
        if (item >= 0 && pickRemove(item)) {
            items[item].gone = true;
            refilter();
            int n = (int)SendMessageW(pickList, LB_GETCOUNT, 0, 0);
            SendMessageW(pickList, LB_SETCURSEL, sel < n ? sel : n - 1, 0);
        }
        return 0;
    }
    if (msg == WM_GETDLGCODE && (w == VK_DOWN || w == VK_UP)) return DLGC_WANTARROWS;
    return CallWindowProcW(oldEditProc, h, msg, w, l);
}

static void acceptPick(HWND h)
{
    int sel = (int)SendMessageW(pickList, LB_GETCURSEL, 0, 0);
    chosen = sel >= 0 ? (int)SendMessageW(pickList, LB_GETITEMDATA, sel, 0) : -1;
    GetWindowTextW(pickEdit, pickQuery, 256);
    EndDialog(h, chosen >= 0);
}

static INT_PTR CALLBACK pickProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_INITDIALOG: {
        pickDlg = h;
        pickEdit = GetDlgItem(h, IDC_PICK_EDIT);
        pickList = GetDlgItem(h, IDC_PICK_LIST);
        SendMessageW(pickEdit, EM_SETCUEBANNER, TRUE, (LPARAM)pickHint);
        oldEditProc = (WNDPROC)SetWindowLongPtrW(pickEdit, GWLP_WNDPROC, (LONG_PTR)editProc);
        SendMessageW(npp._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME, DMF_INIT, (LPARAM)h);
        RECT nr, dr;                                      /* top centre of the Notepad++ window, like VS Code */
        GetWindowRect(npp._nppHandle, &nr);
        GetWindowRect(h, &dr);
        int dw = dr.right - dr.left;
        SetWindowPos(h, NULL, nr.left + (nr.right - nr.left - dw) / 2, nr.top + 90, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        refilter();
        SetFocus(pickEdit);
        return FALSE;
    }
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) PostMessageW(h, WM_COMMAND, IDCANCEL, 0);   /* click outside closes it */
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(w) == IDC_PICK_EDIT && HIWORD(w) == EN_CHANGE) { refilter(); return TRUE; }
        if (LOWORD(w) == IDC_PICK_LIST && HIWORD(w) == LBN_DBLCLK) { acceptPick(h); return TRUE; }
        if (LOWORD(w) == IDOK) { acceptPick(h); return TRUE; }
        if (LOWORD(w) == IDCANCEL) { chosen = -1; EndDialog(h, 0); return TRUE; }
        return FALSE;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *m = (MEASUREITEMSTRUCT *)l;
        HDC dc = GetDC(h);
        HFONT old = SelectObject(dc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
        TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
        SelectObject(dc, old); ReleaseDC(h, dc);
        m->itemHeight = tm.tmHeight + 8;
        return TRUE;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)l;
        if (d->itemID == (UINT)-1) return TRUE;
        PickItem *it = &items[d->itemData];
        bool sel = (d->itemState & ODS_SELECTED) != 0;
        HBRUSH bg = CreateSolidBrush(sel ? RGB(0x1F, 0x3A, 0x5F) : editorBack());
        FillRect(d->hDC, &d->rcItem, bg);
        DeleteObject(bg);
        SetBkMode(d->hDC, TRANSPARENT);
        HFONT old = SelectObject(d->hDC, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
        RECT r = d->rcItem; r.left += 8; r.right -= 8;
        SIZE sz; GetTextExtentPoint32W(d->hDC, it->label, (int)wcslen(it->label), &sz);
        SetTextColor(d->hDC, it->dim ? RGB(0x6E, 0x76, 0x81) : editorFore());
        DrawTextW(d->hDC, it->label, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT r2 = r; r2.left += sz.cx + 16;
        SetTextColor(d->hDC, RGB(0x8B, 0x94, 0x9E));
        DrawTextW(d->hDC, it->detail, -1, &r2, (pathMode ? DT_LEFT | DT_PATH_ELLIPSIS : DT_RIGHT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(d->hDC, old);
        return TRUE;
    }
    }
    return FALSE;
}

/* One picker at a time: they share the item list. */
static bool pickerShown(void) { return pickDlg && IsWindow(pickDlg); }

/* Show the picker; returns the chosen item index or -1. */
static int runPicker(const wchar_t *hint, bool paths)
{
    pathMode = paths;
    wcsncpy(pickHint, hint, 127);
    order = (int *)malloc((itemCount + 1) * sizeof *order);
    static WORD tmpl[512];
    memset(tmpl, 0, sizeof tmpl);
    WORD *p = dlgBegin(tmpl, WS_POPUP | WS_BORDER, 330, 196, 2, L"");
    p = dlgPutItem(p, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 4, 4, 322, 14, IDC_PICK_EDIT, 0x0081, L"");
    p = dlgPutItem(p, WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 4, 22, 322, 170, IDC_PICK_LIST, 0x0083, L"");
    chosen = -1;
    DialogBoxIndirectParamW(hInst, (DLGTEMPLATE *)dlgAlign(tmpl), npp._nppHandle, pickProc, 0);
    return chosen;
}

/* ---------------- Quick Open ---------------- */

static bool skipDir(const wchar_t *name)
{
    static const wchar_t *skip[] = { L".git", L".svn", L".hg", L"node_modules", L"__pycache__", L".venv", L"venv",
                                     L".idea", L".vs", L".vscode", L".mypy_cache", L".pytest_cache" };
    for (size_t i = 0; i < sizeof skip / sizeof skip[0]; i++) if (_wcsicmp(name, skip[i]) == 0) return true;
    return false;
}

static void scanFolder(const wchar_t *root, const wchar_t *dir, int depth)
{
    if (depth > 12 || itemCount > 30000) return;
    wchar_t pat[MAX_PATH];
    if (swprintf(pat, MAX_PATH, L"%ls\\*", dir) < 0) return;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.' && (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2]))) continue;
        wchar_t full[MAX_PATH];
        if (swprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName) < 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!skipDir(fd.cFileName) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) scanFolder(root, full, depth + 1);
            continue;
        }
        const wchar_t *rel = full + wcslen(root);
        if (*rel == L'\\') rel++;
        wchar_t relDir[MAX_PATH];
        wcscpy(relDir, rel);
        wchar_t *slash = wcsrchr(relDir, L'\\');
        if (slash) *slash = 0; else wcscpy(relDir, L"");
        wchar_t detail[MAX_PATH + 4];
        swprintf(detail, sizeof detail / sizeof detail[0], L"%ls%ls%ls", leafOf(root), relDir[0] ? L"\\" : L"", relDir);
        addItem(fd.cFileName, detail, rel, 0, false);
        items[itemCount - 1].data = (UINT_PTR)_wcsdup(full);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

void quickOpen(void)
{
    if (pickerShown()) return;
    wchar_t roots[8][MAX_PATH];
    int n = workspaceRoots(roots, 8);
    if (n == 0) {                                         /* no workspace: the current file's folder */
        SendMessageW(npp._nppHandle, NPPM_GETCURRENTDIRECTORY, MAX_PATH, (LPARAM)roots[0]);
        if (roots[0][0] && isDir(roots[0])) n = 1;
    }
    if (n == 0) { MessageBoxW(npp._nppHandle, L"Open a folder first (File > Open Folder as Workspace).", L"Quick Open", MB_ICONINFORMATION); return; }
    for (int i = 0; i < n; i++) scanFolder(roots[i], roots[i], 0);
    int pick = runPicker(L"Search files by name  (add :42 to jump to line 42)", true);
    if (pick >= 0) {
        const wchar_t *path = (const wchar_t *)items[pick].data;
        SendMessageW(npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)path);
        wchar_t text[256]; int line;
        splitLineSuffix(pickQuery, text, &line);
        if (line > 0) {
            int which = 0;
            SendMessageW(npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
            HWND ed = which ? npp._scintillaSecondHandle : npp._scintillaMainHandle;
            SendMessageW(ed, SCI_ENSUREVISIBLE, line - 1, 0);
            SendMessageW(ed, SCI_GOTOLINE, line - 1, 0);
            SendMessageW(ed, SCI_SCROLLCARET, 0, 0);
        }
    }
    for (int i = 0; i < itemCount; i++) free((void *)items[i].data);
    freeItems();
}

/* ---------------- Command Palette ---------------- */

static void cleanMenuText(const wchar_t *raw, wchar_t *label, wchar_t *keys)
{
    keys[0] = 0;
    int j = 0;
    for (int i = 0; raw[i] && j < 255; i++) {
        if (raw[i] == L'\t') { wcsncpy(keys, raw + i + 1, 63); keys[63] = 0; break; }
        if (raw[i] == L'&') { if (raw[i + 1] == L'&') label[j++] = raw[++i]; continue; }
        label[j++] = raw[i];
    }
    label[j] = 0;
}

static void collectMenu(HMENU m, const wchar_t *prefix, int depth)
{
    if (depth > 4) return;
    int n = GetMenuItemCount(m);
    for (int i = 0; i < n; i++) {
        wchar_t raw[256] = L"", label[256], keys[64];
        MENUITEMINFOW mi = { sizeof mi };
        mi.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU | MIIM_STATE | MIIM_STRING;
        mi.dwTypeData = raw; mi.cch = 255;
        if (!GetMenuItemInfoW(m, i, TRUE, &mi) || (mi.fType & MFT_SEPARATOR) || !raw[0]) continue;
        cleanMenuText(raw, label, keys);
        wchar_t path[512];
        swprintf(path, 512, L"%ls%ls%ls", prefix, prefix[0] ? L" › " : L"", label);
        if (mi.hSubMenu) {
            SendMessageW(npp._nppHandle, WM_INITMENUPOPUP, (WPARAM)mi.hSubMenu, MAKELPARAM(i, FALSE));   /* fresh state */
            collectMenu(mi.hSubMenu, path, depth + 1);
        } else if (mi.wID && mi.wID != (UINT)-1 && depth > 0) {
            addItem(path, keys, path, mi.wID, (mi.fState & MFS_DISABLED) != 0);
        }
    }
}

void commandPalette(void)
{
    HMENU bar = GetMenu(npp._nppHandle);
    if (!bar || pickerShown()) return;
    collectMenu(bar, L"", 0);
    int pick = runPicker(L"Type a command name  (e.g. \"save all\", \"word wrap\", \"run\")", false);
    UINT id = pick >= 0 ? (UINT)items[pick].data : 0;
    freeItems();
    if (id) PostMessageW(npp._nppHandle, WM_COMMAND, id, 0);
}

/* ---------------- Open Recent Folder ---------------- */

static wchar_t (*recent)[MAX_PATH];

static bool removeRecent(int item)
{
    if (!items[item].data) return false;                  /* "Open Folder..." stays */
    forgetRecentFolder(recent[items[item].data - 1]);
    return true;
}

void recentFolderPicker(void)
{
    static wchar_t list[64][MAX_PATH];
    if (pickerShown()) return;
    recent = list;
    int n = recentFolderList(list, 64);
    for (int i = 0; i < n; i++) {
        wchar_t shown[MAX_PATH + 2];                       /* VS Code style: ~\Documents\... */
        size_t home = wcslen(homeDir);
        if (home && _wcsnicmp(list[i], homeDir, home) == 0 && list[i][home] == L'\\') swprintf(shown, MAX_PATH + 2, L"~%ls", list[i] + home);
        else wcscpy(shown, list[i]);
        addItem(leafOf(list[i]), shown, list[i], i + 1, false);
    }
    addItem(L"Open Folder...", L"Ctrl+Shift+O", L"Open Folder...", 0, true);
    pickRemove = removeRecent;
    int pick = runPicker(L"Open a recent folder  (Del removes it from the list)", true);
    pickRemove = NULL;
    wchar_t folder[MAX_PATH] = L"";
    bool browse = pick >= 0 && !items[pick].data;
    if (pick >= 0 && items[pick].data) wcscpy(folder, list[items[pick].data - 1]);
    freeItems();
    if (browse) openFolderCommand();
    else if (folder[0]) openRecentFolder(folder);
}
