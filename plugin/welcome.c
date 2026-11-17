/*
 * VS Code style Welcome page: when the only thing open is an empty, unchanged "new 1" tab, the
 * editor (and its tab bar) shows Start (New File, Open File, Open Folder, New Folder), Recent
 * folders and the main shortcuts instead. It goes away when a file is opened, when you type,
 * or with "New File" (which gives you that empty tab to write in).
 */
#include "devtools.h"

#define NPPM_GETNBOPENFILES  (NPPMSG + 7)
#define PRIMARY_VIEW         1
#define SECOND_VIEW          2
#define NPPN_FILEOPENED      1004
#define NPPN_FILECLOSED      1005
#define IDM_FILE_OPEN        41002
#define WM_WELCOME_UPDATE    (WM_APP + 0x57)
#define WM_WELCOME_GAPS      (WM_APP + 0x58)   /* repaint the main window's uncovered strips (see overlayProc) */
static bool gapsPosted;
#define MAX_RECENT           6

enum { A_NEWFILE, A_OPENFILE, A_OPENFOLDER, A_NEWFOLDER, A_RECENT, A_MORE, A_TOOLS };
typedef struct { RECT r; int action, index; } Link;

static HWND overlay, coverTab;

/* Untitled tabs you made yourself (Ctrl+N, File > New, the toolbar, Welcome's "New File") stay
   tabs even when empty; only the placeholder "new 1" Notepad++ makes when nothing is open gets
   the Welcome page. Buffer ids, forgotten when the tab closes. */
#define NPPM_GETCURRENTBUFFERID (NPPMSG + 60)
static LRESULT userBufs[128];
static int userCount;
static bool newRequested;            /* a New File command is under way: the next untitled tab is yours */

static bool isUserBuf(LRESULT id)
{
    for (int i = 0; i < userCount; i++) if (userBufs[i] == id) return true;
    return false;
}

static void markUserBuf(LRESULT id)
{
    if (!id || isUserBuf(id)) return;
    if (userCount == (int)(sizeof userBufs / sizeof userBufs[0])) { memmove(userBufs, userBufs + 1, (userCount - 1) * sizeof userBufs[0]); userCount--; }
    userBufs[userCount++] = id;
}

static void forgetBuf(LRESULT id)
{
    for (int i = 0; i < userCount; i++)
        if (userBufs[i] == id) { userBufs[i] = userBufs[--userCount]; return; }
}

/* File > New / Ctrl+N. true = handled: the Welcome page was showing, so its empty "new 1" becomes
   your new file (no second tab); otherwise the next untitled tab is noted as yours. */
/* Closing a tab (Ctrl+W, File > Close, the tab's x). Notepad++ keeps a lone empty "new 1" open
   instead of closing it: then it isn't yours any more, and the Welcome page comes back. */
void welcomeCloseRequested(void)
{
    if (!overlay) return;
    forgetBuf(SendMessageW(npp._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
    PostMessageW(overlay, WM_WELCOME_UPDATE, 0, 0);
}

bool welcomeNewFileRequested(void)
{
    if (overlay && IsWindowVisible(overlay)) {
        markUserBuf(SendMessageW(npp._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
        ShowWindow(overlay, SW_HIDE);
        SetFocus(npp._scintillaMainHandle);
        return true;
    }
    newRequested = true;
    return false;
}
static Link links[24];
static int linkCount, hotLink = -1;
static wchar_t recent[MAX_RECENT][MAX_PATH];
static int recentCount;

/* ---------------- when to show ---------------- */

static bool editorIsEmpty(void)
{
    if (SendMessageW(npp._nppHandle, NPPM_GETNBOPENFILES, 0, PRIMARY_VIEW) != 1) return false;
    if (IsWindowVisible(npp._scintillaSecondHandle) && SendMessageW(npp._nppHandle, NPPM_GETNBOPENFILES, 0, SECOND_VIEW) > 0) return false;
    wchar_t path[MAX_PATH] = L"";
    SendMessageW(npp._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH, (LPARAM)path);
    if (wcschr(path, L'\\')) return false;
    if (isUserBuf(SendMessageW(npp._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0))) return false;   /* your own empty tab */
    return SendMessageW(npp._scintillaMainHandle, SCI_GETLENGTH, 0, 0) == 0
        && !SendMessageW(npp._scintillaMainHandle, SCI_GETMODIFY, 0, 0);
}

/* The main view's tab bar: a visible tab control next to (or around) the main editor. */
static BOOL CALLBACK findTabBar(HWND h, LPARAM l)
{
    RECT around = *(RECT *)l, r, x;
    wchar_t cls[32];
    if (GetParent(h) != GetParent(npp._scintillaMainHandle) || !IsWindowVisible(h)) return TRUE;
    if (!GetClassNameW(h, cls, 32) || wcscmp(cls, L"SysTabControl32")) return TRUE;
    GetWindowRect(h, &r);
    InflateRect(&around, 6, 6);
    if (IntersectRect(&x, &r, &around)) { coverTab = h; return FALSE; }
    return TRUE;
}

static void placeOverlay(void)
{
    HWND parent = GetParent(npp._scintillaMainHandle);
    RECT sci, r;
    GetWindowRect(npp._scintillaMainHandle, &sci);
    coverTab = NULL;
    EnumChildWindows(parent, findTabBar, (LPARAM)&sci);
    r = sci;
    if (coverTab) {
        /* Notepad++'s tab control spans its tab row AND the editor below it: cover from its top,
           as wide as the editor (never above the tab row: that is the toolbar) */
        RECT t; GetWindowRect(coverTab, &t);
        if (t.top < sci.top && sci.top - t.top < 120) r.top = t.top;
        SetWindowLongPtrW(coverTab, GWL_STYLE, GetWindowLongPtrW(coverTab, GWL_STYLE) | WS_CLIPSIBLINGS);
    }
    SetWindowLongPtrW(npp._scintillaMainHandle, GWL_STYLE, GetWindowLongPtrW(npp._scintillaMainHandle, GWL_STYLE) | WS_CLIPSIBLINGS);
    MapWindowPoints(NULL, parent, (POINT *)&r, 2);
    RECT now; GetWindowRect(overlay, &now); MapWindowPoints(NULL, parent, (POINT *)&now, 2);
    if (!EqualRect(&now, &r) || !IsWindowVisible(overlay)) {
        SetWindowPos(overlay, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE | SWP_NOCOPYBITS);
        InvalidateRect(overlay, NULL, FALSE);                     /* the layout is centred: repaint all of it */
    }
}

static void welcomeUpdate(void)
{
    if (!overlay) return;
    bool empty = editorIsEmpty();
    if (empty) {
        if (!IsWindowVisible(overlay)) recentCount = recentFolderList(recent, MAX_RECENT);
        placeOverlay();
        if (!IsWindowVisible(overlay)) ShowWindow(overlay, SW_SHOWNA);
        InvalidateRect(overlay, NULL, FALSE);
    } else if (IsWindowVisible(overlay)) ShowWindow(overlay, SW_HIDE);
}

/* ---------------- drawing ---------------- */

static COLORREF mix(COLORREF a, COLORREF b, int pctB)
{
    return RGB((GetRValue(a) * (100 - pctB) + GetRValue(b) * pctB) / 100,
               (GetGValue(a) * (100 - pctB) + GetGValue(b) * pctB) / 100,
               (GetBValue(a) * (100 - pctB) + GetBValue(b) * pctB) / 100);
}

static HFONT font(HDC dc, int pt, int weight, bool underline)
{
    return CreateFontW(-MulDiv(pt, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0, weight, 0, underline, 0,
                       DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

/* A clickable line: text in link colour (underlined when hovered), optional dim text after it. */
static int drawLink(HDC dc, int x, int y, int maxRight, const wchar_t *text, const wchar_t *after,
                    int action, int index, COLORREF linkCol, COLORREF dim, int pt)
{
    int i = linkCount < (int)(sizeof links / sizeof links[0]) ? linkCount++ : linkCount - 1;
    HFONT f = font(dc, pt, FW_NORMAL, i == hotLink), fd = font(dc, pt, FW_NORMAL, false);
    HGDIOBJ of = SelectObject(dc, f);
    SIZE sz; GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
    RECT r = { x, y, x + sz.cx, y + sz.cy };
    if (r.right > maxRight) r.right = maxRight;
    SetTextColor(dc, linkCol);
    DrawTextW(dc, text, -1, &r, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    links[i] = (Link){ r, action, index };
    if (after && r.right + 12 < maxRight) {
        SelectObject(dc, fd);
        RECT a = { r.right + MulDiv(10, GetDeviceCaps(dc, LOGPIXELSX), 96), y, maxRight, y + sz.cy };
        SetTextColor(dc, dim);
        DrawTextW(dc, after, -1, &a, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS);
    }
    SelectObject(dc, of);
    DeleteObject(f);
    DeleteObject(fd);
    return sz.cy;
}

static int heading(HDC dc, int x, int y, const wchar_t *text, COLORREF col)
{
    HFONT f = font(dc, 13, FW_NORMAL, false);
    HGDIOBJ of = SelectObject(dc, f);
    SIZE sz; GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
    SetTextColor(dc, col);
    TextOutW(dc, x, y, text, (int)wcslen(text));
    SelectObject(dc, of);
    DeleteObject(f);
    return sz.cy;
}

static void paint(HWND h, HDC dc)
{
    RECT rc; GetClientRect(h, &rc);
    COLORREF back = editorBack(), fore = editorFore();
    bool dark = GetRValue(back) + GetGValue(back) + GetBValue(back) < 384;
    COLORREF linkCol = dark ? RGB(0x37, 0x94, 0xff) : RGB(0x00, 0x6a, 0xb1), dim = mix(back, fore, 55);
    HBRUSH bg = CreateSolidBrush(back); FillRect(dc, &rc, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    linkCount = 0;
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
#define D(v) MulDiv((v), dpi, 96)
    int w = rc.right, x = w / 10 > D(32) ? w / 10 : D(32), y = rc.bottom / 9 > D(24) ? rc.bottom / 9 : D(24);
    int colW = (w - 2 * x) / 2, x2 = x + colW + D(24);
    bool twoCols = colW > D(260);

    HFONT big = font(dc, 26, FW_LIGHT, false), sub = font(dc, 13, FW_NORMAL, false);
    HGDIOBJ of = SelectObject(dc, big);
    SetTextColor(dc, fore);
    TextOutW(dc, x, y, L"NppDev", 6);
    SIZE sz; GetTextExtentPoint32W(dc, L"Ag", 2, &sz);
    y += sz.cy;
    SelectObject(dc, sub);
    SetTextColor(dc, dim);
    TextOutW(dc, x, y, L"Notepad++ Dev Edition", 21);
    GetTextExtentPoint32W(dc, L"Ag", 2, &sz);
    y += sz.cy + D(36);
    SelectObject(dc, of);
    DeleteObject(big);
    DeleteObject(sub);

    int left = x, top = y, right = twoCols ? x + colW : w - x;
    int gap = D(8);
    y += heading(dc, left, y, L"Start", fore) + gap;
    static const struct { const wchar_t *text, *keys; int action; } start[] = {
        { L"➕  New File...", L"Ctrl+N", A_NEWFILE },
        { L"\U0001F4C4  Open File...", L"Ctrl+O", A_OPENFILE },
        { L"\U0001F4C2  Open Folder...", L"Ctrl+Shift+O", A_OPENFOLDER },
        { L"\U0001F4C1  New Folder...", L"", A_NEWFOLDER } };
    for (int i = 0; i < 4; i++) y += drawLink(dc, left, y, right, start[i].text, start[i].keys, start[i].action, i, linkCol, dim, 10) + gap;

    y += D(24);
    y += heading(dc, left, y, L"Recent", fore) + gap;
    if (!recentCount) {
        HFONT f = font(dc, 10, FW_NORMAL, false); HGDIOBJ o = SelectObject(dc, f);
        SetTextColor(dc, dim);
        TextOutW(dc, left, y, L"No recent folders yet", 21);
        SelectObject(dc, o); DeleteObject(f);
        y += D(22);
    }
    for (int i = 0; i < recentCount; i++) {
        wchar_t shown[MAX_PATH + 2];
        size_t home = wcslen(homeDir);
        if (home && _wcsnicmp(recent[i], homeDir, home) == 0 && recent[i][home] == L'\\') swprintf(shown, MAX_PATH + 2, L"~%ls", recent[i] + home);
        else wcscpy(shown, recent[i]);
        y += drawLink(dc, left, y, right, leafOf(recent[i]), shown, A_RECENT, i, linkCol, dim, 10) + gap;
    }
    if (recentCount) y += drawLink(dc, left, y, right, L"More...", L"Ctrl+Shift+R", A_MORE, 0, linkCol, dim, 10) + gap;

    /* shortcuts: a second column when there is room, else below */
    int sx = twoCols ? x2 : left, sy = twoCols ? top : y + D(24), sr = w - x;
    sy += heading(dc, sx, sy, L"Shortcuts", fore) + gap;
    static const struct { const wchar_t *what, *keys; } keys[] = {
        { L"Run the current file", L"Ctrl+Enter" }, { L"Quick Open a file", L"Ctrl+P" },
        { L"Command Palette", L"Ctrl+Shift+P" }, { L"Show / hide the Explorer", L"Ctrl+B" },
        { L"Show / hide the Run panel", L"Ctrl+`" }, { L"Suggestions (IntelliSense)", L"Ctrl+Space" },
        { L"Go to definition", L"F12" }, { L"Rename symbol", L"F2" }, { L"Problems", L"Ctrl+Shift+M" },
        { L"Toggle comment", L"Ctrl+/" }, { L"Dark / light theme", L"Ctrl+Shift+P → theme" } };
    HFONT f = font(dc, 10, FW_NORMAL, false), fk = font(dc, 10, FW_SEMIBOLD, false);
    HGDIOBJ o = SelectObject(dc, f);
    GetTextExtentPoint32W(dc, L"Ag", 2, &sz);
    int keyCol = sx + (sr - sx) * 62 / 100;
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        RECT a = { sx, sy, keyCol - D(8), sy + sz.cy }, k = { keyCol, sy, sr, sy + sz.cy };
        SelectObject(dc, f); SetTextColor(dc, dim);
        DrawTextW(dc, keys[i].what, -1, &a, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, fk); SetTextColor(dc, fore);
        DrawTextW(dc, keys[i].keys, -1, &k, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        sy += sz.cy + gap;
    }
    SelectObject(dc, o);
    DeleteObject(f);
    DeleteObject(fk);
    sy += D(8);
    drawLink(dc, sx, sy, sr, L"Check / Install Tools...", L"Python, GCC / G++, Java", A_TOOLS, 0, linkCol, dim, 10);
#undef D
}

/* ---------------- window ---------------- */

static void run(int i)
{
    if (i < 0 || i >= linkCount) return;
    switch (links[i].action) {
    case A_NEWFILE:                                       /* this empty tab is now yours to write in */
        markUserBuf(SendMessageW(npp._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0));
        ShowWindow(overlay, SW_HIDE); SetFocus(npp._scintillaMainHandle); break;
    case A_OPENFILE:   PostMessageW(npp._nppHandle, WM_COMMAND, IDM_FILE_OPEN, 0); break;
    case A_OPENFOLDER: openFolderCommand(); break;
    case A_NEWFOLDER:  createFolderCommand(); break;
    case A_RECENT:     openRecentFolder(recent[links[i].index]); break;
    case A_MORE:       recentFolderPicker(); break;
    case A_TOOLS:      runToolsWindow(L"", NULL, 0); break;
    }
}

static int linkAt(LPARAM l)
{
    POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
    for (int i = 0; i < linkCount; i++) if (PtInRect(&links[i].r, p)) return i;
    return -1;
}

static LRESULT CALLBACK overlayProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_WELCOME_UPDATE: welcomeUpdate(); return 0;
    case WM_WELCOME_GAPS:
        /* While a panel's edge is dragged quickly, the strips no panel covers yet are repainted only
           when the mouse rests (WM_PAINT waits for input), leaving stripes of old pixels. Paint the
           main window's uncovered parts now; its children are clipped, so this touches only the gaps. */
        gapsPosted = false;
        RedrawWindow(npp._nppHandle, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);                         /* off-screen: no flicker */
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom), ob = SelectObject(mem, bmp);
        paint(h, mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int i = linkAt(l);
        SetCursor(LoadCursor(NULL, i >= 0 ? IDC_HAND : IDC_ARROW));
        if (i != hotLink) { hotLink = i; InvalidateRect(h, NULL, FALSE); }
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_SETCURSOR: return TRUE;                               /* set in WM_MOUSEMOVE */
    case WM_MOUSELEAVE: hotLink = -1; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: SetFocus(npp._scintillaMainHandle); return 0;   /* keyboard shortcuts keep working */
    case WM_LBUTTONUP: run(linkAt(l)); return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

/* Follow the editor when Notepad++ lays out its windows (resizing, panels docked or undocked). */
static LRESULT CALLBACK followProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data)
{
    (void)id; (void)data;
    LRESULT r = DefSubclassProc(h, msg, w, l);
    if (msg == WM_WINDOWPOSCHANGED && overlay && !gapsPosted) {
        gapsPosted = true;                                        /* once Notepad++ has finished this layout step */
        PostMessageW(overlay, WM_WELCOME_GAPS, 0, 0);
    }
    if (msg == WM_WINDOWPOSCHANGED && overlay && IsWindowVisible(overlay))
        placeOverlay();                                           /* now, before anything repaints: no stripes while resizing */
    else if (msg == WM_SHOWWINDOW && overlay && IsWindowVisible(overlay))
        PostMessageW(overlay, WM_WELCOME_UPDATE, 0, 0);
    return r;
}

void welcomeOnNotify(SciNotifyHeader *n)
{
    if (n->code == NPPN_READY && n->hwndFrom == npp._nppHandle) {
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = overlayProc; wc.hInstance = hInst; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"NppDevWelcome";
        RegisterClassW(&wc);
        overlay = CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                                  GetParent(npp._scintillaMainHandle), NULL, hInst, NULL);
        if (overlay) SetWindowSubclass(npp._scintillaMainHandle, followProc, 0x57E1, 0);
        welcomeUpdate();
        return;
    }
    if (!overlay) return;
    if (n->hwndFrom == npp._nppHandle && n->code == 1003 /* NPPN_FILEBEFORECLOSE */
            && SendMessageW(npp._nppHandle, NPPM_GETNBOPENFILES, 0, PRIMARY_VIEW) == 1) {
        forgetBuf((LRESULT)n->idFrom);                              /* the last tab: see welcomeCloseRequested */
        PostMessageW(overlay, WM_WELCOME_UPDATE, 0, 0);
        return;
    }
    if (n->hwndFrom == npp._nppHandle && (n->code == NPPN_DARKMODECHANGED || n->code == NPPN_WORDSTYLESUPDATED))
        InvalidateRect(overlay, NULL, FALSE);                         /* new theme colours */
    else if (n->hwndFrom == npp._nppHandle
            && (n->code == NPPN_BUFFERACTIVATED || n->code == NPPN_FILEOPENED || n->code == NPPN_FILECLOSED)) {
        if (n->code == NPPN_FILECLOSED) forgetBuf((LRESULT)n->idFrom);
        if (n->code == NPPN_BUFFERACTIVATED && newRequested) {    /* the tab Ctrl+N / File > New just made */
            wchar_t path[MAX_PATH] = L"";
            SendMessageW(npp._nppHandle, NPPM_GETFULLCURRENTPATH, MAX_PATH, (LPARAM)path);
            if (!wcschr(path, L'\\')) { markUserBuf((LRESULT)n->idFrom); newRequested = false; }
        }
        PostMessageW(overlay, WM_WELCOME_UPDATE, 0, 0);              /* after Notepad++ finishes opening/closing */
    }
    else if (n->code == SCN_MODIFIED && n->hwndFrom == npp._scintillaMainHandle && IsWindowVisible(overlay))
        PostMessageW(overlay, WM_WELCOME_UPDATE, 0, 0);              /* typed into "new 1" */
}
