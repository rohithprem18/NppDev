/* Shared declarations for the Dev Tools plugin (RecentFolders.dll). */
#pragma once
#define COBJMACROS                     /* C helpers for COM calls (IFileOpenDialog_Show, ...) */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

/* ---- Notepad++ plugin interface (from PluginInterface.h / Notepad_plus_msgs.h / Docking.h) ---- */
typedef struct { HWND _nppHandle; HWND _scintillaMainHandle; HWND _scintillaSecondHandle; } NppData;
typedef struct { bool _isCtrl; bool _isAlt; bool _isShift; UCHAR _key; } ShortcutKey;
typedef void (__cdecl *PFUNCPLUGINCMD)(void);
typedef struct { wchar_t _itemName[64]; PFUNCPLUGINCMD _pFunc; int _cmdID; bool _init2Check; ShortcutKey *_pShKey; } FuncItem;
typedef struct { void *hwndFrom; UINT_PTR idFrom; unsigned int code; } SciNotifyHeader;
typedef struct { SciNotifyHeader nmhdr; intptr_t position; } SciNotification;   /* only the fields we read */
typedef struct {
    HWND hClient; const wchar_t *pszName; int dlgID; UINT uMask; HICON hIconTab;
    const wchar_t *pszAddInfo; RECT rcFloat; int iPrevCont; const wchar_t *pszModuleName;
} tTbData;

#define NPPMSG                          (WM_USER + 1000)
#define NPPM_GETCURRENTSCINTILLA        (NPPMSG + 4)
#define NPPM_MODELESSDIALOG             (NPPMSG + 12)
#define NPPM_CREATESCINTILLAHANDLE      (NPPMSG + 20)
#define NPPM_GETMENUHANDLE              (NPPMSG + 25)
#define NPPM_DMMSHOW                    (NPPMSG + 30)
#define NPPM_DMMHIDE                    (NPPMSG + 31)
#define NPPM_DMMREGASDCKDLG             (NPPMSG + 33)
#define NPPM_SAVECURRENTFILE            (NPPMSG + 38)
#define NPPM_SETMENUITEMCHECK           (NPPMSG + 40)
#define NPPM_GETPLUGINSCONFIGDIR        (NPPMSG + 46)
#define NPPM_DOOPEN                     (NPPMSG + 77)
#define NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR (NPPMSG + 90)
#define NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR (NPPMSG + 91)
#define NPPM_ISDARKMODEENABLED          (NPPMSG + 107)
#define NPPM_DARKMODESUBCLASSANDTHEME   (NPPMSG + 112)
#define NPPM_GETFULLCURRENTPATH         (WM_USER + 3000 + 1)
#define NPPM_GETCURRENTDIRECTORY        (WM_USER + 3000 + 2)
#define NPPPLUGINMENU       0
#define NPPMAINMENU         1
#define NPPN_READY          1001
#define NPPN_FILESAVED      1008
#define NPPN_SHUTDOWN       1009
#define NPPN_BUFFERACTIVATED 1010
#define NPPN_WORDSTYLESUPDATED 1012     /* the editor theme changed */
#define NPPN_DARKMODECHANGED 1027       /* dark mode switched on / off */
#define DMF_INIT            0x0000000B
#define DWS_DF_CONT_RIGHT   0x10000000
#define DMN_CLOSE           (1050 + 1)

/* ---- Scintilla messages used by the panels ---- */
#define SCI_ADDTEXT 2001
#define SCI_APPENDTEXT 2282
#define SCI_CLEARALL 2004
#define SCI_GETLENGTH 2006
#define SCI_GOTOPOS 2025
#define SCI_GOTOLINE 2024
#define SCI_SETSAVEPOINT 2014
#define SCI_GETLINE 2153
#define SCI_LINELENGTH 2350
#define SCI_LINEFROMPOSITION 2166
#define SCI_POSITIONFROMLINE 2167
#define SCI_GETLINECOUNT 2154
#define SCI_SETREADONLY 2171
#define SCI_SETTEXT 2181
#define SCI_GETTEXT 2182
#define SCI_GETTEXTLENGTH 2183
#define SCI_SETCODEPAGE 2037
#define SCI_STYLESETFORE 2051
#define SCI_STYLESETBACK 2052
#define SCI_STYLESETBOLD 2053
#define SCI_STYLESETSIZE 2055
#define SCI_STYLESETFONT 2056
#define SCI_STYLESETUNDERLINE 2059
#define SCI_STYLESETHOTSPOT 2409
#define SCI_STYLECLEARALL 2050
#define SCI_STYLEGETFONT 2486
#define SCI_STYLEGETSIZE 2485
#define SCI_STARTSTYLING 2032
#define SCI_SETSTYLING 2033
#define SCI_SETMARGINWIDTHN 2242
#define SCI_SETMARGINTYPEN 2240
#define SCI_SETSELBACK 2068
#define SCI_SETCARETFORE 2069
#define SCI_SETCARETLINEVISIBLE 2096
#define SCI_SETWRAPMODE 2268
#define SCI_SCROLLCARET 2169
#define SCI_ENSUREVISIBLE 2232
#define SCI_FINDCOLUMN 2456
#define SCI_DOCUMENTEND 2318
#define SCI_SETHOTSPOTACTIVEUNDERLINE 2412
#define SCI_SETHSCROLLBAR 2130
#define SCI_SETUSETABS 2124
#define SCI_SETTABWIDTH 2036
#define SCI_EMPTYUNDOBUFFER 2175
#define SCI_GETMODIFY 2159
#define SCI_SETMODEVENTMASK 2359
#define SCI_SETILEXER 4033
#define SCI_SETELEMENTCOLOUR 2753
#define SC_ELEMENT_SELECTION_BACK 11
#define SC_ELEMENT_SELECTION_INACTIVE_BACK 17
#define STYLE_DEFAULT 32
#define STYLE_LINENUMBER 33
#define SC_MARGIN_NUMBER 1
#define SCN_HOTSPOTCLICK 2019
#define SCN_MODIFIED 2008

/* ---- shared state and helpers (RecentFolders.c) ---- */
extern HINSTANCE hInst;
extern NppData npp;
extern HWND fawTree;
extern wchar_t homeDir[MAX_PATH];
extern wchar_t devtoolsDir[MAX_PATH];   /* runner.py, pyrun.exe, tools: <NppDev>\devtools or %USERPROFILE%\NppRunner */
extern bool bundleMode;                 /* running as the NppDev bundle */

bool isDir(const wchar_t *p);
const wchar_t *leafOf(const wchar_t *p);
bool findRootPath(const wchar_t *rootName, wchar_t *out);
bool itemPath(HTREEITEM it, wchar_t *out);
int workspaceRoots(wchar_t roots[][MAX_PATH], int max);          /* root folders shown in the panel */
WORD *dlgPutStr(WORD *p, const wchar_t *s);
WORD *dlgAlign(WORD *p);
WORD *dlgPutItem(WORD *p, DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const wchar_t *text);
WORD *dlgBegin(WORD *tmpl, DWORD style, short cx, short cy, WORD count, const wchar_t *title);
int funcCmdId(int index);                                         /* Notepad++ command id of one of our funcItems */
COLORREF editorFore(void);
COLORREF editorBack(void);
void editorFont(char *name, int cb, int *size);

/* ---- run panel (runpanel.c) ---- */
void runPanelToggle(void);
void runPanelRunCurrent(void);
void runPanelStop(void);
void runPanelOnNotify(SciNotifyHeader *n);
void runPanelShowOutput(void);
/* Run a command line in cwd, streaming its output into the Output box. */
bool runPanelExec(const wchar_t *title, const wchar_t *cmdline, const wchar_t *cwd, void (*onDone)(DWORD exitCode));
extern int runPanelFuncIndex;                                    /* funcItem index of "Toggle Run Panel" */
void runPanelRenamed(const wchar_t *oldPath, const wchar_t *newPath);   /* keep a file's saved Input / Expected */


int recentFolderList(wchar_t out[][MAX_PATH], int max);         /* most recent first */
void forgetRecentFolder(const wchar_t *folder);                   /* take it off the recent list */
void openRecentFolder(const wchar_t *folder);                     /* switch this window to it */
void openFolderCommand(void);                                     /* File > Open Folder dialog */
/* Check / Install Tools window (tools-setup.ps1 args); notify gets msg with its exit code when it closes */
bool runToolsWindow(const wchar_t *args, HWND notify, UINT msg);
void createFolderCommand(void);                                   /* pick a place, name, create, open it */
void closeFolderCommand(void);                                    /* File > Close Folder */
void explorerNewFile(void);                                       /* New File / New Folder in the open folder */
void explorerNewFolder(void);
HICON makeNewItemIcon(int cx, int cy, bool dark, bool mirror, int kind);   /* kind: 0 file, 1 folder */

/* ---- Explorer look of the folder panel (explorer.c) ---- */
void explorerAttach(HWND panel, HWND toolbar, HWND tree);
void explorerLayout(void);
void explorerRefresh(void);                                       /* the open folder changed */
LRESULT explorerCustomDraw(NMTVCUSTOMDRAW *cd, LRESULT result);

/* ---- Welcome page in the empty editor (welcome.c) ---- */
void welcomeOnNotify(SciNotifyHeader *n);
bool welcomeNewFileRequested(void);                               /* File > New / Ctrl+N; true = handled (no new tab) */
void welcomeCloseRequested(void);                                 /* File > Close / Ctrl+W */

/* ---- pickers (picker.c) ---- */
void quickOpen(void);
void commandPalette(void);
void recentFolderPicker(void);
