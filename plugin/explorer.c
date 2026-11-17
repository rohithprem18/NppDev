/*
 * VS Code style look for Notepad++'s Folder as Workspace panel:
 *   - an "EXPLORER" header with New File, New Folder, Collapse Folders and a "..." menu
 *     (instead of the panel's row of small unlabeled toolbar icons)
 *   - file-type icons (C, C++, Java, Python, Jupyter, JSON, Markdown, ...) and folder icons
 *   - full-row selection, taller rows, no dotted lines
 *   - when no folder is open: "You have not yet opened a folder" with Open Folder / Open Recent
 * The panel itself stays Notepad++'s; this only restyles it (see hookWorkspacePanel).
 */
#include "devtools.h"

#define HEADER_CLASS L"NppDevExplorerHeader"
#define EMPTY_CLASS  L"NppDevExplorerEmpty"
enum { B_NEWFILE, B_NEWFOLDER, B_COLLAPSE, B_MORE, B_COUNT };
enum { M_LOCATE = 1, M_OPEN, M_RECENT, M_REVEAL, M_CLOSE };
enum { K_FILE, K_FOLDER, K_FOLDER_OPEN };

static HWND panelWnd, toolbarWnd, treeWnd, headerWnd, emptyWnd, tipWnd;
static int hot = -1;                 /* button under the mouse (header) / in the empty view */
static RECT buttons[B_COUNT];
static RECT emptyButtons[2];

/* ---------------- colours and sizes ---------------- */

static int dpiOf(HWND h)
{
    HDC dc = GetDC(h);
    int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(h, dc);
    return d;
}
#define S(v) MulDiv((v), dpiOf(panelWnd), 96)

static bool darkMode(void) { return SendMessageW(npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0; }

static COLORREF backColor(void)
{
    COLORREF c = treeWnd ? TreeView_GetBkColor(treeWnd) : (COLORREF)-1;
    if (c != (COLORREF)-1) return c;
    if (!darkMode()) return GetSysColor(COLOR_WINDOW);
    COLORREF d[12] = { 0 };                                       /* NppDarkMode::Colors */
    if (SendMessageW(npp._nppHandle, NPPMSG + 108 /* NPPM_GETDARKMODECOLORS */, sizeof d, (LPARAM)d)) return d[1];
    return RGB(0x0d, 0x11, 0x17);
}

static COLORREF textColor(void)
{
    COLORREF c = treeWnd ? TreeView_GetTextColor(treeWnd) : (COLORREF)-1;
    if (c != (COLORREF)-1) return c;
    return darkMode() ? RGB(0xc9, 0xd1, 0xd9) : GetSysColor(COLOR_WINDOWTEXT);
}

static COLORREF mix(COLORREF a, COLORREF b, int pctB)
{
    return RGB((GetRValue(a) * (100 - pctB) + GetRValue(b) * pctB) / 100,
               (GetGValue(a) * (100 - pctB) + GetGValue(b) * pctB) / 100,
               (GetBValue(a) * (100 - pctB) + GetBValue(b) * pctB) / 100);
}

static HFONT uiFont(int pt, int weight)
{
    return CreateFontW(-MulDiv(pt, dpiOf(panelWnd), 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

/* ---------------- file-type icons ---------------- */

typedef struct { const wchar_t *exts, *glyph; COLORREF color; } FileType;
#define C(r, g, b) RGB(0x##r, 0x##g, 0x##b)
static const FileType fileTypes[] = {           /* extensions are ";ext;" lists */
    { L";c;",                              L"C",   C(51, 9a, ba) },
    { L";cpp;cc;cxx;c++;",                 L"C+",  C(a0, 74, c4) },
    { L";h;hpp;hh;hxx;",                   L"h",   C(a0, 74, c4) },
    { L";java;",                           L"J",   C(cc, 3e, 44) },
    { L";class;jar;",                      L"J",   C(6d, 80, 86) },
    { L";py;pyw;",                         L"Py",  C(4b, 8b, be) },
    { L";ipynb;",                          L"nb",  C(f3, 76, 26) },
    { L";json;",                           L"{}",  C(cb, cb, 41) },
    { L";md;markdown;",                    L"M",   C(51, 9a, ba) },
    { L";html;htm;xml;xaml;svg;",          L"<>",  C(e3, 79, 33) },
    { L";css;scss;",                       L"#",   C(51, 9a, ba) },
    { L";js;mjs;cjs;",                     L"JS",  C(cb, cb, 41) },
    { L";ts;tsx;",                         L"TS",  C(51, 9a, ba) },
    { L";go;",                             L"Go",  C(51, 9a, ba) },
    { L";rs;",                             L"R",   C(e3, 79, 33) },
    { L";cs;",                             L"C#",  C(59, 9e, 5e) },
    { L";kt;kts;",                         L"K",   C(a0, 74, c4) },
    { L";sql;",                            L"DB",  C(f5, 5d, 85) },
    { L";csv;xlsx;xls;",                   L"☷", C(89, a0, 3d) },
    { L";bat;cmd;ps1;sh;",                 L">_",  C(89, a0, 3d) },
    { L";exe;dll;o;obj;bin;",              L"⚙", C(6d, 80, 86) },
    { L";zip;7z;rar;gz;tar;",              L"Z",   C(cb, cb, 41) },
    { L";png;jpg;jpeg;gif;bmp;ico;webp;",  L"▣", C(a0, 74, c4) },
    { L";txt;log;in;out;ans;",             L"≡", C(8b, 94, 9e) },
};
#undef C

static const FileType *typeOf(const wchar_t *name)
{
    const wchar_t *dot = wcsrchr(name, L'.');
    if (!dot || !dot[1] || wcslen(dot) > 12) return NULL;
    wchar_t key[16];
    swprintf(key, 16, L";%ls;", dot + 1);
    _wcslwr(key);
    for (size_t i = 0; i < sizeof fileTypes / sizeof fileTypes[0]; i++)
        if (wcsstr(fileTypes[i].exts, key)) return &fileTypes[i];
    return NULL;
}

static void drawFolder(HDC dc, RECT r, bool open)
{
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = r.left + w / 16, y = r.top + h * 3 / 16, x2 = r.right - w / 16, y2 = r.bottom - h * 2 / 16;
    HBRUSH back = CreateSolidBrush(open ? RGB(0xa0, 0x7e, 0x46) : RGB(0xc0, 0x95, 0x53));
    HPEN none = (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ op = SelectObject(dc, none), ob = SelectObject(dc, back);
    POINT tab[] = { { x, y }, { x + w * 6 / 16, y }, { x + w * 7 / 16, y + h * 2 / 16 }, { x2, y + h * 2 / 16 }, { x2, y2 }, { x, y2 } };
    Polygon(dc, tab, 6);
    if (open) {                                               /* lighter front flap, tilted */
        HBRUSH front = CreateSolidBrush(RGB(0xdc, 0xb6, 0x7a));
        SelectObject(dc, front);
        POINT flap[] = { { x + w * 3 / 16, y + h * 6 / 16 }, { r.right, y + h * 6 / 16 }, { x2 - w * 2 / 16, y2 }, { x, y2 } };
        Polygon(dc, flap, 4);
        SelectObject(dc, ob);
        DeleteObject(front);
    }
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(back);
}

static void drawPage(HDC dc, RECT r, COLORREF ink)
{
    int w = r.right - r.left, h = r.bottom - r.top;
    HPEN pen = CreatePen(PS_SOLID, MulDiv(1, w, 16) ? MulDiv(1, w, 16) : 1, ink);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int x = r.left + w * 3 / 16, x2 = r.left + w * 13 / 16, y = r.top + h / 16, y2 = r.bottom - h / 16, f = w * 4 / 16;
    POINT p[] = { { x, y }, { x2 - f, y }, { x2, y + f }, { x2, y2 }, { x, y2 }, { x, y } };
    Polyline(dc, p, 6);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
}

static void drawTypeIcon(HDC dc, RECT r, const wchar_t *name, int kind)
{
    if (kind != K_FILE) { drawFolder(dc, r, kind == K_FOLDER_OPEN); return; }
    const FileType *t = typeOf(name);
    if (!t) { drawPage(dc, r, mix(textColor(), backColor(), 35)); return; }
    int h = r.bottom - r.top, len = (int)wcslen(t->glyph);
    bool symbol = t->glyph[0] > 0x2000;
    HFONT f = CreateFontW(-(len > 1 ? h * 62 / 100 : h * 86 / 100), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                          CLEARTYPE_QUALITY, 0, symbol ? L"Segoe UI Symbol" : L"Segoe UI");
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, t->color);
    DrawTextW(dc, t->glyph, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    SelectObject(dc, of);
    DeleteObject(f);
}

/* Folder or file? Folders have children or are folders on disk; cached per tree item. */
static struct { HTREEITEM it; bool dir; } kindCache[1024];

static int itemKind(HTREEITEM it, const wchar_t *name)
{
    bool open = (TreeView_GetItemState(treeWnd, it, TVIS_EXPANDED) & TVIS_EXPANDED) != 0;
    if (!TreeView_GetParent(treeWnd, it) || TreeView_GetChild(treeWnd, it)) return open ? K_FOLDER_OPEN : K_FOLDER;
    size_t slot = ((uintptr_t)it >> 4) % 1024;
    if (kindCache[slot].it != it) {
        wchar_t path[MAX_PATH];
        kindCache[slot].it = it;
        kindCache[slot].dir = itemPath(it, path) && isDir(path);
    }
    (void)name;
    return kindCache[slot].dir ? K_FOLDER : K_FILE;
}

/* The tree's custom draw, after Notepad++'s own (dark mode) handler produced `result`:
   ask for a call after each item is drawn, then paint our icon over the item's own one. */
LRESULT explorerCustomDraw(NMTVCUSTOMDRAW *cd, LRESULT result)
{
    if (!treeWnd || cd->nmcd.hdr.hwndFrom != treeWnd) return result;
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:     return result | CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: return result | CDRF_NOTIFYPOSTPAINT;
    case CDDS_ITEMPOSTPAINT: {
        HTREEITEM it = (HTREEITEM)cd->nmcd.dwItemSpec;
        RECT text;
        if (!TreeView_GetItemRect(treeWnd, it, &text, TRUE)) return result;
        int cx = 16, cy = 16;
        HIMAGELIST il = TreeView_GetImageList(treeWnd, TVSIL_NORMAL);
        if (il) ImageList_GetIconSize(il, &cx, &cy);
        else return result;                                   /* no icons in this tree */
        wchar_t name[MAX_PATH] = L"";
        TVITEMW tv = { 0 };
        tv.mask = TVIF_TEXT; tv.hItem = it; tv.pszText = name; tv.cchTextMax = MAX_PATH;
        TreeView_GetItem(treeWnd, &tv);
        int mid = (text.top + text.bottom) / 2;
        RECT icon = { text.left - cx - 3, mid - cy / 2, text.left - 3, mid - cy / 2 + cy };
        COLORREF under = GetPixel(cd->nmcd.hdc, text.left - 1, mid);   /* the row's background (selected or not) */
        if (under == CLR_INVALID) under = backColor();
        RECT wipe = { icon.left - 1, text.top, text.left, text.bottom };
        HBRUSH b = CreateSolidBrush(under);
        FillRect(cd->nmcd.hdc, &wipe, b);
        DeleteObject(b);
        drawTypeIcon(cd->nmcd.hdc, icon, name, itemKind(it, name));
        return result;
    }
    }
    return result;
}

/* ---------------- actions ---------------- */

static void collapseBelow(HTREEITEM it)
{
    for (HTREEITEM c = TreeView_GetChild(treeWnd, it); c; c = TreeView_GetNextSibling(treeWnd, c)) {
        collapseBelow(c);
        if (TreeView_GetChild(treeWnd, c)) TreeView_Expand(treeWnd, c, TVE_COLLAPSE);
    }
}

static void collapseFolders(void)
{
    if (!treeWnd) return;
    SendMessageW(treeWnd, WM_SETREDRAW, FALSE, 0);
    for (HTREEITEM r = TreeView_GetRoot(treeWnd); r; r = TreeView_GetNextSibling(treeWnd, r)) {
        collapseBelow(r);
        TreeView_Expand(treeWnd, r, TVE_EXPAND);
        TreeView_EnsureVisible(treeWnd, r);
    }
    SendMessageW(treeWnd, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(treeWnd, NULL, TRUE);
}

static HTREEITEM childNamed(HTREEITEM parent, const wchar_t *name, size_t len)
{
    wchar_t text[MAX_PATH];
    for (HTREEITEM c = TreeView_GetChild(treeWnd, parent); c; c = TreeView_GetNextSibling(treeWnd, c)) {
        TVITEMW tv = { 0 };
        tv.mask = TVIF_TEXT; tv.hItem = c; tv.pszText = text; tv.cchTextMax = MAX_PATH;
        if (TreeView_GetItem(treeWnd, &tv) && wcslen(text) == len && _wcsnicmp(text, name, len) == 0) return c;
    }
    return NULL;
}

/* Select the current file in the tree (expanding its folders). */
static void locateCurrentFile(void)
{
    wchar_t file[MAX_PATH] = L"", root[MAX_PATH];
    SendMessageW(npp._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH, (LPARAM)file);
    for (HTREEITEM r = treeWnd ? TreeView_GetRoot(treeWnd) : NULL; r; r = TreeView_GetNextSibling(treeWnd, r)) {
        if (!itemPath(r, root)) continue;
        size_t n = wcslen(root);
        if (_wcsnicmp(file, root, n) != 0 || file[n] != L'\\') continue;
        HTREEITEM at = r;
        for (const wchar_t *p = file + n + 1; at && *p; ) {
            const wchar_t *slash = wcschr(p, L'\\');
            size_t len = slash ? (size_t)(slash - p) : wcslen(p);
            TreeView_Expand(treeWnd, at, TVE_EXPAND);
            at = childNamed(at, p, len);
            p = slash ? slash + 1 : p + len;
        }
        if (at) { TreeView_SelectItem(treeWnd, at); TreeView_EnsureVisible(treeWnd, at); SetFocus(treeWnd); }
        return;
    }
    MessageBoxW(npp._nppHandle, L"The current file isn't inside the open folder.", L"Locate Current File", MB_ICONINFORMATION);
}

static void revealFolder(void)
{
    wchar_t root[MAX_PATH];
    HTREEITEM r = treeWnd ? TreeView_GetRoot(treeWnd) : NULL;
    if (r && itemPath(r, root)) ShellExecuteW(NULL, L"open", root, NULL, NULL, SW_SHOWNORMAL);
}

static void moreMenu(HWND owner, RECT at)
{
    bool hasFolder = treeWnd && TreeView_GetRoot(treeWnd);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (hasFolder ? 0 : MF_GRAYED), M_LOCATE, L"Locate Current File");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, M_OPEN, L"Open Folder...\tCtrl+Shift+O");
    AppendMenuW(m, MF_STRING, M_RECENT, L"Open Recent Folder...\tCtrl+Shift+R");
    AppendMenuW(m, MF_STRING | (hasFolder ? 0 : MF_GRAYED), M_REVEAL, L"Reveal in File Explorer");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (hasFolder ? 0 : MF_GRAYED), M_CLOSE, L"Close Folder");
    MapWindowPoints(owner, NULL, (POINT *)&at, 2);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, at.right, at.bottom, 0, owner, NULL);
    DestroyMenu(m);
    switch (cmd) {
    case M_LOCATE: locateCurrentFile(); break;
    case M_OPEN:   openFolderCommand(); break;
    case M_RECENT: recentFolderPicker(); break;
    case M_REVEAL: revealFolder(); break;
    case M_CLOSE:  closeFolderCommand(); break;
    }
}

static void runButton(HWND owner, int b)
{
    switch (b) {
    case B_NEWFILE:   explorerNewFile(); break;
    case B_NEWFOLDER: explorerNewFolder(); break;
    case B_COLLAPSE:  collapseFolders(); break;
    case B_MORE:      moreMenu(owner, buttons[B_MORE]); break;
    }
}

/* ---------------- header ---------------- */

static void drawButtonIcon(HDC dc, int b, RECT r, COLORREF ink)
{
    int s = r.bottom - r.top, pad = s / 5;
    RECT in = { r.left + pad, r.top + pad, r.right - pad, r.bottom - pad };
    if (b == B_NEWFILE || b == B_NEWFOLDER) {
        HICON ic = makeNewItemIcon(in.right - in.left, in.bottom - in.top, darkMode(), false, b == B_NEWFILE ? 0 : 1);
        if (ic) { DrawIconEx(dc, in.left, in.top, ic, 0, 0, 0, NULL, DI_NORMAL); DestroyIcon(ic); }
        return;
    }
    HPEN pen = CreatePen(PS_SOLID, s >= 30 ? 2 : 1, ink);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (b == B_COLLAPSE) {                                    /* a box with a minus */
        int w = in.right - in.left;
        Rectangle(dc, in.left + w / 8, in.top + w / 8, in.right - w / 8, in.bottom - w / 8);
        MoveToEx(dc, in.left + w * 5 / 16, (in.top + in.bottom) / 2, NULL);
        LineTo(dc, in.right - w * 5 / 16 + 1, (in.top + in.bottom) / 2);
    } else {                                                  /* three dots */
        HBRUSH dot = CreateSolidBrush(ink);
        int cy = (in.top + in.bottom) / 2, d = (in.right - in.left) / 6 + 1;
        for (int i = 0; i < 3; i++) {
            int cx = in.left + (in.right - in.left) * (2 * i + 1) / 6;
            RECT e = { cx - d / 2, cy - d / 2, cx - d / 2 + d, cy - d / 2 + d };
            FillRect(dc, &e, dot);
        }
        DeleteObject(dot);
    }
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
}

static void layoutButtons(HWND h)
{
    RECT rc; GetClientRect(h, &rc);
    int s = S(22), gap = S(2), y = (rc.bottom - s) / 2, x = rc.right - S(8) - s;
    for (int b = B_COUNT - 1; b >= 0; b--, x -= s + gap) SetRect(&buttons[b], x, y, x + s, y + s);
}

static void paintHeader(HWND h, HDC dc)
{
    RECT rc; GetClientRect(h, &rc);
    COLORREF back = backColor(), ink = textColor();
    HBRUSH bg = CreateSolidBrush(back); FillRect(dc, &rc, bg); DeleteObject(bg);
    layoutButtons(h);
    HFONT f = uiFont(8, FW_SEMIBOLD);
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, mix(back, ink, 70));
    RECT t = { S(12), 0, buttons[0].left - S(4), rc.bottom };
    DrawTextW(dc, L"EXPLORER", -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, of);
    DeleteObject(f);
    for (int b = 0; b < B_COUNT; b++) {
        if (b == hot) {
            HBRUSH hb = CreateSolidBrush(mix(back, ink, 16));
            FillRect(dc, &buttons[b], hb);
            DeleteObject(hb);
        }
        drawButtonIcon(dc, b, buttons[b], mix(back, ink, 85));
    }
    HPEN line = CreatePen(PS_SOLID, 1, mix(back, ink, 12));    /* a hairline under the header */
    HGDIOBJ op = SelectObject(dc, line);
    MoveToEx(dc, 0, rc.bottom - 1, NULL); LineTo(dc, rc.right, rc.bottom - 1);
    SelectObject(dc, op); DeleteObject(line);
}

static void addTips(HWND h)
{
    static const wchar_t *tips[B_COUNT] = { L"New File... (Ctrl+Alt+N)", L"New Folder...", L"Collapse Folders", L"More Actions..." };
    if (!tipWnd) {
        tipWnd = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                 0, 0, 0, 0, h, NULL, hInst, NULL);
    }
    layoutButtons(h);
    for (int b = 0; b < B_COUNT; b++) {
        TOOLINFOW ti = { sizeof ti };
        ti.uFlags = TTF_SUBCLASS; ti.hwnd = h; ti.uId = b; ti.rect = buttons[b]; ti.lpszText = (LPWSTR)tips[b];
        if (!SendMessageW(tipWnd, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti)) SendMessageW(tipWnd, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
}

static int buttonAt(const RECT *rs, int n, LPARAM l)
{
    POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
    for (int i = 0; i < n; i++) if (PtInRect(&rs[i], p)) return i;
    return -1;
}

static LRESULT CALLBACK headerProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_SIZE: addTips(h); InvalidateRect(h, NULL, FALSE); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom), ob = SelectObject(mem, bmp);
        paintHeader(h, mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int b = buttonAt(buttons, B_COUNT, l);
        if (b != hot) { hot = b; InvalidateRect(h, NULL, FALSE); }
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: hot = -1; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_LBUTTONUP: {
        int b = buttonAt(buttons, B_COUNT, l);
        if (b >= 0) runButton(h, b);
        return 0;
    }
    }
    return DefWindowProcW(h, msg, w, l);
}

/* ---------------- empty state ---------------- */

static void paintEmpty(HWND h, HDC dc)
{
    RECT rc; GetClientRect(h, &rc);
    COLORREF back = backColor(), ink = textColor();
    HBRUSH bg = CreateSolidBrush(back); FillRect(dc, &rc, bg); DeleteObject(bg);
    HFONT f = uiFont(9, FW_NORMAL), fb = uiFont(9, FW_SEMIBOLD);
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, mix(back, ink, 80));
    int pad = S(16), y = S(14), w = rc.right - 2 * pad;
    RECT t = { pad, y, pad + w, y + S(60) };
    DrawTextW(dc, L"You have not yet opened a folder.", -1, &t, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    DrawTextW(dc, L"You have not yet opened a folder.", -1, &t, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    y = t.bottom + S(14);
    static const wchar_t *labels[2] = { L"Open Folder", L"Open Recent Folder" };
    SelectObject(dc, fb);
    for (int i = 0; i < 2; i++) {
        SetRect(&emptyButtons[i], pad, y, pad + (w > S(240) ? S(240) : w), y + S(28));
        COLORREF fill = i == 0 ? RGB(0x0e, 0x63, 0x9c) : mix(back, ink, 14);
        if (hot == 100 + i) fill = i == 0 ? RGB(0x11, 0x77, 0xbb) : mix(back, ink, 22);
        HBRUSH bb = CreateSolidBrush(fill);
        FillRect(dc, &emptyButtons[i], bb);
        DeleteObject(bb);
        SetTextColor(dc, i == 0 ? RGB(0xff, 0xff, 0xff) : ink);
        DrawTextW(dc, labels[i], -1, &emptyButtons[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += S(36);
    }
    SelectObject(dc, f);
    SetTextColor(dc, mix(back, ink, 55));
    RECT hint = { pad, y + S(6), pad + w, rc.bottom };
    DrawTextW(dc, L"Ctrl+Shift+O opens a folder, Ctrl+Shift+R a recent one, Ctrl+B shows or hides this panel.",
              -1, &hint, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, of);
    DeleteObject(f);
    DeleteObject(fb);
}

static LRESULT CALLBACK emptyProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom), ob = SelectObject(mem, bmp);
        paintEmpty(h, mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE: InvalidateRect(h, NULL, FALSE); return 0;
    case WM_TIMER:
        if (treeWnd && TreeView_GetRoot(treeWnd)) explorerRefresh();
        return 0;
    case WM_MOUSEMOVE: {
        int b = buttonAt(emptyButtons, 2, l), v = b >= 0 ? 100 + b : -1;
        SetCursor(LoadCursor(NULL, b >= 0 ? IDC_HAND : IDC_ARROW));
        if (v != hot) { hot = v; InvalidateRect(h, NULL, FALSE); }
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE: hot = -1; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_LBUTTONUP: {
        int b = buttonAt(emptyButtons, 2, l);
        if (b == 0) openFolderCommand();
        else if (b == 1) recentFolderPicker();
        return 0;
    }
    }
    return DefWindowProcW(h, msg, w, l);
}

/* ---------------- wiring ---------------- */

/* Place the header over the panel's own toolbar row, the tree below it, and the empty view over
   the tree when no folder is open. Called after the panel lays itself out. */
void explorerLayout(void)
{
    if (!panelWnd || !IsWindow(panelWnd) || !headerWnd) return;
    RECT rc; GetClientRect(panelWnd, &rc);
    int hh = S(30);
    if (toolbarWnd && IsWindowVisible(toolbarWnd)) ShowWindow(toolbarWnd, SW_HIDE);
    SetWindowPos(headerWnd, HWND_TOP, 0, 0, rc.right, hh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    static bool wasEmpty = true;
    HTREEITEM root = treeWnd ? TreeView_GetRoot(treeWnd) : NULL;
    bool empty = !root;
    /* the tree is hidden while empty: it doesn't clip its siblings and would paint over the empty view */
    if (treeWnd) {
        /* Notepad++ has just placed the tree below its (hidden) toolbar; move it under our header
           without copying old pixels and repaint it all, so no rows are left half drawn */
        SetWindowPos(treeWnd, NULL, 0, hh, rc.right, rc.bottom - hh > 0 ? rc.bottom - hh : 0,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS | (empty ? SWP_HIDEWINDOW : SWP_SHOWWINDOW));
        if (!empty) RedrawWindow(treeWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
    }
    if (wasEmpty && !empty && !TreeView_GetNextSibling(treeWnd, root))
        TreeView_Expand(treeWnd, root, TVE_EXPAND);               /* like VS Code: a folder opens expanded */
    wasEmpty = empty;
    if (empty) {
        SetWindowPos(emptyWnd, HWND_TOP, 0, hh, rc.right, rc.bottom - hh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetTimer(emptyWnd, 1, 400, NULL);                         /* notice a folder added some other way */
    } else if (IsWindowVisible(emptyWnd)) {
        KillTimer(emptyWnd, 1);
        ShowWindow(emptyWnd, SW_HIDE);
    }
}

/* Restyle a (newly found) Folder as Workspace panel; harmless to call again. */
void explorerAttach(HWND panel, HWND toolbar, HWND tree)
{
    static bool registered;
    if (!registered) {
        WNDCLASSW wc = { 0 };
        wc.hInstance = hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpfnWndProc = headerProc; wc.lpszClassName = HEADER_CLASS; RegisterClassW(&wc);
        wc.lpfnWndProc = emptyProc; wc.lpszClassName = EMPTY_CLASS; RegisterClassW(&wc);
        registered = true;
    }
    bool fresh = panel != panelWnd || !headerWnd || !IsWindow(headerWnd);
    panelWnd = panel; toolbarWnd = toolbar;
    /* the panel must not paint its background over the header / tree (flicker while resizing) */
    SetWindowLongPtrW(panel, GWL_STYLE, GetWindowLongPtrW(panel, GWL_STYLE) | WS_CLIPCHILDREN);
    if (tree != treeWnd) {
        treeWnd = tree;
        memset(kindCache, 0, sizeof kindCache);
        if (tree) {
            LONG_PTR st = GetWindowLongPtrW(tree, GWL_STYLE);
            SetWindowLongPtrW(tree, GWL_STYLE, (st & ~(TVS_HASLINES | WS_BORDER)) | TVS_FULLROWSELECT);
            SetWindowLongPtrW(tree, GWL_EXSTYLE, GetWindowLongPtrW(tree, GWL_EXSTYLE) & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
            SetWindowPos(tree, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            TreeView_SetItemHeight(tree, S(22));
            TreeView_SetExtendedStyle(tree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
        }
    }
    if (fresh) {
        headerWnd = CreateWindowExW(0, HEADER_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, panel, NULL, hInst, NULL);
        emptyWnd = CreateWindowExW(0, EMPTY_CLASS, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, panel, NULL, hInst, NULL);
        tipWnd = NULL;
        addTips(headerWnd);
    }
    explorerLayout();
}

/* The folder in the panel changed (opened, switched or closed). */
void explorerRefresh(void)
{
    memset(kindCache, 0, sizeof kindCache);
    explorerLayout();
    if (treeWnd) InvalidateRect(treeWnd, NULL, TRUE);
    if (headerWnd) InvalidateRect(headerWnd, NULL, FALSE);
    if (emptyWnd) InvalidateRect(emptyWnd, NULL, FALSE);
}
