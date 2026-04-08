/*
 * MDView v3.0 - Total Commander Lister Plugin for Markdown
 * =========================================================
 * Lightweight WLX plugin: built-in Markdown->HTML, embedded MSHTML, zero deps.
 *
 * Hotkeys:
 *   Ctrl+Plus/Minus/0  Zoom in / out / reset
 *   Ctrl+D             Toggle dark/light mode
 *   Ctrl+T             Toggle Table of Contents sidebar
 *   Ctrl+F             Find in page with highlighting
 *   Ctrl+P             Print document
 *   Ctrl+G             Go to top
 *   Ctrl+A             Select all text in the active view
 *   Ctrl+C             Copy text to clipboard (HTML if focus is in rendered view, raw markdown if focus is in text view)
 *   Escape             Close find bar / TOC / help
 *   Ctrl+M             Toggle split view (Markdown render + raw text side by side)
 *   F1                 Show keyboard shortcuts help
 *   CTRL-F             Find in page (with highlighting)
 *   Shift+F3           Find in page (reverse)
 *   F3				    Find next
 *
 * (c) 2026 - MIT License
 */

#define _CRT_SECURE_NO_WARNINGS //avoid warnings for fopen, sprintf, etc.
#define COBJMACROS
#ifndef UNICODE
    #define UNICODE
#endif
#ifndef _UNICODE
    #define _UNICODE
#endif _UNICODE

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
    #define WINVER _WIN32_WINNT
#endif

#if _WIN32_WINNT < 0x0600
    #define MDVIEW_RAW_FONT_NAME_A "Courier New"
    #define MDVIEW_RAW_FONT_NAME L"Courier New"
#else
    #define MDVIEW_RAW_FONT_NAME_A "Cascadia Mono"
    #define MDVIEW_RAW_FONT_NAME L"Cascadia Mono"
#endif
#define MDVIEW_RAW_FONT_PT   11

#include <windows.h>
#include <windowsx.h>
#include <richedit.h>
#include <ole2.h>
#include <exdisp.h>
#include <mshtml.h>
#include <mshtmhst.h>
#include <docobj.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── TC Lister Plugin Interface ──────────────────────────────────────── */

#define LISTPLUGIN_OK    0
#define LISTPLUGIN_ERROR 1

typedef struct {
    int   size;
    DWORD PluginInterfaceVersionLow;
    DWORD PluginInterfaceVersionHi;
    char  DefaultIniName[MAX_PATH];
} ListDefaultParamStruct;

static LRESULT CALLBACK ContainerWndProc(HWND, UINT, WPARAM, LPARAM);
static char* read_file_utf8(const char*);
static char* md_to_html(const char*);
static int   is_dark_theme(void);
static void  navigate_to_html(IWebBrowser2*, const char*);
static LONG_PTR mdview_set_window_ptr(HWND, int, LONG_PTR);
static LONG_PTR mdview_get_window_ptr(HWND, int);

static const wchar_t CLASS_NAME[] = L"MDViewWLXContainer";
static HINSTANCE g_hInstance = NULL;
static int g_classRegistered = 0;

typedef struct {
    IWebBrowser2* pBrowser;
    IOleObject*   pOleObj;
    HWND          hwndIEServer;  /* The actual IE rendering window */
    WNDPROC       origIEProc;    /* Original wndproc of IE Server */
    HWND          hwndContainer; /* Our container window */
    HWND          hwndText;      /* Raw text view (RichEdit), optional */
    HWND          hwndTextCount; /* Raw char count label */
    WNDPROC       origTextProc;  /* Subclass of raw text control */
    HFONT         hTextFont;     /* Font for raw text view */
    int           splitView;     /* 0/1 */
    int           syncGuard;     /* recursion guard for scroll sync */
    size_t        rawCharCount;  /* UTF-8 codepoint count for raw markdown */
    char*         mdUtf8;        /* raw markdown (UTF-8), owned */
} MDViewData;

/* ── INI Settings Persistence ────────────────────────────────────────── */

static char g_iniPath[MAX_PATH] = { 0 };

typedef struct {
    int fontSize;    /* 9-30, default 19 */
    int isDark;      /* 0 or 1, -1 = auto */
    int maxWidth;    /* column width in px, 0 = no limit, default 0 */
    int lineNums;    /* 0 or 1 */
    int rawFontSize; /* raw text font size */
    wchar_t rawFontName[64]; /* raw text font name */
} MDVSettings;

static MDVSettings g_settings = { 19, -1, 960, 0, MDVIEW_RAW_FONT_PT, MDVIEW_RAW_FONT_NAME };

static void load_settings(void) {
    if (!g_iniPath[0]) return;
    g_settings.fontSize = GetPrivateProfileIntA("MDView", "FontSize", 19, g_iniPath);
    g_settings.isDark = GetPrivateProfileIntA("MDView", "DarkMode", -1, g_iniPath);
    g_settings.maxWidth = GetPrivateProfileIntA("MDView", "MaxWidth", 0, g_iniPath);
    g_settings.lineNums = GetPrivateProfileIntA("MDView", "LineNumbers", 0, g_iniPath);
    g_settings.rawFontSize = GetPrivateProfileIntA("MDView", "RawFontSize", MDVIEW_RAW_FONT_PT, g_iniPath);
    char fontNameA[64];
    GetPrivateProfileStringA("MDView", "RawFontName", MDVIEW_RAW_FONT_NAME_A, fontNameA, sizeof(fontNameA), g_iniPath);
    MultiByteToWideChar(CP_ACP, 0, fontNameA, -1, g_settings.rawFontName, 64);

    /* Clamp */
    if (g_settings.fontSize < 9) g_settings.fontSize = 9;
    if (g_settings.fontSize > 30) g_settings.fontSize = 30;
    if (g_settings.maxWidth != 0 && g_settings.maxWidth < 400) g_settings.maxWidth = 400;
    if (g_settings.maxWidth > 9999) g_settings.maxWidth = 9999;
    if (g_settings.rawFontSize < 6 || g_settings.rawFontSize > 72) g_settings.rawFontSize = MDVIEW_RAW_FONT_PT;
}

static void save_setting_int(const char* key, int val) {
    if (!g_iniPath[0]) return;
    char buf[16]; _snprintf(buf, sizeof(buf), "%d", val);
    WritePrivateProfileStringA("MDView", key, buf, g_iniPath);
}

static void save_setting_str(const char* key, const wchar_t* val) {
    if (!g_iniPath[0]) return;
    char buf[64];
    WideCharToMultiByte(CP_ACP, 0, val, -1, buf, sizeof(buf), NULL, NULL);
    WritePrivateProfileStringA("MDView", key, buf, g_iniPath);
}

static LONG_PTR mdview_set_window_ptr(HWND hwnd, int index, LONG_PTR value) {
#if defined(_WIN64)
    return SetWindowLongPtrW(hwnd, index, value);
#elif _WIN32_WINNT < 0x0600
    return (LONG_PTR)SetWindowLongW(hwnd, index, (LONG)value);
#else
    return SetWindowLongPtrW(hwnd, index, value);
#endif
}

static LONG_PTR mdview_get_window_ptr(HWND hwnd, int index) {
#if defined(_WIN64)
    return GetWindowLongPtrW(hwnd, index);
#elif _WIN32_WINNT < 0x0600
    return (LONG_PTR)GetWindowLongW(hwnd, index);
#else
    return GetWindowLongPtrW(hwnd, index);
#endif
}

/* Execute JavaScript on the browser document */

/* ── OLE command helper (copy/paste) ─────────────────────────────────── */

static void browser_exec_olecmd(IWebBrowser2* pBrowser, OLECMDID cmd) {
    if (!pBrowser) return;

    IDispatch* pDispDoc = NULL;
    if (FAILED(pBrowser->lpVtbl->get_Document(pBrowser, &pDispDoc)) || !pDispDoc) return;

    IOleCommandTarget* pCmd = NULL;
    if (SUCCEEDED(pDispDoc->lpVtbl->QueryInterface(pDispDoc, &IID_IOleCommandTarget, (void**)&pCmd)) && pCmd) {
        /* MSHTML generally accepts OLECMDIDs with NULL command group */
        pCmd->lpVtbl->Exec(pCmd, NULL, cmd, OLECMDEXECOPT_DODEFAULT, NULL, NULL);
        pCmd->lpVtbl->Release(pCmd);
    }
    pDispDoc->lpVtbl->Release(pDispDoc);
}


static void browser_execwb(IWebBrowser2* pBrowser, OLECMDID cmd) {
    if (!pBrowser) return;
    /* ExecWB is the most reliable way to get MSHTML to place CF_HTML + text on the clipboard (Copy),
       and to invoke the built-in paste behaviour (Paste). */
    IWebBrowser2_ExecWB(pBrowser, cmd, OLECMDEXECOPT_DODEFAULT, NULL, NULL);
}

static int is_child_of(HWND child, HWND parent) {
    while (child) {
        if (child == parent) return 1;
        child = GetParent(child);
    }
    return 0;
}

/* Ctrl+C behaviour:
   - if focus is in raw view: copy raw markdown (plain text)
   - if focus is in rendered view: copy formatted selection (HTML + text) */
static void do_copy(MDViewData* d) {
    if (!d) return;
    HWND f = GetFocus();

    if (d->hwndText && (f == d->hwndText || is_child_of(f, d->hwndText))) {
        SendMessageW(d->hwndText, WM_COPY, 0, 0);
        return;
    }
    if (d->pBrowser) {
        browser_execwb(d->pBrowser, OLECMDID_COPY);
    }
}

/* Ctrl+A behaviour:
   - if focus is in raw view: select all raw markdown
   - if focus is in rendered view: select all rendered content */
static void do_select_all(MDViewData* d) {
    if (!d) return;
    HWND f = GetFocus();

    if (d->hwndText && (f == d->hwndText || is_child_of(f, d->hwndText))) {
        SendMessageW(d->hwndText, EM_SETSEL, 0, -1);
        return;
    }
    if (d->pBrowser) {
        browser_execwb(d->pBrowser, OLECMDID_SELECTALL);
    }
}

/* Context menu command IDs */
enum {
    IDM_ZOOM_IN = 1001,
    IDM_ZOOM_OUT,
    IDM_ZOOM_RESET,
    IDM_TOGGLE_DARK,
    IDM_TOGGLE_TOC,
    IDM_SEARCH,
    IDM_PRINT,
    IDM_TOP,
    IDM_TOGGLE_LINEWRAP,
    IDM_WIDTH_TOGGLE,
    IDM_WIDTH_NORMAL,
    IDM_HELP,
    IDM_COPY,
    IDM_PASTE,
    IDM_TOGGLE_SPLIT
};

static void exec_js(IWebBrowser2* pB, const wchar_t* code) {
    if (!pB) return;
    IDispatch* pDisp = NULL;
    IWebBrowser2_get_Document(pB, &pDisp);
    if (!pDisp) return;
    IHTMLDocument2* pDoc = NULL;
    IDispatch_QueryInterface(pDisp, &IID_IHTMLDocument2, (void**)&pDoc);
    IDispatch_Release(pDisp);
    if (!pDoc) return;
    IHTMLWindow2* pWin = NULL;
    IHTMLDocument2_get_parentWindow(pDoc, &pWin);
    IHTMLDocument2_Release(pDoc);
    if (!pWin) return;
    BSTR bCode = SysAllocString(code);
    BSTR bLang = SysAllocString(L"JavaScript");
    VARIANT vResult;
    VariantInit(&vResult);
    IHTMLWindow2_execScript(pWin, bCode, bLang, &vResult);
    SysFreeString(bCode);
    SysFreeString(bLang);
    IHTMLWindow2_Release(pWin);
}

static void show_context_menu(MDViewData* d, HWND hwnd, int x, int y);

static wchar_t* utf8_to_wide_dup(const char* s) {
    if (!s) return NULL;
    int wl = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (wl <= 0) return NULL;
    wchar_t* w = (wchar_t*)calloc((size_t)wl, sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, wl);
    return w;
}

static size_t utf8_char_count(const char* s) {
    size_t count = 0;
    const unsigned char* p = (const unsigned char*)s;
    if (!p) return 0;
    while (*p) {
        if ((*p & 0xC0) != 0x80 && *p != '\r' && *p != '\n') ++count;
        ++p;
    }
    return count;
}

static void set_raw_count_label(HWND hwnd, size_t count) {
    if (!hwnd) return;
    wchar_t text[64];
    _snwprintf_s(text, _countof(text), _TRUNCATE, L"RAW chars: %llu", (unsigned long long)count);
    SetWindowTextW(hwnd, text);
}

static int get_document_title_utf8(IWebBrowser2* pB, char* out, int outsz) {
    if (!pB || !out || outsz <= 0) return 0;
    out[0] = '\0';

    IDispatch* pDisp = NULL;
    IWebBrowser2_get_Document(pB, &pDisp);
    if (!pDisp) return 0;

    IHTMLDocument2* pDoc = NULL;
    IDispatch_QueryInterface(pDisp, &IID_IHTMLDocument2, (void**)&pDoc);
    IDispatch_Release(pDisp);
    if (!pDoc) return 0;

    BSTR bTitle = NULL;
    IHTMLDocument2_get_title(pDoc, &bTitle);
    IHTMLDocument2_Release(pDoc);
    if (!bTitle) return 0;

    WideCharToMultiByte(CP_UTF8, 0, bTitle, -1, out, outsz, NULL, NULL);
    SysFreeString(bTitle);
    return 1;
}

static double get_html_scroll_ratio(MDViewData* d) {
    if (!d || !d->pBrowser) return 0.0;

    exec_js(d->pBrowser,
        L"(function(){var de=document.documentElement,b=document.body;"
        L"var st=(de&&de.scrollTop)|| (b&&b.scrollTop)||0;"
        L"var sh=Math.max((de&&de.scrollHeight)||0,(b&&b.scrollHeight)||0);"
        L"var ih=window.innerHeight|| (de&&de.clientHeight)||0;"
        L"document.title=''+st+','+sh+','+ih;})();");

    char t[128];
    if (!get_document_title_utf8(d->pBrowser, t, (int)sizeof(t))) return 0.0;

    double st=0, sh=0, ih=0;
    if (sscanf_s(t, "%lf,%lf,%lf", &st, &sh, &ih) != 3) return 0.0;

    double denom = sh - ih;
    if (denom < 1.0) denom = 1.0;
    double r = st / denom;
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 1.0;
    return r;
}

static void set_html_scroll_ratio(MDViewData* d, double r) {
    if (!d || !d->pBrowser) return;
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 1.0;

    wchar_t js[256];
    swprintf(js, 256,
        L"(function(){var de=document.documentElement,b=document.body;"
        L"var sh=Math.max((de&&de.scrollHeight)||0,(b&&b.scrollHeight)||0);"
        L"var ih=window.innerHeight|| (de&&de.clientHeight)||0;"
        L"var y=(sh-ih)*%.6f; if(y<0)y=0; window.scrollTo(0,y);})();", r);
    exec_js(d->pBrowser, js);
}

/* ── RichEdit scroll ratio helpers (method 1: ratio-based sync) ───────── */

static double get_edit_scroll_ratio(HWND hEdit) {
    if (!hEdit) return 0.0;

    SCROLLINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (!GetScrollInfo(hEdit, SB_VERT, &si)) return 0.0;

    /* Max scroll position per Win32 semantics */
    int maxPos = (int)si.nMax - (int)si.nPage + 1;
    if (maxPos < 1) return 0.0;

    double r = (double)si.nPos / (double)maxPos;
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 1.0;
    return r;
}

static void set_edit_scroll_ratio(HWND hEdit, double r) {
    if (!hEdit) return;
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 1.0;

    SCROLLINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (!GetScrollInfo(hEdit, SB_VERT, &si)) return;

    int maxPos = (int)si.nMax - (int)si.nPage + 1;
    if (maxPos < 1) return;

    int target = (int)((double)maxPos * r + 0.5);
    if (target < 0) target = 0;
    if (target > maxPos) target = maxPos;

    /* Ask control to scroll to target position.
       WM_VSCROLL with SB_THUMBPOSITION works reliably on RichEdit. */
    SendMessageW(hEdit, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, target), 0);
    SendMessageW(hEdit, WM_VSCROLL, MAKEWPARAM(SB_THUMBTRACK, target), 0);
}

static void sync_html_to_edit(MDViewData* d) {
    if (!d || !d->splitView || !d->hwndText) return;
    if (d->syncGuard) return;
    d->syncGuard = 1;

    /* Ratio-based mapping: HTML scroll ratio -> RichEdit scroll ratio */
    double r = get_html_scroll_ratio(d);
    set_edit_scroll_ratio(d->hwndText, r);

    d->syncGuard = 0;
}

static void sync_edit_to_html(MDViewData* d) {
    if (!d || !d->splitView || !d->hwndText) return;
    if (d->syncGuard) return;
    d->syncGuard = 1;

    /* Ratio-based mapping: RichEdit scroll ratio -> HTML scroll ratio */
    double r = get_edit_scroll_ratio(d->hwndText);
    set_html_scroll_ratio(d, r);

    d->syncGuard = 0;
}

static void layout_views(MDViewData* d) {
    if (!d || !d->hwndContainer || !d->pBrowser) return;

    RECT rc; GetClientRect(d->hwndContainer, &rc);
    int w = rc.right;
    int h = rc.bottom;
    const int rawHeaderH = 28;
    const int rawPad = 8;

    int leftW = w;
    int rightW = 0;
    int rawTextH = h - rawHeaderH;
    if (rawTextH < 0) rawTextH = 0;

    if (d->splitView && d->hwndText) {
        leftW = w / 2;
        rightW = w - leftW;
        if (leftW < 50) leftW = 50;
        if (rightW < 50) rightW = 50;
    }

    IWebBrowser2_put_Left(d->pBrowser, 0);
    IWebBrowser2_put_Top(d->pBrowser, 0);
    IWebBrowser2_put_Width(d->pBrowser, leftW);
    IWebBrowser2_put_Height(d->pBrowser, h);

    if (d->pOleObj) {
        IOleInPlaceObject* pIPO = NULL;
        IOleObject_QueryInterface(d->pOleObj, &IID_IOleInPlaceObject, (void**)&pIPO);
        if (pIPO) {
            RECT rB = { 0, 0, leftW, h };
            IOleInPlaceObject_SetObjectRects(pIPO, &rB, &rB);
            IOleInPlaceObject_Release(pIPO);
        }
    }

    HWND child = GetWindow(d->hwndContainer, GW_CHILD);
    while (child) {
        if (child == d->hwndTextCount) {
            if (d->splitView && d->hwndText) {
                ShowWindow(child, SW_SHOW);
                MoveWindow(child, leftW + rawPad, rawPad / 2, rightW - (rawPad * 2), rawHeaderH - rawPad, TRUE);
            } else {
                ShowWindow(child, SW_HIDE);
            }
        } else if (child == d->hwndText) {
            if (d->splitView) {
                ShowWindow(child, SW_SHOW);
                MoveWindow(child, leftW, rawHeaderH, rightW, rawTextH, TRUE);
            } else {
                ShowWindow(child, SW_HIDE);
            }
        } else {
            MoveWindow(child, 0, 0, leftW, h, TRUE);
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

static void toggle_split_view(MDViewData* d);

static LRESULT CALLBACK TextViewSubclassProc(HWND hwnd, UINT msg, WPARAM wP, LPARAM lP) {
    MDViewData* d = (MDViewData*)GetPropW(hwnd, L"MDViewData");
    if (!d) return DefWindowProcW(hwnd, msg, wP, lP);

    if (msg == WM_KEYDOWN) {
        int ctrl = GetKeyState(VK_CONTROL) & 0x8000;
        if (ctrl) {
            if (wP == 'M') { toggle_split_view(d); return 0; }
            if (wP == 'A') { SendMessageW(hwnd, EM_SETSEL, 0, -1); return 0; }
            if (wP == 'C') { SendMessageW(hwnd, WM_COPY, 0, 0); return 0; }
            if (wP == 'V') { SendMessageW(hwnd, WM_PASTE, 0, 0); return 0; }
            if (wP == 'F') {
                if (d->hwndIEServer) SetFocus(d->hwndIEServer);
                if (d->pBrowser) exec_js(d->pBrowser, L"sf()");
                return 0;
            }
        }

        if (wP == VK_ESCAPE) {
            HWND w = hwnd;
            while (w) {
                HWND p = GetParent(w);
                if (!p) break;
                w = p;
            }
            if (w) PostMessageW(w, WM_KEYDOWN, VK_ESCAPE, lP);
            return 0;
        }
        
        if (wP == VK_F3) {
            if (d->hwndIEServer) SetFocus(d->hwndIEServer);
            if (d->pBrowser) {
                if (GetKeyState(VK_SHIFT) & 0x8000) 
                    exec_js(d->pBrowser, L"fp()");
                else 
                    exec_js(d->pBrowser, L"fn()");
            }
            return 0;
        }
    }

    if (msg == WM_CONTEXTMENU) {
        int x = GET_X_LPARAM(lP);
        int y = GET_Y_LPARAM(lP);
        if (x == -1 && y == -1) {
            POINT pt; GetCursorPos(&pt);
            x = pt.x; y = pt.y;
        }
        show_context_menu(d, hwnd, x, y);
        return 0;
    }

    LRESULT r = CallWindowProcW(d->origTextProc, hwnd, msg, wP, lP);

    if (msg == WM_VSCROLL || msg == WM_MOUSEWHEEL ||
        (msg == WM_KEYDOWN && (wP == VK_UP || wP == VK_DOWN || wP == VK_PRIOR || wP == VK_NEXT || wP == VK_HOME || wP == VK_END))) {
        sync_edit_to_html(d);
    }

    return r;
}

static void toggle_split_view(MDViewData* d) {
    if (!d || !d->hwndContainer) return;

    if (!d->splitView) {
        if (!d->hwndText) {
            /* Ensure RichEdit class is available */
            /* Try RichEdit 4.1+ first, then fallback to 2.0/3.0 for XP compatibility */
            const wchar_t* editClass = L"RichEdit50W";
            if (!LoadLibraryW(L"Msftedit.dll")) {
                LoadLibraryW(L"Riched20.dll");
                editClass = L"RICHEDIT20W";
            }

            HWND hEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE, editClass, L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                ES_MULTILINE | ES_AUTOVSCROLL |
                ES_READONLY | ES_NOHIDESEL,
                0, 0, 0, 0,
                d->hwndContainer, NULL, g_hInstance, NULL);

            if (hEdit) {
                d->hwndText = hEdit;
                SetPropW(hEdit, L"MDViewData", (HANDLE)d);
                d->origTextProc = (WNDPROC)mdview_set_window_ptr(hEdit, GWLP_WNDPROC, (LONG_PTR)TextViewSubclassProc);

                d->hwndTextCount = CreateWindowExW(
                    0, L"STATIC", L"",
                    WS_CHILD | SS_RIGHT,
                    0, 0, 0, 0,
                    d->hwndContainer, NULL, g_hInstance, NULL);
                if (d->hwndTextCount) {
                    SendMessageW(d->hwndTextCount, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
                    set_raw_count_label(d->hwndTextCount, d->rawCharCount);
                }

                /* Monospace font for raw Markdown */
                if (!d->hTextFont || d->hTextFont == (HFONT)GetStockObject(DEFAULT_GUI_FONT)) {
                    HDC hdc = GetDC(hEdit);
                    int dpiY = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
                    if (hdc) ReleaseDC(hEdit, hdc);

                    LOGFONTW lf = { 0 };
                    lf.lfHeight = -MulDiv(g_settings.rawFontSize, dpiY, 72);
                    lf.lfCharSet = DEFAULT_CHARSET;
                    wcsncpy_s(lf.lfFaceName, LF_FACESIZE, g_settings.rawFontName, _TRUNCATE);
                    d->hTextFont = CreateFontIndirectW(&lf);
                    if (!d->hTextFont) d->hTextFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                }
                SendMessageW(hEdit, WM_SETFONT, (WPARAM)d->hTextFont, TRUE);

                /* Add some padding */
                SendMessageW(hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10, 10));

                if (d->mdUtf8) {
                    wchar_t* w = utf8_to_wide_dup(d->mdUtf8);
                    if (w) { SetWindowTextW(hEdit, w); free(w); }
                }
            }
        }
        d->splitView = 1;
    } else {
        d->splitView = 0;
    }

    layout_views(d);

    if (d->splitView && d->hwndText) sync_html_to_edit(d);
}


static void show_context_menu(MDViewData* d, HWND hwnd, int x, int y) {
    if (!d) return;

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, IDM_COPY,  L"Copy\tCtrl+C");
    AppendMenuW(hMenu, MF_STRING, IDM_PASTE, L"Paste\tCtrl+V");
    AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE_SPLIT, L"Toggle split text view\tCtrl+M");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING, IDM_ZOOM_IN,    L"Zoom in\tCtrl++");
    AppendMenuW(hMenu, MF_STRING, IDM_ZOOM_OUT,   L"Zoom out\tCtrl+-");
    AppendMenuW(hMenu, MF_STRING, IDM_ZOOM_RESET, L"Reset zoom\tCtrl+0");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE_DARK,     L"Toggle dark mode\tCtrl+D");
    AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE_TOC,      L"Toggle table of contents\tCtrl+T");
    AppendMenuW(hMenu, MF_STRING, IDM_SEARCH,          L"Search\tCtrl+F");
    AppendMenuW(hMenu, MF_STRING, IDM_PRINT,           L"Print\tCtrl+P");
    AppendMenuW(hMenu, MF_STRING, IDM_TOP,             L"Scroll to top\tCtrl+G");
    AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE_LINEWRAP, L"Toggle line wrap\tCtrl+L");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING, IDM_WIDTH_TOGGLE, L"Toggle content width\tCtrl+W");
    AppendMenuW(hMenu, MF_STRING, IDM_WIDTH_NORMAL, L"Content width (normal)\tCtrl+Shift+W");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING, IDM_HELP, L"Help\tF1");

    UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY;
    int cmd = (int)TrackPopupMenu(hMenu, flags, x, y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    switch (cmd) {
    case IDM_COPY:
        browser_exec_olecmd(d->pBrowser, OLECMDID_COPY);
        break;
    case IDM_PASTE:
        browser_exec_olecmd(d->pBrowser, OLECMDID_PASTE);
        break;
    case IDM_TOGGLE_SPLIT:
        toggle_split_view(d);
        break;

    case IDM_ZOOM_IN:
        exec_js(d->pBrowser, L"zi()");
        break;
    case IDM_ZOOM_OUT:
        exec_js(d->pBrowser, L"zo()");
        break;
    case IDM_ZOOM_RESET:
        exec_js(d->pBrowser, L"zr()");
        break;

    case IDM_TOGGLE_DARK:
        exec_js(d->pBrowser, L"td()");
        break;
    case IDM_TOGGLE_TOC:
        exec_js(d->pBrowser, L"ttoc()");
        break;
    case IDM_SEARCH:
        exec_js(d->pBrowser, L"sf()");
        break;
    case IDM_PRINT:
        exec_js(d->pBrowser, L"window.print()");
        break;
    case IDM_TOP:
        exec_js(d->pBrowser, L"window.scrollTo(0,0)");
        break;
    case IDM_TOGGLE_LINEWRAP:
        exec_js(d->pBrowser, L"tl()");
        break;
    case IDM_WIDTH_TOGGLE:
        exec_js(d->pBrowser, L"cw()");
        break;
    case IDM_WIDTH_NORMAL:
        exec_js(d->pBrowser, L"cn()");
        break;

    case IDM_HELP:
        exec_js(d->pBrowser, L"th()");
        break;
    default:
        break;
    }
}

/* Subclass proc for the IE Server window - only intercepts our Ctrl+ hotkeys,
   everything else (PgUp, PgDn, arrows, Escape, etc.) passes through untouched */
static LRESULT CALLBACK IEServerSubclassProc(HWND hwnd, UINT msg, WPARAM wP, LPARAM lP) {
    /* Get our container's data via the stored property */
    MDViewData* d = (MDViewData*)GetPropW(hwnd, L"MDViewData");
    if (!d) return DefWindowProcW(hwnd, msg, wP, lP);

    if (msg == WM_KEYDOWN) {
        int ctrl = GetKeyState(VK_CONTROL) & 0x8000;
        if (ctrl) {
            switch (wP) {
            case VK_OEM_PLUS: case VK_ADD:
                exec_js(d->pBrowser, L"zi()"); return 0;
            case VK_OEM_MINUS: case VK_SUBTRACT:
                exec_js(d->pBrowser, L"zo()"); return 0;
            case '0': case VK_NUMPAD0:
                exec_js(d->pBrowser, L"zr()"); return 0;
            case 'D':
                exec_js(d->pBrowser, L"td()"); return 0;
            case 'T':
                exec_js(d->pBrowser, L"ttoc()"); return 0;
            case 'F':
                exec_js(d->pBrowser, L"sf()"); return 0;
            case 'P':
                exec_js(d->pBrowser, L"window.print()"); return 0;
            case 'G':
                exec_js(d->pBrowser, L"window.scrollTo(0,0)"); return 0;
            case 'L':
                exec_js(d->pBrowser, L"tl()"); return 0;
            case 'W':
                if (GetKeyState(VK_SHIFT) & 0x8000)
                    exec_js(d->pBrowser, L"cn()");
                else
                    exec_js(d->pBrowser, L"cw()");
                return 0;
            case VK_OEM_2:
                exec_js(d->pBrowser, L"th()"); return 0;
            case 'A':
                do_select_all(d); return 0;
            case 'C':
                do_copy(d); return 0;
            case 'V':
                browser_execwb(d->pBrowser, OLECMDID_PASTE); return 0;
            case 'M':
                toggle_split_view(d); return 0;
            }
        }
        if (wP == VK_F1) {
            exec_js(d->pBrowser, L"th()"); return 0;
        }
        if (wP == VK_F3) {
            if (GetKeyState(VK_SHIFT) & 0x8000) 
                exec_js(d->pBrowser, L"fp()");
            else 
                exec_js(d->pBrowser, L"fn()");
            return 0;
        }
        
        /* Escape: the IE control eats it, so forward to TC's lister parent */
        if (wP == VK_ESCAPE) {
            HWND parent = GetParent(GetParent(GetParent(hwnd))); /* IE_Server -> Shell DocObj -> Shell Embed -> our container -> TC lister */
            if (!parent) parent = GetParent(GetParent(hwnd));
            /* Walk up to find TC's lister window and send Escape */
            HWND w = hwnd;
            while (w) {
                HWND p = GetParent(w);
                if (!p) break;
                /* Send to the topmost parent we can find */
                w = p;
            }
            if (w) PostMessageW(w, WM_KEYDOWN, VK_ESCAPE, lP);
            return 0;
        }
    }

    
    if (msg == WM_CONTEXTMENU) {
        int x = GET_X_LPARAM(lP);
        int y = GET_Y_LPARAM(lP);
        if (x == -1 && y == -1) {
            POINT pt; GetCursorPos(&pt);
            x = pt.x; y = pt.y;
        }
        show_context_menu(d, hwnd, x, y);
        return 0;
    }

{
        LRESULT r = CallWindowProcW(d->origIEProc, hwnd, msg, wP, lP);
        if (d->splitView && (msg == WM_VSCROLL || msg == WM_MOUSEWHEEL ||
            (msg == WM_KEYDOWN && (wP == VK_UP || wP == VK_DOWN || wP == VK_PRIOR || wP == VK_NEXT || wP == VK_HOME || wP == VK_END)))) {
            sync_html_to_edit(d);
        }
        return r;
    }
}

/* ── Minimal COM Site Implementation ─────────────────────────────────── */

typedef struct SiteImpl {
    IOleClientSite    clientSite;
    IOleInPlaceSite   inPlaceSite;
    IOleInPlaceFrame  inPlaceFrame;
    IDocHostUIHandler docHostUI;
    LONG              refCount;
    HWND              hwndParent;
} SiteImpl;

#define SITE_FROM_CLIENT(p)  ((SiteImpl*)((char*)(p) - offsetof(SiteImpl, clientSite)))
#define SITE_FROM_INPLACE(p) ((SiteImpl*)((char*)(p) - offsetof(SiteImpl, inPlaceSite)))
#define SITE_FROM_FRAME(p)   ((SiteImpl*)((char*)(p) - offsetof(SiteImpl, inPlaceFrame)))
#define SITE_FROM_DOCHOST(p) ((SiteImpl*)((char*)(p) - offsetof(SiteImpl, docHostUI)))

/* IOleClientSite */
static HRESULT STDMETHODCALLTYPE CS_QI(IOleClientSite* This, REFIID riid, void** ppv) {
    SiteImpl* s = SITE_FROM_CLIENT(This);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IOleClientSite)) *ppv = &s->clientSite;
    else if (IsEqualIID(riid, &IID_IOleInPlaceSite))  *ppv = &s->inPlaceSite;
    else if (IsEqualIID(riid, &IID_IOleInPlaceFrame) || IsEqualIID(riid, &IID_IOleWindow)) *ppv = &s->inPlaceFrame;
    else if (IsEqualIID(riid, &IID_IDocHostUIHandler)) *ppv = &s->docHostUI;
    else { *ppv = NULL; return E_NOINTERFACE; }
    InterlockedIncrement(&s->refCount); return S_OK;
}
static ULONG STDMETHODCALLTYPE CS_AddRef(IOleClientSite* This) { return InterlockedIncrement(&SITE_FROM_CLIENT(This)->refCount); }
static ULONG STDMETHODCALLTYPE CS_Release(IOleClientSite* This) { SiteImpl* s = SITE_FROM_CLIENT(This); LONG r = InterlockedDecrement(&s->refCount); if (r==0) free(s); return r; }
static HRESULT STDMETHODCALLTYPE CS_Save(IOleClientSite* This) { return S_OK; }
static HRESULT STDMETHODCALLTYPE CS_GetMoniker(IOleClientSite* This, DWORD a, DWORD b, IMoniker** c) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE CS_GetContainer(IOleClientSite* This, IOleContainer** c) { *c = NULL; return E_NOINTERFACE; }
static HRESULT STDMETHODCALLTYPE CS_ShowObj(IOleClientSite* This) { return S_OK; }
static HRESULT STDMETHODCALLTYPE CS_OnShow(IOleClientSite* This, BOOL f) { return S_OK; }
static HRESULT STDMETHODCALLTYPE CS_ReqLayout(IOleClientSite* This) { return E_NOTIMPL; }
static IOleClientSiteVtbl g_csVtbl = { CS_QI, CS_AddRef, CS_Release, CS_Save, CS_GetMoniker, CS_GetContainer, CS_ShowObj, CS_OnShow, CS_ReqLayout };

/* IOleInPlaceSite */
static HRESULT STDMETHODCALLTYPE IPS_QI(IOleInPlaceSite* This, REFIID riid, void** ppv) { return CS_QI(&SITE_FROM_INPLACE(This)->clientSite, riid, ppv); }
static ULONG STDMETHODCALLTYPE IPS_AddRef(IOleInPlaceSite* This) { return InterlockedIncrement(&SITE_FROM_INPLACE(This)->refCount); }
static ULONG STDMETHODCALLTYPE IPS_Release(IOleInPlaceSite* This) { SiteImpl* s = SITE_FROM_INPLACE(This); LONG r = InterlockedDecrement(&s->refCount); if(r==0) free(s); return r; }
static HRESULT STDMETHODCALLTYPE IPS_GetWindow(IOleInPlaceSite* This, HWND* h) { *h = SITE_FROM_INPLACE(This)->hwndParent; return S_OK; }
static HRESULT STDMETHODCALLTYPE IPS_CSHelp(IOleInPlaceSite* This, BOOL f) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE IPS_CanAct(IOleInPlaceSite* This) { return S_OK; }
static HRESULT STDMETHODCALLTYPE IPS_OnAct(IOleInPlaceSite* This) { return S_OK; }
static HRESULT STDMETHODCALLTYPE IPS_OnUI(IOleInPlaceSite* This) { return S_OK; }
static HRESULT STDMETHODCALLTYPE IPS_GetWinCtx(IOleInPlaceSite* This, IOleInPlaceFrame** ppF, IOleInPlaceUIWindow** ppD, LPRECT rP, LPRECT rC, LPOLEINPLACEFRAMEINFO fi) {
    SiteImpl* s = SITE_FROM_INPLACE(This); *ppF = (IOleInPlaceFrame*)&s->inPlaceFrame; InterlockedIncrement(&s->refCount);
    *ppD = NULL; GetClientRect(s->hwndParent, rP); *rC = *rP; fi->fMDIApp = FALSE; fi->hwndFrame = s->hwndParent; fi->haccel = NULL; fi->cAccelEntries = 0; return S_OK;
}
static HRESULT STDMETHODCALLTYPE IPS_Scroll(IOleInPlaceSite* This, SIZE s) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE IPS_UIDeact(IOleInPlaceSite* This, BOOL f) { return S_OK; }
static HRESULT STDMETHODCALLTYPE IPS_IPDeact(IOleInPlaceSite* This) { return S_OK; }
static HRESULT STDMETHODCALLTYPE IPS_Discard(IOleInPlaceSite* This) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE IPS_DeactUndo(IOleInPlaceSite* This) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE IPS_PosRect(IOleInPlaceSite* This, LPCRECT r) { return S_OK; }
static IOleInPlaceSiteVtbl g_ipsVtbl = { IPS_QI, IPS_AddRef, IPS_Release, IPS_GetWindow, IPS_CSHelp, IPS_CanAct, IPS_OnAct, IPS_OnUI, IPS_GetWinCtx, IPS_Scroll, IPS_UIDeact, IPS_IPDeact, IPS_Discard, IPS_DeactUndo, IPS_PosRect };

/* IOleInPlaceFrame */
static HRESULT STDMETHODCALLTYPE IPF_QI(IOleInPlaceFrame* This, REFIID riid, void** ppv) { return CS_QI(&SITE_FROM_FRAME(This)->clientSite, riid, ppv); }
static ULONG STDMETHODCALLTYPE IPF_AddRef(IOleInPlaceFrame* This) { return InterlockedIncrement(&SITE_FROM_FRAME(This)->refCount); }
static ULONG STDMETHODCALLTYPE IPF_Release(IOleInPlaceFrame* This) { SiteImpl* s = SITE_FROM_FRAME(This); LONG r = InterlockedDecrement(&s->refCount); if(r==0) free(s); return r; }
static HRESULT STDMETHODCALLTYPE IPF_GetWindow(IOleInPlaceFrame* This, HWND* h) { *h = SITE_FROM_FRAME(This)->hwndParent; return S_OK; }
static HRESULT STDMETHODCALLTYPE IPF_CSHelp(IOleInPlaceFrame* This, BOOL f) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE IPF_GetBorder(IOleInPlaceFrame* This, LPRECT r) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE IPF_ReqBorder(IOleInPlaceFrame* This, LPCBORDERWIDTHS b) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE IPF_SetBorder(IOleInPlaceFrame* This, LPCBORDERWIDTHS b) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE IPF_SetActive(IOleInPlaceFrame* This, IOleInPlaceActiveObject* a, LPCOLESTR s) { return S_OK; }
static HRESULT STDMETHODCALLTYPE IPF_InsMenus(IOleInPlaceFrame* This, HMENU h, LPOLEMENUGROUPWIDTHS w) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE IPF_SetMenu(IOleInPlaceFrame* This, HMENU h, HOLEMENU hm, HWND hw) { return S_OK; }
static HRESULT STDMETHODCALLTYPE IPF_RemMenus(IOleInPlaceFrame* This, HMENU h) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE IPF_SetStatus(IOleInPlaceFrame* This, LPCOLESTR t) { return S_OK; }
static HRESULT STDMETHODCALLTYPE IPF_EnableMod(IOleInPlaceFrame* This, BOOL f) { return S_OK; }
static HRESULT STDMETHODCALLTYPE IPF_TransAccel(IOleInPlaceFrame* This, LPMSG m, WORD w) { return E_NOTIMPL; }
static IOleInPlaceFrameVtbl g_ipfVtbl = { IPF_QI, IPF_AddRef, IPF_Release, IPF_GetWindow, IPF_CSHelp, IPF_GetBorder, IPF_ReqBorder, IPF_SetBorder, IPF_SetActive, IPF_InsMenus, IPF_SetMenu, IPF_RemMenus, IPF_SetStatus, IPF_EnableMod, IPF_TransAccel };

/* IDocHostUIHandler */
static HRESULT STDMETHODCALLTYPE DH_QI(IDocHostUIHandler* This, REFIID riid, void** ppv) { return CS_QI(&SITE_FROM_DOCHOST(This)->clientSite, riid, ppv); }
static ULONG STDMETHODCALLTYPE DH_AddRef(IDocHostUIHandler* This) { return InterlockedIncrement(&SITE_FROM_DOCHOST(This)->refCount); }
static ULONG STDMETHODCALLTYPE DH_Release(IDocHostUIHandler* This) { SiteImpl* s = SITE_FROM_DOCHOST(This); LONG r = InterlockedDecrement(&s->refCount); if(r==0) free(s); return r; }
static HRESULT STDMETHODCALLTYPE DH_CtxMenu(IDocHostUIHandler* This, DWORD id, POINT* pt, IUnknown* o, IDispatch* d) { return S_OK; }
static HRESULT STDMETHODCALLTYPE DH_GetHostInfo(IDocHostUIHandler* This, DOCHOSTUIINFO* p) { p->cbSize=sizeof(DOCHOSTUIINFO); p->dwFlags=DOCHOSTUIFLAG_NO3DBORDER|DOCHOSTUIFLAG_THEME; p->dwDoubleClick=DOCHOSTUIDBLCLK_DEFAULT; return S_OK; }
static HRESULT STDMETHODCALLTYPE DH_ShowUI(IDocHostUIHandler* This, DWORD a, IOleInPlaceActiveObject* b, IOleCommandTarget* c, IOleInPlaceFrame* d, IOleInPlaceUIWindow* e) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE DH_HideUI(IDocHostUIHandler* This) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE DH_UpdateUI(IDocHostUIHandler* This) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE DH_EnableMod(IDocHostUIHandler* This, BOOL f) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE DH_OnDocAct(IDocHostUIHandler* This, BOOL f) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE DH_OnFrmAct(IDocHostUIHandler* This, BOOL f) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE DH_Resize(IDocHostUIHandler* This, LPCRECT r, IOleInPlaceUIWindow* w, BOOL f) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE DH_TransAccel(IDocHostUIHandler* This, LPMSG m, const GUID* g, DWORD d) { return S_FALSE; }
static HRESULT STDMETHODCALLTYPE DH_OptKey(IDocHostUIHandler* This, LPOLESTR* p, DWORD d) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE DH_DropTgt(IDocHostUIHandler* This, IDropTarget* dt, IDropTarget** pdt) { return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE DH_GetExt(IDocHostUIHandler* This, IDispatch** ppd) { *ppd=NULL; return S_FALSE; }
static HRESULT STDMETHODCALLTYPE DH_TransUrl(IDocHostUIHandler* This, DWORD d, LPWSTR url, LPWSTR* purl) { return S_FALSE; }
static HRESULT STDMETHODCALLTYPE DH_FilterDO(IDocHostUIHandler* This, IDataObject* d, IDataObject** pd) { return S_FALSE; }
static IDocHostUIHandlerVtbl g_dhVtbl = { DH_QI, DH_AddRef, DH_Release, DH_CtxMenu, DH_GetHostInfo, DH_ShowUI, DH_HideUI, DH_UpdateUI, DH_EnableMod, DH_OnDocAct, DH_OnFrmAct, DH_Resize, DH_TransAccel, DH_OptKey, DH_DropTgt, DH_GetExt, DH_TransUrl, DH_FilterDO };

static SiteImpl* CreateSiteImpl(HWND hwnd) {
    SiteImpl* s = (SiteImpl*)calloc(1, sizeof(SiteImpl));
    if (!s) return NULL;
    s->clientSite.lpVtbl = &g_csVtbl; s->inPlaceSite.lpVtbl = &g_ipsVtbl;
    s->inPlaceFrame.lpVtbl = &g_ipfVtbl; s->docHostUI.lpVtbl = &g_dhVtbl;
    s->refCount = 1; s->hwndParent = hwnd;
    return s;
}

/* ── String Buffer ───────────────────────────────────────────────────── */

typedef struct { char* data; size_t len; size_t cap; } StrBuf;

enum { MDVIEW_MAX_FILE_SIZE = 16 * 1024 * 1024 };

static int sb_init(StrBuf* sb) {
    sb->cap = 4096;
    sb->data = (char*)malloc(sb->cap);
    if (!sb->data) {
        sb->cap = 0;
        sb->len = 0;
        return 0;
    }
    sb->data[0] = '\0';
    sb->len = 0;
    return 1;
}

static int sb_ensure(StrBuf* sb, size_t x) {
    while (sb->len + x + 1 > sb->cap) {
        size_t newCap = sb->cap ? sb->cap * 2 : 4096;
        char* newData = (char*)realloc(sb->data, newCap);
        if (!newData) return 0;
        sb->data = newData;
        sb->cap = newCap;
    }
    return 1;
}

static int sb_append(StrBuf* sb, const char* s) {
    size_t n = strlen(s);
    if (!sb_ensure(sb, n)) return 0;
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 1;
}

static int sb_append_char(StrBuf* sb, char c) {
    if (!sb_ensure(sb, 1)) return 0;
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return 1;
}
static void sb_append_esc(StrBuf* sb, const char* s, size_t n) {
    for(size_t i=0;i<n;i++) switch(s[i]){
        case '&': sb_append(sb,"&amp;"); break;
        case '<': sb_append(sb,"&lt;"); break;
        case '>': sb_append(sb,"&gt;"); break;
        case '"': sb_append(sb,"&quot;"); break;
        default:  sb_append_char(sb,s[i]); break;
    }
}

static int is_mermaid_lang(const char* lang) {
    if (!lang) return 0;
    while (*lang == ' ' || *lang == '\t') lang++;
    return _strnicmp(lang, "mermaid", 7) == 0 &&
           (lang[7] == '\0' || lang[7] == ' ' || lang[7] == '\t' || lang[7] == '`' || lang[7] == '~');
}

static void append_mermaid_block(StrBuf* sb, const char* src, size_t n) {
    sb_append(sb, "<div class=\"mdv-mermaid\" data-mdv-mermaid=\"1\"><pre class=\"mdv-mermaid-src\"><code>");
    sb_append_esc(sb, src, n);
    sb_append(sb, "</code></pre><div class=\"mdv-mermaid-view\"></div></div>\n");
}

static int is_safe_url_scheme(const char* url, size_t len, int allowMailto) {
    if (!url || len == 0) return 0;

    while (len > 0 && (*url == ' ' || *url == '\t' || *url == '\r' || *url == '\n')) {
        ++url;
        --len;
    }
    if (len == 0) return 0;

    if (len >= 8 && _strnicmp(url, "https://", 8) == 0) return 1;
    if (len >= 7 && _strnicmp(url, "http://", 7) == 0) return 1;
    if (allowMailto && len >= 7 && _strnicmp(url, "mailto:", 7) == 0) return 1;
    return 0;
}

/* ── Markdown Inline Parser ──────────────────────────────────────────── */

static void parse_inline(StrBuf* sb, const char* t, size_t len) {
    size_t i = 0;
    while (i < len) {
        /* Backslash escape */
        if (t[i]=='\\' && i+1<len) {
            char nx=t[i+1];
            if(nx=='*'||nx=='_'||nx=='`'||nx=='['||nx==']'||nx=='('||nx==')'||nx=='#'||nx=='~'||nx=='!'||nx=='|'||nx=='\\'||nx=='-')
            { sb_append_esc(sb,&t[i+1],1); i+=2; continue; }
        }
        /* Line break (2+ trailing spaces + \n) */
        if (t[i]==' ' && i+1<len && t[i+1]==' ') {
            size_t j=i+2; while(j<len&&t[j]==' ')j++;
            if(j<len&&t[j]=='\n'){ sb_append(sb,"<br>\n"); i=j+1; continue; }
        }
        /* Inline code */
        if (t[i]=='`') {
            int tk=0; size_t st=i; while(i<len&&t[i]=='`'){tk++;i++;}
            size_t e=i; int found=0;
            while(e<=len-tk){
                if(t[e]=='`'){ int ct=0;size_t ce=e; while(ce<len&&t[ce]=='`'){ct++;ce++;}
                    if(ct==tk){ sb_append(sb,"<code>"); sb_append_esc(sb,t+i,e-i); sb_append(sb,"</code>"); i=ce; found=1; break; } e=ce;
                } else e++;
            }
            if(!found) sb_append_esc(sb,t+st,tk);
            continue;
        }
        /* Image ![alt](url) */
        if (t[i]=='!' && i+1<len && t[i+1]=='[') {
            size_t as=i+2,j=as; int d=1;
            while(j<len&&d>0){if(t[j]=='[')d++;else if(t[j]==']')d--;if(d>0)j++;}
            if(j<len&&j+1<len&&t[j+1]=='('){
                size_t us=j+2,ue=us; while(ue<len&&t[ue]!=')')ue++;
                if(ue<len){
                    if (is_safe_url_scheme(t + us, ue - us, 0)) {
                        sb_append(sb,"<img alt=\""); sb_append_esc(sb,t+as,j-as);
                        sb_append(sb,"\" src=\""); sb_append_esc(sb,t+us,ue-us);
                        sb_append(sb,"\" style=\"max-width:100%\">");
                    } else {
                        sb_append_esc(sb, t + i, ue + 1 - i);
                    }
                    i=ue+1; continue;
                }
            }
        }
        /* Link [text](url) */
        if (t[i]=='[') {
            size_t ts=i+1,j=ts; int d=1;
            while(j<len&&d>0){if(t[j]=='[')d++;else if(t[j]==']')d--;if(d>0)j++;}
            if(j<len&&j+1<len&&t[j+1]=='('){
                size_t us=j+2,ue=us; while(ue<len&&t[ue]!=')')ue++;
                if(ue<len){
                    if (is_safe_url_scheme(t + us, ue - us, 1)) {
                        sb_append(sb,"<a href=\""); sb_append_esc(sb,t+us,ue-us);
                        sb_append(sb,"\">"); parse_inline(sb,t+ts,j-ts); sb_append(sb,"</a>");
                    } else {
                        sb_append_esc(sb, t + i, ue + 1 - i);
                    }
                    i=ue+1; continue;
                }
            }
        }
        /* Strikethrough ~~text~~ */
        if (t[i]=='~'&&i+1<len&&t[i+1]=='~') {
            size_t s2=i+2,e2=s2; while(e2+1<len&&!(t[e2]=='~'&&t[e2+1]=='~'))e2++;
            if(e2+1<len){ sb_append(sb,"<del>"); parse_inline(sb,t+s2,e2-s2); sb_append(sb,"</del>"); i=e2+2; continue; }
        }
        /* Bold+Italic ***text*** */
        if ((t[i]=='*'||t[i]=='_')&&i+2<len&&t[i+1]==t[i]&&t[i+2]==t[i]) {
            char m=t[i]; size_t s3=i+3,e3=s3;
            while(e3+2<len&&!(t[e3]==m&&t[e3+1]==m&&t[e3+2]==m))e3++;
            if(e3+2<len){ sb_append(sb,"<strong><em>"); parse_inline(sb,t+s3,e3-s3); sb_append(sb,"</em></strong>"); i=e3+3; continue; }
        }
        /* Bold **text** */
        if ((t[i]=='*'||t[i]=='_')&&i+1<len&&t[i+1]==t[i]) {
            char m=t[i]; size_t s2=i+2,e2=s2;
            while(e2+1<len&&!(t[e2]==m&&t[e2+1]==m))e2++;
            if(e2+1<len&&e2>s2){ sb_append(sb,"<strong>"); parse_inline(sb,t+s2,e2-s2); sb_append(sb,"</strong>"); i=e2+2; continue; }
        }
        /* Italic *text* */
        if ((t[i]=='*'||t[i]=='_')&&i+1<len&&t[i+1]!=t[i]&&t[i+1]!=' ') {
            char m=t[i]; size_t s1=i+1,e1=s1; while(e1<len&&t[e1]!=m)e1++;
            if(e1<len&&e1>s1&&t[e1-1]!=' '){ sb_append(sb,"<em>"); parse_inline(sb,t+s1,e1-s1); sb_append(sb,"</em>"); i=e1+1; continue; }
        }
        /* Autolink bare URLs */
        if (i+8<len&&(strncmp(t+i,"https://",8)==0||strncmp(t+i,"http://",7)==0)) {
            size_t us=i; while(i<len&&t[i]!=' '&&t[i]!='\n'&&t[i]!='\r'&&t[i]!=')'&&t[i]!='>'&&t[i]!='"')i++;
            while(i>us&&(t[i-1]=='.'||t[i-1]==','||t[i-1]==';'))i--;
            sb_append(sb,"<a href=\""); sb_append_esc(sb,t+us,i-us); sb_append(sb,"\">"); sb_append_esc(sb,t+us,i-us); sb_append(sb,"</a>"); continue;
        }
        /* Plain char */
        sb_append_esc(sb,&t[i],1); i++;
    }
}

/* ── Markdown Block Parser Helpers ───────────────────────────────────── */

static int count_leading(const char* l, char c) { int n=0; while(l[n]==c)n++; return n; }
typedef struct { char** lines; int count; } Lines;
static void free_lines(Lines* l);
static Lines split_lines(const char* text) {
    Lines r; r.count=0; int cap=256; r.lines=(char**)malloc(cap*sizeof(char*));
    if (!r.lines) return r;
    const char* p=text;
    while(*p){ const char* eol=p; while(*eol&&*eol!='\n')eol++;
        size_t ll=eol-p; if(ll>0&&p[ll-1]=='\r')ll--;
        char* line=(char*)malloc(ll+1);
        if(!line){ free_lines(&r); r.lines=NULL; r.count=0; return r; }
        memcpy(line,p,ll); line[ll]='\0';
        if(r.count>=cap){
            char** newLines;
            cap*=2;
            newLines=(char**)realloc(r.lines,cap*sizeof(char*));
            if(!newLines){ free(line); free_lines(&r); r.lines=NULL; r.count=0; return r; }
            r.lines=newLines;
        }
        r.lines[r.count++]=line; p=eol; if(*p=='\n')p++;
    } return r;
}
static void free_lines(Lines* l) { for(int i=0;i<l->count;i++) free(l->lines[i]); free(l->lines); }
static int is_hr(const char* l) { const char* p=l; while(*p==' ')p++; char c=*p; if(c!='-'&&c!='*'&&c!='_')return 0; int n=0; while(*p){if(*p==c)n++;else if(*p!=' ')return 0;p++;} return n>=3; }

static int is_table_sep(const char* l) {
    const char* p=l; while(*p==' ')p++; if(*p=='|')p++;
    int cells=0;
    while(*p){ while(*p==' ')p++; if(*p==':')p++; if(*p!='-')return 0; while(*p=='-')p++; if(*p==':')p++; while(*p==' ')p++; cells++; if(*p=='|'){p++;continue;} if(*p=='\0')break; return 0; }
    return cells>0;
}

static int parse_trow(const char* l, char cells[][1024], int mx) {
    const char* p=l; while(*p==' ')p++; if(*p=='|')p++;
    int nc=0;
    while(*p&&nc<mx){
        const char* s=p; int ic=0;
        while(*p){if(*p=='`')ic=!ic;if(*p=='\\'&&*(p+1)){p+=2;continue;}if(*p=='|'&&!ic)break;p++;}
        const char* e=p; while(s<e&&*s==' ')s++; while(e>s&&*(e-1)==' ')e--;
        size_t cl=e-s; if(cl>=1024)cl=1023; memcpy(cells[nc],s,cl); cells[nc][cl]='\0'; nc++;
        if(*p=='|')p++;
    }
    if(nc>0&&cells[nc-1][0]=='\0')nc--;
    return nc;
}

static void parse_talign(const char* l, char al[], int mx) {
    const char* p=l; while(*p==' ')p++; if(*p=='|')p++; int c=0;
    while(*p&&c<mx){ while(*p==' ')p++; int left=(*p==':');if(*p==':')p++; while(*p=='-')p++; int right=(*p==':');if(*p==':')p++; while(*p==' ')p++;
        if(left&&right)al[c]='c'; else if(right)al[c]='r'; else al[c]='l'; c++; if(*p=='|')p++;
    }
}

static int get_indent(const char* l) { int n=0; while(l[n]==' ')n++; if(l[n]=='\t')return n+4; return n; }
static int is_ul(const char* t) { return(t[0]=='-'||t[0]=='*'||t[0]=='+')&&t[1]==' '; }
static int is_ol(const char* t) { int i=0; while(t[i]>='0'&&t[i]<='9')i++; if(i==0||i>9)return 0; if((t[i]=='.'||t[i]==')')&&t[i+1]==' ')return i+2; return 0; }

/* ── Markdown Block Parser ───────────────────────────────────────────── */

static char* md_to_html(const char* markdown) {
    StrBuf sb; sb_init(&sb);
    Lines lines = split_lines(markdown);
    if (!sb.data || !lines.lines) {
        if (sb.data) free(sb.data);
        if (lines.lines) free_lines(&lines);
        return NULL;
    }
    int i = 0;

    while (i < lines.count) {
        const char* line = lines.lines[i];
        int indent = get_indent(line);
        const char* tr = line + indent;

        if (tr[0]=='\0') { i++; continue; }

        /* Fenced code block */
        if (strncmp(tr,"```",3)==0 || strncmp(tr,"~~~",3)==0) {
            char fc=tr[0]; const char* lang=tr+3; while(*lang==' ')lang++;
            int isMermaid = is_mermaid_lang(lang);
            StrBuf mermaid;
            if (isMermaid) {
                sb_init(&mermaid);
            } else {
                sb_append(&sb,"<pre><code");
                if(*lang){ sb_append(&sb," class=\"language-"); const char* le=lang; while(*le&&*le!=' '&&*le!='`'&&*le!='~')le++; sb_append_esc(&sb,lang,le-lang); sb_append(&sb,"\""); }
                sb_append(&sb,">");
            }
            i++;
            while(i<lines.count){ const char* cl=lines.lines[i]; const char* ct=cl; while(*ct==' ')ct++;
                if((fc=='`'&&strncmp(ct,"```",3)==0)||(fc=='~'&&strncmp(ct,"~~~",3)==0)){i++;break;}
                if (isMermaid) {
                    if (mermaid.len > 0) sb_append(&mermaid,"\n");
                    sb_append(&mermaid,cl);
                } else {
                    if(sb.data[sb.len-1]!='>') sb_append(&sb,"\n");
                    sb_append_esc(&sb,cl,strlen(cl));
                }
                i++;
            }
            if (isMermaid) {
                append_mermaid_block(&sb, mermaid.data, mermaid.len);
                free(mermaid.data);
            } else {
                sb_append(&sb,"</code></pre>\n");
            }
            continue;
        }

        /* Indented code block */
        if (indent>=4 && !is_ul(tr) && !is_ol(tr)) {
            sb_append(&sb,"<pre><code>");
            while(i<lines.count){ const char* cl=lines.lines[i];
                if(cl[0]=='\0'){if(i+1<lines.count&&get_indent(lines.lines[i+1])>=4){sb_append(&sb,"\n");i++;continue;}break;}
                if(get_indent(cl)<4)break;
                if(sb.data[sb.len-1]!='>') sb_append(&sb,"\n");
                sb_append_esc(&sb,cl+4,strlen(cl+4)); i++;
            }
            sb_append(&sb,"</code></pre>\n"); continue;
        }

        /* ATX Headings with id for TOC */
        if (tr[0]=='#') {
            int lv=count_leading(tr,'#');
            if(lv>=1&&lv<=6&&tr[lv]==' '){
                const char* c=tr+lv+1; size_t cl=strlen(c);
                while (cl > 0 && c[cl - 1] == '#')cl--; while (cl > 0 && c[cl - 1] == ' ')cl--;
                char tag[4]; _snprintf(tag, sizeof(tag), "h%d", lv);
                char idnum[16]; _snprintf(idnum, sizeof(idnum), "%d", i);
                sb_append(&sb,"<"); sb_append(&sb,tag); sb_append(&sb," id=\"mdv-h");
                sb_append(&sb,idnum); sb_append(&sb,"\">");
                parse_inline(&sb,c,cl);
                sb_append(&sb,"</"); sb_append(&sb,tag); sb_append(&sb,">\n"); i++; continue;
            }
        }

        /* Setext headings */
        if (i+1<lines.count && tr[0]!='\0') {
            const char* nx=lines.lines[i+1]; while(*nx==' ')nx++; int nl=(int)strlen(nx);
            if(nl>=1){ int ae=1,ad=1; for(int j=0;j<nl;j++){if(nx[j]!='=')ae=0;if(nx[j]!='-')ad=0;}
                if(ae){ sb_append(&sb,"<h1>"); parse_inline(&sb,tr,strlen(tr)); sb_append(&sb,"</h1>\n"); i+=2; continue; }
                if(ad&&!is_hr(lines.lines[i+1])){ sb_append(&sb,"<h2>"); parse_inline(&sb,tr,strlen(tr)); sb_append(&sb,"</h2>\n"); i+=2; continue; }
            }
        }

        /* HR */
        if (is_hr(line)) { sb_append(&sb,"<hr>\n"); i++; continue; }

        /* Blockquote */
        if (tr[0]=='>'&&(tr[1]==' '||tr[1]=='\0')) {
            StrBuf bq; sb_init(&bq);
            while(i<lines.count){ const char* bl=lines.lines[i]; while(*bl==' ')bl++;
                if(bl[0]=='>'&&(bl[1]==' '||bl[1]=='\0')){if(bq.len>0)sb_append(&bq,"\n");sb_append(&bq,bl[1]==' '?bl+2:bl+1);i++;}
                else if(bl[0]=='\0')break; else{sb_append(&bq,"\n");sb_append(&bq,bl);i++;}
            }
            char* inner=md_to_html(bq.data);
            sb_append(&sb,"<blockquote>\n"); sb_append(&sb,inner); sb_append(&sb,"</blockquote>\n");
            free(inner); free(bq.data); continue;
        }

        /* Table */
        if (i+1<lines.count && is_table_sep(lines.lines[i+1])) {
            char cells[64][1024]; char al[64]; memset(al,'l',sizeof(al));
            int nc=parse_trow(line,cells,64); parse_talign(lines.lines[i+1],al,64);
            sb_append(&sb,"<table>\n<thead>\n<tr>\n");
            for(int c=0;c<nc;c++){
                sb_append(&sb,"<th"); if(al[c]=='c')sb_append(&sb," style=\"text-align:center\""); else if(al[c]=='r')sb_append(&sb," style=\"text-align:right\"");
                sb_append(&sb,">"); parse_inline(&sb,cells[c],strlen(cells[c])); sb_append(&sb,"</th>\n");
            }
            sb_append(&sb,"</tr>\n</thead>\n<tbody>\n"); i+=2;
            while(i<lines.count){ const char* rl=lines.lines[i]; while(*rl==' ')rl++;
                if(rl[0]=='\0'||!strchr(rl,'|'))break;
                int rc=parse_trow(lines.lines[i],cells,64); sb_append(&sb,"<tr>\n");
                for(int c=0;c<nc;c++){
                    sb_append(&sb,"<td"); if(al[c]=='c')sb_append(&sb," style=\"text-align:center\""); else if(al[c]=='r')sb_append(&sb," style=\"text-align:right\"");
                    sb_append(&sb,">"); if(c<rc)parse_inline(&sb,cells[c],strlen(cells[c])); sb_append(&sb,"</td>\n");
                }
                sb_append(&sb,"</tr>\n"); i++;
            }
            sb_append(&sb,"</tbody>\n</table>\n"); continue;
        }

        /* Lists */
        if (is_ul(tr) || is_ol(tr)) {
            int ordered=is_ol(tr); int bi=indent;
            sb_append(&sb,ordered?"<ol>\n":"<ul>\n");
            while(i<lines.count){
                const char* ll=lines.lines[i]; int li=get_indent(ll); const char* lt=ll+li;
                int iu=is_ul(lt)&&li<=bi+1; int om=is_ol(lt); int io=om&&li<=bi+1;
                if(lt[0]=='\0'){i++;continue;}
                if(!iu&&!io&&li<=bi)break;
                if(iu||io){
                    const char* ic=iu?lt+2:lt+om;
                    int task=0,chk=0;
                    if(strncmp(ic,"[ ] ",4)==0){task=1;ic+=4;}
                    else if(strncmp(ic,"[x] ",4)==0||strncmp(ic,"[X] ",4)==0){task=1;chk=1;ic+=4;}
                    sb_append(&sb,"<li>");
                    if(task) sb_append(&sb,chk?"<input type=\"checkbox\" checked disabled> ":"<input type=\"checkbox\" disabled> ");
                    parse_inline(&sb,ic,strlen(ic)); i++;
                    StrBuf nest; sb_init(&nest); int hn=0;
                    while(i<lines.count){ const char* nl=lines.lines[i]; int ni=get_indent(nl); const char* nt=nl+ni;
                        if(nt[0]=='\0'){if(i+1<lines.count&&get_indent(lines.lines[i+1])>bi+1){sb_append(&nest,"\n");i++;hn=1;continue;}break;}
                        if(ni>bi+1){if(nest.len>0)sb_append(&nest,"\n");sb_append(&nest,nl);hn=1;i++;}else break;
                    }
                    if(hn){char* nh=md_to_html(nest.data);sb_append(&sb,"\n");sb_append(&sb,nh);free(nh);}
                    free(nest.data); sb_append(&sb,"</li>\n");
                } else i++;
            }
            sb_append(&sb,ordered?"</ol>\n":"</ul>\n"); continue;
        }

        /* Paragraph */
        { StrBuf para; sb_init(&para);
            while(i<lines.count){
                const char* pl=lines.lines[i]; int pi=get_indent(pl); const char* pt=pl+pi;
                if(pt[0]=='\0')break; if(pt[0]=='#'&&pt[1]==' ')break; if(is_hr(pl))break;
                if(pt[0]=='>'&&(pt[1]==' '||pt[1]=='\0'))break;
                if(strncmp(pt,"```",3)==0||strncmp(pt,"~~~",3)==0)break;
                if(is_ul(pt)||is_ol(pt))break;
                if(i+1<lines.count&&is_table_sep(lines.lines[i+1]))break;
                if(para.len>0) sb_append(&para,"\n");
                sb_append(&para,tr); i++;
                if(i<lines.count){ const char* nx=lines.lines[i]; while(*nx==' ')nx++; int nl2=(int)strlen(nx);
                    if(nl2>=1){int ae=1,ad=1;for(int j=0;j<nl2;j++){if(nx[j]!='=')ae=0;if(nx[j]!='-')ad=0;}if(ae||ad)break;}
                    pi=get_indent(lines.lines[i]); tr=lines.lines[i]+pi;
                }
            }
            sb_append(&sb,"<p>"); parse_inline(&sb,para.data,para.len); sb_append(&sb,"</p>\n");
            free(para.data);
        }
    }
    free_lines(&lines);
    return sb.data;
}

/* ── Theme Detection ─────────────────────────────────────────────────── */

static int is_dark_theme(void) {
#if _WIN32_WINNT >= 0x0600
    HKEY hKey; DWORD val=1, sz=sizeof(DWORD);
    if(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey)==ERROR_SUCCESS){
        RegQueryValueExW(hKey,L"AppsUseLightTheme",NULL,NULL,(LPBYTE)&val,&sz); RegCloseKey(hKey); return val==0;
    } return 0;
#else
    return 0;
#endif
}

/* ── CSS ─────────────────────────────────────────────────────────────── */

static void build_css(StrBuf* sb) {
    sb_append(sb,
    "*{box-sizing:border-box}"
    "html{background:#fff;min-height:100%;width:100%}");

    /* Body — full viewport background */
    sb_append(sb, "body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;");
    { char tmp[64]; _snprintf(tmp, sizeof(tmp), "font-size:%dpx;", g_settings.fontSize); sb_append(sb, tmp); }
    sb_append(sb, "line-height:1.7;color:#24292e;background:#fff;margin:0;padding:0;"
    "transition:background .2s,color .2s}");
    sb_append(sb, "body.dark{color:#d4d4d4;background:#1e1e1e}");

    /* Content container — centered, optional max-width */
    sb_append(sb, "#mdv-ct{margin:0 auto;padding:12px 32px 24px;");
    if (g_settings.maxWidth > 0) {
        char tmp[64]; sprintf_s(tmp, sizeof(tmp), "max-width:%dpx;", g_settings.maxWidth); sb_append(sb, tmp);
    }
    sb_append(sb, "}");

    sb_append(sb,
    "h1,h2,h3,h4,h5,h6{color:#1a1a1a;margin-top:1.4em;margin-bottom:.6em;font-weight:600}"
    "body.dark h1,body.dark h2,body.dark h3,body.dark h4,body.dark h5,body.dark h6{color:#e0e0e0}"
    "#mdv-ct>:first-child{margin-top:0}"
    "h1{font-size:2em;padding-bottom:.3em;border-bottom:1px solid #e1e4e8}"
    "h2{font-size:1.5em;padding-bottom:.25em;border-bottom:1px solid #e1e4e8}"
    "body.dark h1,body.dark h2{border-bottom-color:#444}"
    "h3{font-size:1.25em}"
    "a{color:#0366d6;text-decoration:none}a:hover{text-decoration:underline}"
    "body.dark a{color:#569cd6}"
    "code{font-family:Consolas,'Courier New',monospace;background:#f6f8fa;"
    "padding:2px 6px;border-radius:3px;font-size:.9em;color:#d73a49}"
    "body.dark code{background:#2d2d2d;color:#ce9178}"
    "pre{background:#f6f8fa;border:1px solid #e1e4e8;border-radius:6px;"
    "padding:16px;overflow-x:auto;line-height:1.5;position:relative;white-space:pre-wrap;word-wrap:break-word}"
    "body.dark pre{background:#2d2d2d;border-color:#404040}"
    "pre code{background:none;padding:0;color:#24292e;white-space:pre-wrap;word-wrap:break-word}"
    "body.dark pre code{color:#d4d4d4}"
    ".mdv-mermaid{margin:1em 0}"
    ".mdv-mermaid-view{display:none;margin-top:10px;padding:16px;background:#f6f8fa;border:1px solid #e1e4e8;border-radius:6px;overflow:hidden;font-size:1em;text-align:center}"
    "body.dark .mdv-mermaid-view{background:#2d2d2d;border-color:#404040}"
    ".mdv-mermaid.ok .mdv-mermaid-view{display:block}"
    ".mdv-mermaid.ok .mdv-mermaid-src{display:none}"
    ".mdv-mermaid svg{display:block;width:auto;max-width:100%;height:auto;margin:0 auto;font-family:inherit;font-size:inherit}"
    ".mdv-mm-node{fill:#ffffff;stroke:#57606a;stroke-width:1.5}"
    ".mdv-mm-edge{stroke:#57606a;stroke-width:2;fill:none}"
    ".mdv-mm-edge.dash{stroke-dasharray:6 4}"
    ".mdv-mm-edge.bold{stroke-width:3}"
    ".mdv-mm-life{stroke:#8b949e;stroke-width:1.5;stroke-dasharray:6 4;fill:none}"
    ".mdv-mm-note{fill:#fff8c5;stroke:#8b949e;stroke-width:1.2}"
    ".mdv-mermaid svg text{font-family:inherit;font-size:1em}"
    ".mdv-mm-text{fill:#24292e;font-family:inherit;font-size:1em}"
    ".mdv-mm-note-text{fill:#24292e;font-family:inherit;font-size:1em}"
    "body.dark .mdv-mm-node{fill:#1e1e1e;stroke:#9da7b3}"
    "body.dark .mdv-mm-edge{stroke:#9da7b3}"
    "body.dark .mdv-mm-life{stroke:#9da7b3}"
    "body.dark .mdv-mm-note{fill:#4a4a48;stroke:#80868f}"
    "body.dark .mdv-mm-text{fill:#d4d4d4}"
    "body.dark .mdv-mm-note-text{fill:#f0f0f0}"
    "blockquote{margin:.8em 0;padding:.5em 1em;border-left:4px solid #0366d6;"
    "background:#f6f8fa;color:#6a737d}"
    "body.dark blockquote{border-left-color:#569cd6;background:#252526;color:#aaa}"
    "blockquote p{margin:.4em 0}"
    "table{border-collapse:collapse;width:100%;margin:1em 0}"
    "th,td{border:1px solid #dfe2e5;padding:8px 12px}"
    "body.dark th,body.dark td{border-color:#444}"
    "th{background:#f6f8fa;font-weight:600}"
    "body.dark th{background:#2d2d2d}"
    "tr:nth-child(even){background:#f9f9f9}"
    "body.dark tr:nth-child(even){background:#252526}"
    "hr{border:none;border-top:1px solid #e1e4e8;margin:1.5em 0}"
    "body.dark hr{border-top-color:#444}"
    "img{max-width:100%;border-radius:4px}"
    "ul,ol{padding-left:2em}li{margin:.3em 0}"
    "input[type=checkbox]{margin-right:6px}"
    "del{color:#999}body.dark del{color:#888}"

    /* Line numbers */
    "pre.ln{padding-left:0}pre.ln code{display:block}"
    ".ln-wrap{display:table;width:100%}"
    ".ln-nums{display:table-cell;width:1px;padding:0 12px;text-align:right;"
    "color:#6a737d;border-right:1px solid #e1e4e8;user-select:none;"
    "-webkit-user-select:none;white-space:pre;vertical-align:top;"
    "font-family:Consolas,'Courier New',monospace;font-size:.9em;line-height:1.5}"
    "body.dark .ln-nums{color:#aaa;border-right-color:#404040}"
    ".ln-code{display:table-cell;padding:0 16px;white-space:pre-wrap;word-wrap:break-word;"
    "vertical-align:top;overflow-x:auto;font-family:Consolas,'Courier New',monospace;font-size:.9em;line-height:1.5}"

    /* Expand/collapse */
    ".mdv-collapsible{max-height:400px;overflow:hidden;position:relative}"
    ".mdv-collapsible.expanded{max-height:none}"
    ".mdv-expand-btn{display:block;text-align:center;padding:8px;margin-top:4px;cursor:pointer;"
    "color:#0366d6;font-size:13px;font-family:'Segoe UI',sans-serif;"
    "background:linear-gradient(transparent,#f6f8fa 60%);border:none;position:relative}"
    "body.dark .mdv-expand-btn{color:#569cd6;background:linear-gradient(transparent,#2d2d2d 60%)}"
    ".mdv-collapse-fade{position:absolute;bottom:0;left:0;right:0;height:60px;"
    "background:linear-gradient(rgba(246,248,250,0),#f6f8fa);pointer-events:none}"
    "body.dark .mdv-collapse-fade{background:linear-gradient(rgba(45,45,45,0),#2d2d2d)}"

    /* Syntax highlighting */
    ".sh-kw{color:#d73a49}body.dark .sh-kw{color:#569cd6}"
    ".sh-str{color:#032f62}body.dark .sh-str{color:#ce9178}"
    ".sh-num{color:#005cc5}body.dark .sh-num{color:#b5cea8}"
    ".sh-cm{color:#6a737d;font-style:italic}body.dark .sh-cm{color:#6a9955;font-style:italic}"
    ".sh-fn{color:#6f42c1}body.dark .sh-fn{color:#dcdcaa}"
    ".sh-op{color:#d73a49}body.dark .sh-op{color:#d4d4d4}"
    ".sh-type{color:#22863a}body.dark .sh-type{color:#4ec9b0}"
    ".sh-tag{color:#22863a}body.dark .sh-tag{color:#569cd6}"
    ".sh-attr{color:#6f42c1}body.dark .sh-attr{color:#9cdcfe}"
    ".sh-val{color:#032f62}body.dark .sh-val{color:#ce9178}"

    /* Find bar */
    "#mdv-fb{display:none;position:fixed;top:0;left:0;right:0;z-index:10000;"
    "background:#f0f0f0;border-bottom:1px solid #ccc;"
    "padding:6px 12px;font:13px 'Segoe UI',sans-serif;box-shadow:0 2px 8px rgba(0,0,0,.15)}"
    "body.dark #mdv-fb{background:#2d2d2d;border-bottom-color:#555}"
    "#mdv-fb.on{display:flex;align-items:center;gap:8px}"
    "#mdv-fi{padding:4px 8px;border:1px solid #bbb;border-radius:3px;"
    "font-size:13px;width:280px;outline:none;background:#fff;color:#24292e}"
    "body.dark #mdv-fi{background:#1e1e1e;border-color:#555;color:#d4d4d4}"
    "#mdv-fi:focus{border-color:#0366d6}"
    "body.dark #mdv-fi:focus{border-color:#569cd6}"
    "#mdv-fc{color:#6a737d;min-width:70px;font-size:12px}"
    ".fb{padding:3px 10px;border:1px solid #bbb;border-radius:3px;"
    "background:#e0e0e0;color:#24292e;cursor:pointer;font-size:13px}"
    "body.dark .fb{background:#404040;border-color:#555;color:#d4d4d4}"
    ".fb:hover{background:#d0d0d0}body.dark .fb:hover{background:#505050}"
    ".hl{background:#fff59d;color:#000;border-radius:2px}"
    ".hl-a{background:#ff9800;color:#000;font-weight:bold}"
    "body.dark .hl{background:#6b5b00}body.dark .hl-a{background:#b37400}"

    /* TOC */
    "#mdv-toc{display:none;position:fixed;top:0;right:0;bottom:0;width:280px;z-index:9999;"
    "background:#f6f8fa;border-left:1px solid #e1e4e8;"
    "overflow-y:auto;padding:16px 0;font:13px 'Segoe UI',sans-serif;"
    "box-shadow:-2px 0 12px rgba(0,0,0,.1)}"
    "body.dark #mdv-toc{background:#252526;border-left-color:#404040}"
    "#mdv-toc.on{display:block}"
    "#mdv-toc-t{padding:0 16px 12px;font-size:14px;font-weight:700;color:#1a1a1a;"
    "border-bottom:1px solid #e1e4e8;margin-bottom:8px}"
    "body.dark #mdv-toc-t{color:#e0e0e0;border-bottom-color:#404040}"
    ".ti{display:block;padding:4px 16px;color:#24292e;text-decoration:none;cursor:pointer;border-left:3px solid transparent}"
    "body.dark .ti{color:#d4d4d4}"
    ".ti:hover{background:#ebeef1;border-left-color:#0366d6}"
    "body.dark .ti:hover{background:#2d2d2d;border-left-color:#569cd6}"
    ".t1{font-weight:600}.t2{padding-left:28px}.t3{padding-left:40px;font-size:12px}"
    ".t4{padding-left:52px;font-size:12px;color:#6a737d}body.dark .t4{color:#aaa}"

    /* Toast */
    "#mdv-toast{display:none;position:fixed;bottom:20px;left:50%;transform:translateX(-50%);"
    "background:#333;color:#fff;padding:8px 20px;"
    "border-radius:6px;font:13px 'Segoe UI',sans-serif;"
    "box-shadow:0 4px 12px rgba(0,0,0,.3);z-index:10001;opacity:0;transition:opacity .3s}"
    "body.dark #mdv-toast{background:#555}"
    "#mdv-toast.on{display:block;opacity:1}"

    /* Progress */
    "#mdv-prog{position:fixed;top:0;left:0;height:3px;z-index:10002;"
    "background:#0366d6;width:0;transition:width .1s}"
    "body.dark #mdv-prog{background:#569cd6}"

    /* Char count */
    "#mdv-char{position:fixed;right:16px;bottom:16px;z-index:10002;"
    "padding:6px 10px;border-radius:999px;background:rgba(255,255,255,.92);"
    "border:1px solid #d0d7de;color:#57606a;font:12px 'Segoe UI',sans-serif;"
    "box-shadow:0 3px 12px rgba(0,0,0,.10);pointer-events:none}"
    "body.dark #mdv-char{background:rgba(37,37,38,.94);border-color:#444;color:#c9d1d9}"

    /* Help overlay */
    "#mdv-help{display:none;position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);"
    "background:#ffffff;border:1px solid #ccc;border-radius:12px;"
    "padding:28px 36px;z-index:10003;box-shadow:0 12px 48px rgba(0,0,0,.35);"
    "font:16px 'Segoe UI',sans-serif;min-width:440px;max-width:520px;color:#24292e}"
    "body.dark #mdv-help{background:#2d2d2d;border-color:#555;color:#d4d4d4}"
    "#mdv-help.on{display:block}"
    "#mdv-help h3{margin:0 0 16px;color:#1a1a1a;font-size:20px;font-weight:700;"
    "padding-bottom:12px;border-bottom:1px solid #ddd}"
    "body.dark #mdv-help h3{color:#e0e0e0;border-bottom-color:#444}"
    ".hrow{display:flex;justify-content:space-between;align-items:center;padding:7px 0}"
    ".hrow span:first-child{color:#24292e;font-size:15px}"
    "body.dark .hrow span:first-child{color:#d4d4d4}"
    ".hkeys{display:flex;gap:5px;align-items:center}"
    ".kc{display:inline-block;font-family:Consolas,'Courier New',monospace;font-size:13px;"
    "background:#f0f0f0;color:#333;padding:4px 9px;border-radius:5px;"
    "border:1px solid #ccc;border-bottom-width:2px;"
    "min-width:26px;text-align:center;line-height:1.3;"
    "box-shadow:0 1px 0 #bbb}"
    "body.dark .kc{background:#404040;color:#ddd;border-color:#555;box-shadow:0 1px 0 #333}"
    ".kc-plus{color:#999;font-size:12px;padding:0 2px}"
    "body.dark .kc-plus{color:#888}"
    ".help-sep{height:1px;background:#e0e0e0;margin:8px 0}"
    "body.dark .help-sep{background:#444}"
    ".help-foot{font-size:13px;color:#999;text-align:center;margin-top:14px}"
    "body.dark .help-foot{color:#777}"
    );
}

/* ── JavaScript ──────────────────────────────────────────────────────── */

static void build_js(StrBuf* sb) {
    /* Initial values from settings */
    char init[128];
    _snprintf(init, sizeof(init), "<script>var fs=%d,mw=%d,ln=%d,tt=null;",
            g_settings.fontSize, g_settings.maxWidth, g_settings.lineNums);
    sb_append(sb, init);

    sb_append(sb,
    /* Toast */
    "function toast(m){var t=document.getElementById('mdv-toast');t.innerText=m;t.className='on';"
    "if(tt)clearTimeout(tt);tt=setTimeout(function(){t.className=''},1500)}"

    /* Char count */
    "function rcc(){var ct=document.getElementById('mdv-ct'),out=[],walker,n,pv;"
    "if(!ct)return 0;"
    "walker=document.createTreeWalker?document.createTreeWalker(ct,4,null,false):mdvTextWalker(ct);"
    "function inPre(node){while(node&&node!==ct){var nm=node.nodeName;if(nm==='PRE'||nm==='CODE'||nm==='TEXTAREA')return true;node=node.parentNode;}return false;}"
    "function isHidden(node){while(node&&node!==ct){if(node.nodeType===1&&node.currentStyle&&node.currentStyle.display==='none')return true;node=node.parentNode;}return false;}"
    "while(walker.nextNode()){n=walker.currentNode;if(!n||!n.nodeValue||isHidden(n.parentNode))continue;"
    "if(inPre(n.parentNode)){out.push(n.nodeValue.replace(/\\r|\\n/g,''));pv=n.nodeValue.slice(-1);continue;}"
    "var t=n.nodeValue.replace(/\\u00a0/g,' ').replace(/\\s+/g,' ');"
    "if(!t)continue;"
    "if(pv===' '&&t.charAt(0)===' ')t=t.substring(1);"
    "out.push(t);pv=t.slice(-1);}"
    "return out.join('').replace(/\\r|\\n/g,'').replace(/^\\s+|\\s+$/g,'').length;}"
    "function ucc(){var el=document.getElementById('mdv-char');if(el)el.innerText='MD chars: '+rcc();}"

    /* Zoom */
    "function zi(){fs=Math.min(fs+1,30);af()}"
    "function zo(){fs=Math.max(fs-1,9);af()}"
    "function zr(){fs=19;af()}"
    "function fitMermaidSvg(svg){var vb,parts,baseW,baseH,view,availW,targetW,targetH;"
    "if(!svg)return;vb=svg.getAttribute('viewBox');if(!vb)return;parts=vb.split(/\\s+/);if(parts.length!==4)return;"
    "baseW=parseFloat(parts[2]);baseH=parseFloat(parts[3]);if(!(baseW>0)||!(baseH>0))return;"
    "view=svg.parentNode;availW=view?(view.clientWidth||view.offsetWidth):0;"
    "if(!(availW>0)){if(window.setTimeout&&!svg.getAttribute('data-mdv-fit-pending')){svg.setAttribute('data-mdv-fit-pending','1');window.setTimeout(function(){try{svg.removeAttribute('data-mdv-fit-pending');fitMermaidSvg(svg);}catch(ex){}},0);}return;}"
    "availW=Math.max(220,availW-4);targetW=Math.min(availW,baseW);targetH=Math.max(80,Math.round(baseH*(targetW/baseW)));"
    "svg.style.width=targetW+'px';svg.style.height=targetH+'px';}"
    "function syncMermaidTypography(){var svgs=document.querySelectorAll('.mdv-mermaid svg');"
    "var cs=window.getComputedStyle?window.getComputedStyle(document.body,null):null;"
    "var ff=cs&&cs.fontFamily?cs.fontFamily:document.body.style.fontFamily;"
    "var fz=cs&&cs.fontSize?cs.fontSize:document.body.style.fontSize;"
    "for(var i=0;i<svgs.length;i++){if(ff)svgs[i].style.fontFamily=ff;if(fz)svgs[i].style.fontSize=fz;fitMermaidSvg(svgs[i]);}}"
    "function mmBodyFontPx(){var cs=window.getComputedStyle?window.getComputedStyle(document.body,null):null;"
    "var fz=cs&&cs.fontSize?parseFloat(cs.fontSize):parseFloat(document.body.style.fontSize);"
    "if(!(fz>0))fz=16;return fz;}"
    "function af(){document.body.style.fontSize=fs+'px';syncMermaidTypography();toast('Font: '+fs+'px')}"

    /* Theme toggle */
    "function td(){var b=document.body,h=document.documentElement;"
    "if(b.className.indexOf('dark')>=0){b.className=b.className.replace('dark','').replace(/^\\s+|\\s+$/g,'');"
    "h.style.background='#fff';toast('Light mode')}"
    "else{b.className=(b.className?b.className+' ':'')+'dark';"
    "h.style.background='#1e1e1e';toast('Dark mode')}}"

    /* Column width */
    "function cw(){if(mw===0)mw=800;mw=Math.min(mw+80,9999);aw()}"
    "function cn(){if(mw===0)return;mw=mw-80;if(mw<400){mw=0;document.getElementById('mdv-ct').style.maxWidth='none';syncMermaidTypography();toast('Width: full');return;}aw()}"
    "function aw(){document.getElementById('mdv-ct').style.maxWidth=mw+'px';syncMermaidTypography();toast('Width: '+mw+'px')}"

    /* Line numbers toggle */
    "function tl(){"
    "ln=ln?0:1;var ps=document.querySelectorAll('pre');"
    "for(var i=0;i<ps.length;i++){"
    "var p=ps[i];"
    "if(ln&&!p._lnDone){"
    "  var code=p.getElementsByTagName('code')[0];if(!code)continue;"
    "  var htm=code.innerHTML;"
    /* Count lines by splitting on newlines in the HTML */
    "  var tmp=htm.replace(/<br\\s*\\/?>/gi,'\\n');"
    "  var lines=tmp.split('\\n');"
    "  while(lines.length>1&&lines[lines.length-1].replace(/\\s|&nbsp;/g,'')==='')lines.pop();"
    "  var nums='';"
    "  for(var j=0;j<lines.length;j++){nums+=(j+1)+'\\n';}"
    "  var wrap=document.createElement('div');wrap.className='ln-wrap';"
    "  var nd=document.createElement('div');nd.className='ln-nums';"
    "  nd.style.whiteSpace='pre';nd.appendChild(document.createTextNode(nums));"
    "  var cd=document.createElement('div');cd.className='ln-code';cd.innerHTML=htm;"
    "  wrap.appendChild(nd);wrap.appendChild(cd);"
    "  code.innerHTML='';code.appendChild(wrap);"
    "  p.className=(p.className?p.className+' ':'')+'ln';p._lnDone=1;"
    "}else if(!ln&&p._lnDone){"
    "  var cd2=p.querySelector('.ln-code');if(cd2){var code2=p.getElementsByTagName('code')[0];"
    "  if(code2){code2.innerHTML=cd2.innerHTML;}}"
    "  p.className=p.className.replace(/\\bln\\b/g,'').replace(/^\\s+|\\s+$/g,'');p._lnDone=0;"
    "}}"
    "toast(ln?'Line numbers ON':'Line numbers OFF')}"

    /* TOC */
    "function btoc(){var toc=document.getElementById('mdv-toc');"
    "var hs=document.querySelectorAll('h1[id],h2[id],h3[id],h4[id]');"
    "var old=toc.querySelectorAll('.ti');for(var i=0;i<old.length;i++)old[i].parentNode.removeChild(old[i]);"
    "for(var i=0;i<hs.length;i++){var h=hs[i],a=document.createElement('a');"
    "a.className='ti t'+h.tagName.charAt(1);a.innerText=h.innerText;a.href='#'+h.id;"
    "a.onclick=(function(id){return function(e){e.preventDefault?e.preventDefault():e.returnValue=false;var el=document.getElementById(id);if(el)el.scrollIntoView()}})(h.id);"
    "toc.appendChild(a)}initLinkTooltips()}"
    "function ttoc(){var toc=document.getElementById('mdv-toc');"
    "if(toc.className.indexOf('on')>=0){toc.className='';document.body.style.marginRight='0'}"
    "else{btoc();toc.className='on';document.body.style.marginRight='280px'}}"

    /* Link tooltip preview */
    "function mdvLinkTitleText(a){var href,raw;if(!a)return '';"
    "raw=a.getAttribute?a.getAttribute('href'):null;"
    "if(!raw)return '';"
    "if(raw.charAt&&raw.charAt(0)==='#')return raw;"
    "href=a.href||raw;"
    "return href||'';}"
    "function initLinkTooltips(){var as=document.getElementsByTagName('a'),i,a,t;"
    "for(i=0;i<as.length;i++){a=as[i];t=mdvLinkTitleText(a);if(!t)continue;"
    "if(typeof a._mdvOrigTitle==='undefined')a._mdvOrigTitle=a.getAttribute('title');"
    "a.title=t;}}"

    /* Find */
    "var fm=[],fi=-1;"
    "function mdvTextWalker(root){var nodes=[],stack=[root],n,i,ch;"
    "while(stack.length){n=stack.pop();if(!n)continue;"
    "if(n.nodeType===3){nodes.push(n);continue;}"
    "ch=n.childNodes;if(!ch)continue;for(i=ch.length-1;i>=0;i--)stack.push(ch[i]);}"
    "return{_nodes:nodes,_idx:-1,currentNode:null,nextNode:function(){this._idx++;if(this._idx>=this._nodes.length)return false;this.currentNode=this._nodes[this._idx];return true;}}}"
    "function cf(){var ms=document.querySelectorAll('.hl');for(var i=0;i<ms.length;i++){"
    "var m=ms[i],p=m.parentNode;p.replaceChild(document.createTextNode(m.innerText),m);p.normalize()}"
    "fm=[];fi=-1;document.getElementById('mdv-fc').innerText=''}"

    "function df(txt,mc,ww){cf();if(!txt)return;mc=mc?1:0;ww=ww?1:0;"
    "var ct=document.getElementById('mdv-ct');if(!ct)return;"
    "var w=document.createTreeWalker?document.createTreeWalker(ct,4,null,false):mdvTextWalker(ct),ns=[];"
    "var re=null;"
    "if(ww){"
    "  var esc=txt.replace(/[\\^$.*+?()[\\]{}|]/g,'\\\\$&');"
    "  re=new RegExp('\\\\b'+esc+'\\\\b',mc?'g':'gi');"
    "}"
    "while(w.nextNode()){var n=w.currentNode;"
    "if(n.parentNode&&n.parentNode.tagName==='SCRIPT')continue;"
    "if(!n.nodeValue)continue;"
    "if(re){if(re.test(n.nodeValue))ns.push(n);re.lastIndex=0;}"
    "else{var hay=mc?n.nodeValue:n.nodeValue.toLowerCase();"
    "var nee=mc?txt:txt.toLowerCase();if(hay.indexOf(nee)>=0)ns.push(n);}"
    "}"
    "for(var ni=0;ni<ns.length;ni++){var nd=ns[ni],v=nd.nodeValue,f=document.createDocumentFragment();"
    "if(re){"
    "  var m,li=0;re.lastIndex=0;"
    "  while((m=re.exec(v))!==null){"
    "    var s=m.index,e=s+m[0].length;"
    "    if(s>li)f.appendChild(document.createTextNode(v.substring(li,s)));"
    "    var sp=document.createElement('span');sp.className='hl';"
    "    sp.appendChild(document.createTextNode(v.substring(s,e)));f.appendChild(sp);li=e;"
    "    if(m[0].length===0)re.lastIndex++;"
    "  }"
    "  if(li<v.length)f.appendChild(document.createTextNode(v.substring(li)));"
    "}else{"
    "  var nee=mc?txt:txt.toLowerCase();"
    "  var idx=0,pos,vl=mc?v:v.toLowerCase();"
    "  while((pos=vl.indexOf(nee,idx))>=0){"
    "    if(pos>idx)f.appendChild(document.createTextNode(v.substring(idx,pos)));"
    "    var sp=document.createElement('span');sp.className='hl';"
    "    sp.appendChild(document.createTextNode(v.substring(pos,pos+txt.length)));f.appendChild(sp);idx=pos+txt.length;"
    "  }"
    "  if(idx<v.length)f.appendChild(document.createTextNode(v.substring(idx)));"
    "}"
    "nd.parentNode.replaceChild(f,nd);}"
    "fm=document.querySelectorAll('.hl');fi=fm.length>0?0:-1;ufh()}"

    "function ufh(){for(var i=0;i<fm.length;i++)fm[i].className='hl';"
    "if(fi>=0&&fi<fm.length){"
    "  fm[fi].className='hl hl-a';"
    "  var el=fm[fi], rect=el.getBoundingClientRect();"
    "  var fb=document.getElementById('mdv-fb');"
    "  var off=(fb&&fb.className.indexOf('on')>=0)?fb.offsetHeight+10:20;"
    "  if(rect.top<off||rect.bottom>window.innerHeight){"
    "    var st=window.pageYOffset||document.documentElement.scrollTop;"
    "    window.scrollTo(0,rect.top+st-off);"
    "  }"
    "}"
    "var c=document.getElementById('mdv-fc');"
    "if(fm.length>0)c.innerText=(fi+1)+' of '+fm.length;"
    "else c.innerText=document.getElementById('mdv-fi').value?'No matches':''}"

    "function fn(){if(fm.length===0)return;fi=(fi+1)%fm.length;ufh()}"
    "function fp(){if(fm.length===0)return;fi=(fi-1+fm.length)%fm.length;ufh()}"
    "function sf(){var b=document.getElementById('mdv-fb');b.className='on';"
    "var inp=document.getElementById('mdv-fi');inp.focus();inp.select()}"
    "function hf(){document.getElementById('mdv-fb').className='';cf()}"

    /* Help */
    "function th(){var h=document.getElementById('mdv-help');h.className=h.className==='on'?'':'on'}"

    /* Progress */
    "function up(){var b=document.getElementById('mdv-prog'),d=document.body,"
    "st=d.scrollTop||document.body.scrollTop,sh=d.scrollHeight-d.clientHeight;"
    "if(sh>0)b.style.width=(st/sh*100)+'%';else b.style.width='0'}"
    "window.onscroll=up;"

    /* Syntax highlighting — regex-based, applied once on load */
    "function shAll(){"
    "var pres=document.querySelectorAll('pre code[class]');"
    "for(var i=0;i<pres.length;i++){var el=pres[i],cls=el.className||'';"
    "var lang=cls.replace('language-','');"
    "if(!lang)continue;"
    "var h=el.innerHTML;"

    /* Comments */
    "if(lang==='html'||lang==='xml'){"
    "h=h.replace(/(&lt;!--[\\s\\S]*?--&gt;)/g,'<span class=\"sh-cm\">$1</span>');"
    "}else{"
    "h=h.replace(/((?:^|\\n)\\s*#[^\\n]*)/g,'<span class=\"sh-cm\">$1</span>');"  /* # comments for python/bash/ruby */
    "h=h.replace(/(\\/{2}[^\\n]*)/g,'<span class=\"sh-cm\">$1</span>');"          /* // comments */
    "h=h.replace(/(--[^\\n]*)/g,'<span class=\"sh-cm\">$1</span>');"              /* -- sql comments */
    "h=h.replace(/(\\/\\*[\\s\\S]*?\\*\\/)/g,'<span class=\"sh-cm\">$1</span>');" /* block comments */
    "}"

    /* Strings */
    "h=h.replace(/(&quot;(?:[^&]|&(?!quot;))*?&quot;)/g,'<span class=\"sh-str\">$1</span>');"
    "h=h.replace(/('(?:[^'\\\\]|\\\\.)*?')/g,'<span class=\"sh-str\">$1</span>');"
    "h=h.replace(/(`(?:[^`\\\\]|\\\\.)*?`)/g,'<span class=\"sh-str\">$1</span>');"

    /* Numbers */
    "h=h.replace(/\\b(\\d+\\.?\\d*(?:e[+-]?\\d+)?|0x[0-9a-fA-F]+)\\b/g,'<span class=\"sh-num\">$1</span>');"

    /* HTML/XML tags */
    "if(lang==='html'||lang==='xml'){"
    "h=h.replace(/(&lt;\\/?)([a-zA-Z][a-zA-Z0-9]*)/g,'$1<span class=\"sh-tag\">$2</span>');"
    "h=h.replace(/\\s([a-zA-Z-]+)(=)/g,' <span class=\"sh-attr\">$1</span>$2');"
    "}else{"

    /* Keywords per language family */
    "var kws='';"
    "if(lang==='js'||lang==='javascript'||lang==='typescript'||lang==='ts')"
    "kws='\\\\b(var|let|const|function|return|if|else|for|while|do|switch|case|break|continue|new|this|class|extends|import|export|from|default|try|catch|finally|throw|typeof|instanceof|async|await|yield|of|in|null|undefined|true|false)\\\\b';"
    "else if(lang==='python'||lang==='py')"
    "kws='\\\\b(def|class|return|if|elif|else|for|while|break|continue|import|from|as|try|except|finally|raise|with|yield|lambda|pass|and|or|not|in|is|None|True|False|self|print|global|nonlocal|assert|del)\\\\b';"
    "else if(lang==='c'||lang==='cpp'||lang==='csharp'||lang==='cs'||lang==='java'||lang==='rust'||lang==='go')"
    "kws='\\\\b(int|char|float|double|void|long|short|unsigned|signed|struct|enum|union|typedef|sizeof|static|extern|const|volatile|register|auto|return|if|else|for|while|do|switch|case|break|continue|goto|default|class|public|private|protected|virtual|override|new|delete|this|true|false|null|NULL|nullptr|fn|let|mut|pub|impl|use|mod|match|loop|async|await|func|package|import|var|type|interface|defer|range|select|chan)\\\\b';"
    "else if(lang==='sql')"
    "kws='\\\\b(SELECT|FROM|WHERE|INSERT|UPDATE|DELETE|CREATE|DROP|ALTER|TABLE|INDEX|JOIN|LEFT|RIGHT|INNER|OUTER|ON|AND|OR|NOT|IN|IS|NULL|AS|ORDER|BY|GROUP|HAVING|LIMIT|OFFSET|UNION|ALL|DISTINCT|INTO|VALUES|SET|BEGIN|COMMIT|ROLLBACK|EXISTS|BETWEEN|LIKE|COUNT|SUM|AVG|MAX|MIN|select|from|where|insert|update|delete|create|drop|alter|table|join|left|right|inner|outer|on|and|or|not|in|is|null|as|order|by|group|having|limit)\\\\b';"
    "else if(lang==='bash'||lang==='sh'||lang==='shell'||lang==='zsh')"
    "kws='\\\\b(if|then|else|elif|fi|for|while|do|done|case|esac|in|function|return|local|export|source|echo|exit|cd|ls|grep|sed|awk|cat|rm|mkdir|cp|mv|chmod|chown|sudo|apt|yum|pip|npm)\\\\b';"
    "else if(lang==='css'||lang==='scss'||lang==='less')"
    "kws='\\\\b(color|background|margin|padding|border|font|display|position|width|height|top|left|right|bottom|flex|grid|none|block|inline|relative|absolute|fixed|inherit|auto|important|solid|transparent)\\\\b';"
    "else if(lang==='php')"
    "kws='\\\\b(function|return|if|else|elseif|for|foreach|while|do|switch|case|break|continue|class|public|private|protected|static|new|echo|print|null|true|false|array|isset|empty|unset|require|include|use|namespace|try|catch|finally|throw|var)\\\\b';"
    "if(kws){var re=new RegExp(kws,'g');h=h.replace(re,'<span class=\"sh-kw\">$1</span>');}"

    /* Function calls: word followed by ( */
    "h=h.replace(/\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(/g,'<span class=\"sh-fn\">$1</span>(');"
    "}"

    "el.innerHTML=h;}}"

    /* Expand/collapse for long blocks */
    "function initCollapse(){"
    "var blocks=document.querySelectorAll('pre,blockquote');"
    "for(var i=0;i<blocks.length;i++){"
    "var b=blocks[i];if(b.scrollHeight>420){"
    "b.className=(b.className?b.className+' ':'')+'mdv-collapsible';"
    "var fade=document.createElement('div');fade.className='mdv-collapse-fade';b.appendChild(fade);"
    "var btn=document.createElement('button');btn.className='mdv-expand-btn';"
    "btn.innerText='\\u25BC Show more';btn.onclick=(function(bl,fd,bt){"
    "return function(){if(bl.className.indexOf('expanded')>=0){"
    "bl.className=bl.className.replace(' expanded','');bt.innerText='\\u25BC Show more';fd.style.display=''}"
    "else{bl.className+=' expanded';bt.innerText='\\u25B2 Show less';fd.style.display='none'}}"
    "})(b,fade,btn);"
    "b.parentNode.insertBefore(btn,b.nextSibling);"
    "}}}"

    /* Keyboard handler (backup — primary interception is via IE subclass) */
    "function mmTrim(s){return s?s.replace(/^\\s+|\\s+$/g,''):''}"
    "function mmNodeToken(s){var m=/^([A-Za-z0-9_:-]+)(.*)$/.exec(mmTrim(s)),mm;if(!m)return null;"
    "var id=m[1],rest=mmTrim(m[2]),text=id,shape='rect';"
    "if(rest){"
    "if((mm=/^\\[([^\\]]*)\\]$/.exec(rest)))text=mm[1];"
    "else if((mm=/^\\(\\[([^\\]]*)\\]\\)$/.exec(rest))){text=mm[1];shape='round'}"
    "else if((mm=/^\\(\\((.*)\\)\\)$/.exec(rest))){text=mm[1];shape='circle'}"
    "else if((mm=/^\\((.*)\\)$/.exec(rest))){text=mm[1];shape='round'}"
    "else if((mm=/^\\{([^}]*)\\}$/.exec(rest))){text=mm[1];shape='diamond'}"
    "else return null;}"
    "return{id:id,text:mmTrim(text)||id,shape:shape}}"
    "function mmEnsureNode(ctx,s){var p=mmNodeToken(s),n;if(!p)return null;"
    "n=ctx.map[p.id];if(!n){n={id:p.id,text:p.text,shape:p.shape,order:ctx.nodes.length,level:0};ctx.map[p.id]=n;ctx.nodes.push(n);}"
    "else{if(p.text&&p.text!==p.id)n.text=p.text;if(n.shape==='rect'&&p.shape!=='rect')n.shape=p.shape;}return n}"
    "function mmParseStmt(ctx,s){var m,left,right;"
    "s=mmTrim(s);if(!s)return;"
    "m=/^(.*?)\\s*(\\-\\.\\->|\\-\\->|\\-\\-\\-|\\=\\=>)(?:\\|([^|]+)\\|)?\\s*(.*)$/.exec(s);"
    "if(m){left=mmEnsureNode(ctx,m[1]);if(!left)return;right=mmEnsureNode(ctx,m[4]);if(!right)return;"
    "ctx.edges.push({from:left.id,to:right.id,kind:m[2],label:mmTrim(m[3]||'')});return;}"
    "mmEnsureNode(ctx,s)}"
    "function mmParse(src){"
    "var ctx={dir:'TD',nodes:[],edges:[],map:{}},lines,segs,m,i,j,seenHeader=0;"
    "/* Safety limit for Mermaid source length. */"
    "if(!src||src.length>8192)return null;"
    "lines=src.replace(/\\r/g,'').split('\\n');"
    "for(i=0;i<lines.length;i++){"
    "var line=mmTrim(lines[i]);if(!line||line.substr(0,2)==='%%')continue;"
    "if(!seenHeader){m=/^(graph|flowchart)\\s+(TD|TB|BT|LR|RL)\\b/i.exec(line);if(!m)return null;ctx.dir=m[2].toUpperCase();seenHeader=1;continue;}"
    "segs=line.split(';');for(j=0;j<segs.length;j++)mmParseStmt(ctx,segs[j]);"
    "/* Safety limit for Mermaid node count. */"
    "if(ctx.nodes.length>64)return null;"
    "/* Safety limit for Mermaid edge count. */"
    "if(ctx.edges.length>128)return null;}"
    "if(!seenHeader||ctx.nodes.length===0)return null;"
    "for(i=0;i<ctx.nodes.length;i++){var changed=0,e,from,to;"
    "for(j=0;j<ctx.edges.length;j++){e=ctx.edges[j];from=ctx.map[e.from];to=ctx.map[e.to];"
    "if(from&&to&&to.level<from.level+1){to.level=from.level+1;changed=1;}}"
    "if(!changed)break;}return ctx}"
    "function mmSvgEl(name){return document.createElementNS('http://www.w3.org/2000/svg',name)}"
    "function mmRender(block){"
    "var srcEl=block.querySelector('.mdv-mermaid-src'),view=block.querySelector('.mdv-mermaid-view'),ctx;"
    "if(!srcEl||!view)return;ctx=mmParse(srcEl.innerText||srcEl.textContent||'');if(!ctx)return;"
    "var isHorizontal=(ctx.dir==='LR'||ctx.dir==='RL'),isReverse=(ctx.dir==='BT'||ctx.dir==='RL');"
    "var levels={},rows=[],maxLevel=0,maxSpan=0,svg=mmSvgEl('svg'),defs=mmSvgEl('defs'),mk=mmSvgEl('marker');"
    "var baseW=96,baseH=54,charW=7,gapMinor=36,gapMajor=90,padX=28,padY=24;"
    "var i,j,row,key,rowSpan,canvasW,canvasH,major,n,start,shape,txt,line,sx,sy,ex,ey;"
    "for(i=0;i<ctx.nodes.length;i++){n=ctx.nodes[i];if(n.level>maxLevel)maxLevel=n.level;"
    "n.w=Math.max(baseW,Math.min(220,n.text.length*charW+34));n.h=baseH;"
    "if(n.shape==='circle'){n.w=Math.max(64,Math.min(100,n.text.length*charW+26));n.h=n.w;}"
    "if(n.shape==='diamond'){n.w=Math.max(110,Math.min(220,n.text.length*charW+40));n.h=64;}"
    "key=isReverse?(maxLevel-n.level):n.level;if(!levels[key])levels[key]=[];levels[key].push(n);}"
    "for(i=0;i<=maxLevel;i++){row=levels[i]||[];rows.push(row);rowSpan=0;for(j=0;j<row.length;j++){rowSpan+=row[j].w;}if(row.length>1)rowSpan+=gapMinor*(row.length-1);if(rowSpan>maxSpan)maxSpan=rowSpan;}"
    "if(isHorizontal){canvasW=padX*2+(maxLevel+1)*(baseW+gapMajor);canvasH=padY*2+Math.max(baseH,maxSpan);}"
    "else{canvasW=padX*2+Math.max(baseW,maxSpan);canvasH=padY*2+(maxLevel+1)*(baseH+gapMajor);}"
    "svg.setAttribute('viewBox','0 0 '+canvasW+' '+canvasH);svg.setAttribute('width',canvasW);svg.setAttribute('height',canvasH);svg.setAttribute('preserveAspectRatio','xMidYMin meet');"
    "mk.setAttribute('id','mdv-mm-arrow');mk.setAttribute('markerWidth','10');mk.setAttribute('markerHeight','10');mk.setAttribute('refX','8');mk.setAttribute('refY','3');mk.setAttribute('orient','auto');"
    "var path=mmSvgEl('path');path.setAttribute('d','M0,0 L0,6 L9,3 z');path.setAttribute('fill','currentColor');mk.appendChild(path);defs.appendChild(mk);svg.appendChild(defs);"
    "for(i=0;i<rows.length;i++){row=rows[i];rowSpan=0;for(j=0;j<row.length;j++)rowSpan+=row[j].w;if(row.length>1)rowSpan+=gapMinor*(row.length-1);start=((isHorizontal?canvasH:canvasW)-rowSpan)/2;"
    "major=(isHorizontal?padX:padY)+i*(isHorizontal?(baseW+gapMajor):(baseH+gapMajor));"
    "for(j=0;j<row.length;j++){n=row[j];if(isHorizontal){n.x=major+n.w/2;n.y=start+n.h/2;}else{n.x=start+n.w/2;n.y=major+n.h/2;}start+=n.w+gapMinor;}}"
    "for(i=0;i<ctx.edges.length;i++){var e=ctx.edges[i],from=ctx.map[e.from],to=ctx.map[e.to];if(!from||!to)continue;"
    "if(isHorizontal){sx=from.x+(ctx.dir==='RL'?-from.w/2:from.w/2);sy=from.y;ex=to.x+(ctx.dir==='RL'?to.w/2:-to.w/2);ey=to.y;}"
    "else{sx=from.x;sy=from.y+(ctx.dir==='BT'?-from.h/2:from.h/2);ex=to.x;ey=to.y+(ctx.dir==='BT'?to.h/2:-to.h/2);}"
    "line=mmSvgEl('line');line.setAttribute('x1',sx);line.setAttribute('y1',sy);line.setAttribute('x2',ex);line.setAttribute('y2',ey);"
    "line.setAttribute('class','mdv-mm-edge'+(e.kind==='-.->'?' dash':'')+(e.kind==='==>'?' bold':''));"
    "if(e.kind!=='---')line.setAttribute('marker-end','url(#mdv-mm-arrow)');svg.appendChild(line);}"
    "for(i=0;i<ctx.nodes.length;i++){n=ctx.nodes[i];shape=null;"
    "if(n.shape==='circle'){shape=mmSvgEl('ellipse');shape.setAttribute('cx',n.x);shape.setAttribute('cy',n.y);shape.setAttribute('rx',n.w/2);shape.setAttribute('ry',n.h/2);}"
    "else if(n.shape==='diamond'){shape=mmSvgEl('polygon');shape.setAttribute('points',(n.x)+','+(n.y-n.h/2)+' '+(n.x+n.w/2)+','+n.y+' '+n.x+','+(n.y+n.h/2)+' '+(n.x-n.w/2)+','+n.y);}"
    "else{shape=mmSvgEl('rect');shape.setAttribute('x',n.x-n.w/2);shape.setAttribute('y',n.y-n.h/2);shape.setAttribute('width',n.w);shape.setAttribute('height',n.h);shape.setAttribute('rx',n.shape==='round'?18:6);}"
    "shape.setAttribute('class','mdv-mm-node');svg.appendChild(shape);"
    "txt=mmSvgEl('text');txt.setAttribute('x',n.x);txt.setAttribute('y',n.y+4);txt.setAttribute('text-anchor','middle');txt.setAttribute('class','mdv-mm-text');txt.appendChild(document.createTextNode(n.text));svg.appendChild(txt);}"
    "view.innerHTML='';view.appendChild(svg);block.className+=(block.className?' ':'')+'ok'}"
    "function initMermaid(){var blocks=document.querySelectorAll('.mdv-mermaid[data-mdv-mermaid]');for(var i=0;i<blocks.length;i++)mmRender(blocks[i])}"
    "function mmApplySvgTextStyle(el){var cs,fz,ff;if(!el)return;cs=window.getComputedStyle?window.getComputedStyle(document.body,null):null;"
    "ff=cs&&cs.fontFamily?cs.fontFamily:document.body.style.fontFamily;fz=cs&&cs.fontSize?cs.fontSize:document.body.style.fontSize;"
    "if(ff){el.style.fontFamily=ff;try{el.setAttribute('font-family',ff);}catch(ex){}}"
    "if(fz){el.style.fontSize=fz;try{el.setAttribute('font-size',fz);}catch(ex){}}}"
    "function mmWrapWords(text,maxChars){var words,lines,line,i,w;if(!text)return[''];if(!(maxChars>4))maxChars=16;"
    "words=mmTrim(text).split(/\\s+/);lines=[];line='';for(i=0;i<words.length;i++){w=words[i];"
    "if(!line){line=w;continue;}if((line+' '+w).length<=maxChars)line+=' '+w;else{lines.push(line);line=w;}}"
    "if(line)lines.push(line);if(lines.length===0)lines.push(text);return lines;}"
    "function mmPushText(svg,x,y,lines,cls,anchor){"
    "var step=Math.max(16,Math.round(mmBodyFontPx()*1.25));"
    "for(var i=0;i<lines.length;i++){var t=mmSvgEl('text');t.setAttribute('x',x);t.setAttribute('y',y+i*step);t.setAttribute('text-anchor',anchor||'middle');t.setAttribute('class',cls);mmApplySvgTextStyle(t);t.appendChild(document.createTextNode(lines[i]));svg.appendChild(t);}}"
    "function mmSlotOffset(index,step){if(index<=0)return 0;var n=Math.floor((index+1)/2);return (index%2?1:-1)*n*step;}"
    "function mmParseFlow(src){"
    "var ctx={kind:'flow',dir:'TD',nodes:[],edges:[],map:{}},lines,segs,m,i,j,seenHeader=0;"
    "if(!src||src.length>8192)return null;lines=src.replace(/\\r/g,'').split('\\n');"
    "for(i=0;i<lines.length;i++){var line=mmTrim(lines[i]);if(!line||line.substr(0,2)==='%%')continue;"
    "if(!seenHeader){m=/^(graph|flowchart)\\s+(TD|TB|BT|LR|RL)\\b/i.exec(line);if(!m)return null;ctx.dir=m[2].toUpperCase();seenHeader=1;continue;}"
    "segs=line.split(';');for(j=0;j<segs.length;j++)mmParseStmt(ctx,segs[j]);if(ctx.nodes.length>64||ctx.edges.length>128)return null;}"
    "if(!seenHeader||ctx.nodes.length===0)return null;"
    "for(i=0;i<ctx.nodes.length;i++){var changed=0,e,from,to;for(j=0;j<ctx.edges.length;j++){e=ctx.edges[j];from=ctx.map[e.from];to=ctx.map[e.to];if(from&&to&&to.level<from.level+1){to.level=from.level+1;changed=1;}}if(!changed)break;}return ctx}"
    "function mmRenderFlow(view,ctx){"
    "var isHorizontal=(ctx.dir==='LR'||ctx.dir==='RL'),isReverse=(ctx.dir==='BT'||ctx.dir==='RL');"
    "var levels={},rows=[],maxLevel=0,maxSpan=0,svg=mmSvgEl('svg'),defs=mmSvgEl('defs'),mk=mmSvgEl('marker');"
    "var fontPx=mmBodyFontPx(),baseW=Math.max(96,Math.round(fontPx*6.0)),baseH=Math.max(54,Math.round(fontPx*2.9)),charW=fontPx*0.58,gapMinor=Math.max(36,Math.round(fontPx*2.0)),gapMajor=Math.max(90,Math.round(fontPx*4.8)),padX=Math.max(28,Math.round(fontPx*1.6)),padY=Math.max(24,Math.round(fontPx*1.4));"
    "var i,j,row,key,rowSpan,canvasW,canvasH,major,n,start,shape,txt,line,sx,sy,ex,ey;"
    "for(i=0;i<ctx.nodes.length;i++){n=ctx.nodes[i];if(n.level>maxLevel)maxLevel=n.level;n.w=Math.max(baseW,Math.min(320,Math.round(n.text.length*charW+fontPx*2.0)));n.h=baseH;"
    "if(n.shape==='circle'){n.w=Math.max(Math.round(fontPx*4.2),Math.min(Math.round(fontPx*6.0),Math.round(n.text.length*charW+fontPx*1.6)));n.h=n.w;}if(n.shape==='diamond'){n.w=Math.max(Math.round(fontPx*6.2),Math.min(320,Math.round(n.text.length*charW+fontPx*2.3)));n.h=Math.max(64,Math.round(fontPx*3.4));}"
    "key=isReverse?(maxLevel-n.level):n.level;if(!levels[key])levels[key]=[];levels[key].push(n);}"
    "for(i=0;i<=maxLevel;i++){row=levels[i]||[];rows.push(row);rowSpan=0;for(j=0;j<row.length;j++)rowSpan+=row[j].w;if(row.length>1)rowSpan+=gapMinor*(row.length-1);if(rowSpan>maxSpan)maxSpan=rowSpan;}"
    "if(isHorizontal){canvasW=padX*2+(maxLevel+1)*(baseW+gapMajor);canvasH=padY*2+Math.max(baseH,maxSpan);}else{canvasW=padX*2+Math.max(baseW,maxSpan);canvasH=padY*2+(maxLevel+1)*(baseH+gapMajor);}"
    "svg.setAttribute('viewBox','0 0 '+canvasW+' '+canvasH);svg.setAttribute('width',canvasW);svg.setAttribute('height',canvasH);"
    "mk.setAttribute('id','mdv-mm-arrow');mk.setAttribute('markerWidth','10');mk.setAttribute('markerHeight','10');mk.setAttribute('refX','8');mk.setAttribute('refY','3');mk.setAttribute('orient','auto');"
    "path=mmSvgEl('path');path.setAttribute('d','M0,0 L0,6 L9,3 z');path.setAttribute('fill','currentColor');mk.appendChild(path);defs.appendChild(mk);svg.appendChild(defs);"
    "for(i=0;i<rows.length;i++){row=rows[i];rowSpan=0;for(j=0;j<row.length;j++)rowSpan+=row[j].w;if(row.length>1)rowSpan+=gapMinor*(row.length-1);start=((isHorizontal?canvasH:canvasW)-rowSpan)/2;major=(isHorizontal?padX:padY)+i*(isHorizontal?(baseW+gapMajor):(baseH+gapMajor));"
    "for(j=0;j<row.length;j++){n=row[j];if(isHorizontal){n.x=major+n.w/2;n.y=start+n.h/2;}else{n.x=start+n.w/2;n.y=major+n.h/2;}start+=n.w+gapMinor;}}"
    "for(i=0;i<ctx.edges.length;i++){var e=ctx.edges[i],from=ctx.map[e.from],to=ctx.map[e.to];if(!from||!to)continue;"
    "if(isHorizontal){sx=from.x+(ctx.dir==='RL'?-from.w/2:from.w/2);sy=from.y;ex=to.x+(ctx.dir==='RL'?to.w/2:-to.w/2);ey=to.y;}else{sx=from.x;sy=from.y+(ctx.dir==='BT'?-from.h/2:from.h/2);ex=to.x;ey=to.y+(ctx.dir==='BT'?to.h/2:-to.h/2);}"
    "line=mmSvgEl('line');line.setAttribute('x1',sx);line.setAttribute('y1',sy);line.setAttribute('x2',ex);line.setAttribute('y2',ey);line.setAttribute('class','mdv-mm-edge'+(e.kind==='-.->'?' dash':'')+(e.kind==='==>'?' bold':''));if(e.kind!=='---')line.setAttribute('marker-end','url(#mdv-mm-arrow)');svg.appendChild(line);"
    "if(e.label)mmPushText(svg,(sx+ex)/2,(sy+ey)/2-8,[e.label],'mdv-mm-text');}"
    "for(i=0;i<ctx.nodes.length;i++){n=ctx.nodes[i];shape=null;if(n.shape==='circle'){shape=mmSvgEl('ellipse');shape.setAttribute('cx',n.x);shape.setAttribute('cy',n.y);shape.setAttribute('rx',n.w/2);shape.setAttribute('ry',n.h/2);}else if(n.shape==='diamond'){shape=mmSvgEl('polygon');shape.setAttribute('points',(n.x)+','+(n.y-n.h/2)+' '+(n.x+n.w/2)+','+n.y+' '+n.x+','+(n.y+n.h/2)+' '+(n.x-n.w/2)+','+n.y);}else{shape=mmSvgEl('rect');shape.setAttribute('x',n.x-n.w/2);shape.setAttribute('y',n.y-n.h/2);shape.setAttribute('width',n.w);shape.setAttribute('height',n.h);shape.setAttribute('rx',n.shape==='round'?18:6);}shape.setAttribute('class','mdv-mm-node');svg.appendChild(shape);txt=mmSvgEl('text');txt.setAttribute('x',n.x);txt.setAttribute('y',n.y+4);txt.setAttribute('text-anchor','middle');txt.setAttribute('class','mdv-mm-text');mmApplySvgTextStyle(txt);txt.appendChild(document.createTextNode(n.text));svg.appendChild(txt);}"
    "view.innerHTML='';view.appendChild(svg);return 1}"
    "function mmSeqActor(ctx,name,label){var a;if(!name)return null;a=ctx.map[name];if(!a){a={id:name,label:label||name,index:ctx.actors.length};ctx.map[name]=a;ctx.actors.push(a);}else if(label)a.label=label;return a}"
    "function mmParseSequence(src){"
    "var ctx={kind:'sequence',actors:[],map:{},items:[]},lines,m,i,line,a,b;"
    "if(!src||src.length>8192)return null;lines=src.replace(/\\r/g,'').split('\\n');"
    "for(i=0;i<lines.length;i++){line=mmTrim(lines[i]);if(!line||line.substr(0,2)==='%%')continue;if(i===0){if(!/^sequenceDiagram\\b/i.test(line))return null;continue;}"
    "if((m=/^(participant|actor)\\s+([A-Za-z0-9_:-]+)(?:\\s+as\\s+(.+))?$/i.exec(line))){mmSeqActor(ctx,m[2],mmTrim(m[3]||m[2]));continue;}"
    "if((m=/^Note\\s+(right|left)\\s+of\\s+([A-Za-z0-9_:-]+)\\s*:\\s*(.+)$/i.exec(line))){a=mmSeqActor(ctx,m[2],m[2]);ctx.items.push({type:'note',side:m[1].toLowerCase(),actor:a.id,text:mmTrim(m[3])});continue;}"
    "if((m=/^([A-Za-z0-9_:-]+?)\\s*(-->>|-->|->>|->|==>>|==>)\\s*([A-Za-z0-9_:-]+)\\s*:\\s*(.+)$/i.exec(line))){a=mmSeqActor(ctx,m[1],m[1]);b=mmSeqActor(ctx,m[3],m[3]);ctx.items.push({type:'msg',from:a.id,to:b.id,kind:m[2],text:mmTrim(m[4])});continue;}}"
    "if(ctx.actors.length===0||ctx.actors.length>16||ctx.items.length>96)return null;return ctx}"
    "function mmRenderSequence(view,ctx){"
    "var svg=mmSvgEl('svg'),defs=mmSvgEl('defs'),mk=mmSvgEl('marker'),path=mmSvgEl('path');"
    "var fontPx=mmBodyFontPx(),padX=Math.max(44,Math.round(fontPx*2.5)),topY=Math.max(34,Math.round(fontPx*1.9)),boxW=Math.max(120,Math.round(fontPx*7.2)),boxH=Math.max(44,Math.round(fontPx*2.7)),colGap=Math.max(96,Math.round(fontPx*5.8)),rowGap=Math.max(58,Math.round(fontPx*3.4)),bodyTop=topY+boxH+Math.max(26,Math.round(fontPx*1.6)),noteLane=Math.max(110,Math.round(fontPx*7.0));"
    "var width=padX*2+noteLane*2+ctx.actors.length*boxW+(ctx.actors.length-1)*colGap,height=bodyTop+ctx.items.length*rowGap+boxH+34;"
    "var i,a,x,y,line,msg,note,t,rect;"
    "svg.setAttribute('viewBox','0 0 '+width+' '+height);svg.setAttribute('width',width);svg.setAttribute('height',height);svg.setAttribute('preserveAspectRatio','xMidYMin meet');"
    "mk.setAttribute('id','mdv-mm-arrow');mk.setAttribute('markerWidth','10');mk.setAttribute('markerHeight','10');mk.setAttribute('refX','8');mk.setAttribute('refY','3');mk.setAttribute('orient','auto');path.setAttribute('d','M0,0 L0,6 L9,3 z');path.setAttribute('fill','currentColor');mk.appendChild(path);defs.appendChild(mk);svg.appendChild(defs);"
    "for(i=0;i<ctx.actors.length;i++){a=ctx.actors[i];a.cx=padX+noteLane+i*(boxW+colGap)+boxW/2;rect=mmSvgEl('rect');rect.setAttribute('x',a.cx-boxW/2);rect.setAttribute('y',topY);rect.setAttribute('width',boxW);rect.setAttribute('height',boxH);rect.setAttribute('rx','5');rect.setAttribute('class','mdv-mm-node');svg.appendChild(rect);mmPushText(svg,a.cx,topY+27,[a.label],'mdv-mm-text');line=mmSvgEl('line');line.setAttribute('x1',a.cx);line.setAttribute('y1',topY+boxH);line.setAttribute('x2',a.cx);line.setAttribute('y2',height-boxH-10);line.setAttribute('class','mdv-mm-life');svg.appendChild(line);}"
    "for(i=0;i<ctx.items.length;i++){y=bodyTop+i*rowGap;msg=ctx.items[i];if(msg.type==='msg'){var from=ctx.map[msg.from],to=ctx.map[msg.to];if(!from||!to)continue;line=mmSvgEl('line');line.setAttribute('x1',from.cx);line.setAttribute('y1',y);line.setAttribute('x2',to.cx);line.setAttribute('y2',y);line.setAttribute('class','mdv-mm-edge'+(msg.kind.indexOf('--')>=0?' dash':''));line.setAttribute('marker-end','url(#mdv-mm-arrow)');svg.appendChild(line);mmPushText(svg,(from.cx+to.cx)/2,y-Math.max(8,Math.round(fontPx*0.45)),[msg.text],'mdv-mm-text');}else if(msg.type==='note'){var noteLines,noteW,noteH,notePad=Math.max(10,Math.round(fontPx*0.6));a=ctx.map[msg.actor];if(!a)continue;noteLines=mmWrapWords(msg.text,Math.max(10,Math.round(fontPx*0.95)));noteW=Math.max(110,Math.round(fontPx*7.2));noteH=Math.max(40,notePad*2+noteLines.length*Math.max(16,Math.round(fontPx*1.25)));x=a.cx+(msg.side==='right'?Math.round(fontPx*4.0):-Math.round(fontPx*4.0));note=mmSvgEl('rect');note.setAttribute('x',x-(msg.side==='right'?0:noteW));note.setAttribute('y',y-Math.round(noteH/2));note.setAttribute('width',noteW);note.setAttribute('height',noteH);note.setAttribute('rx','3');note.setAttribute('class','mdv-mm-note');svg.appendChild(note);mmPushText(svg,msg.side==='right'?(x+notePad):(x-notePad),y-Math.round(noteH/2)+notePad+Math.max(12,Math.round(fontPx*0.8)),noteLines,'mdv-mm-note-text',msg.side==='right'?'start':'end');}}"
    "view.innerHTML='';view.appendChild(svg);return 1}"
    "function mmClassGet(ctx,name){var c=ctx.map[name];if(!c){c={id:name,title:name,members:[]};ctx.map[name]=c;ctx.classes.push(c);}return c}"
    "function mmParseClass(src){"
    "var ctx={kind:'class',classes:[],edges:[],map:{}},lines,i,line,m,inClass=null;"
    "if(!src||src.length>8192)return null;lines=src.replace(/\\r/g,'').split('\\n');"
    "for(i=0;i<lines.length;i++){line=mmTrim(lines[i]);if(!line||line.substr(0,2)==='%%')continue;if(i===0){if(!/^classDiagram\\b/i.test(line))return null;continue;}"
    "if(inClass&&line==='}'){inClass=null;continue;}if(inClass){inClass.members.push(line);continue;}"
    "if((m=/^class\\s+([A-Za-z0-9_:-]+)\\s*\\{$/.exec(line))){inClass=mmClassGet(ctx,m[1]);continue;}"
    "if((m=/^class\\s+([A-Za-z0-9_:-]+)$/.exec(line))){mmClassGet(ctx,m[1]);continue;}"
    "if((m=/^([A-Za-z0-9_:-]+)\\s*:\\s*(.+)$/.exec(line))){mmClassGet(ctx,m[1]).members.push(mmTrim(m[2]));continue;}"
    "if((m=/^([A-Za-z0-9_:-]+)\\s+([.<|o*\\->]+)\\s+([A-Za-z0-9_:-]+)(?:\\s*:\\s*(.+))?$/.exec(line))){mmClassGet(ctx,m[1]);mmClassGet(ctx,m[3]);ctx.edges.push({from:m[1],to:m[3],kind:m[2],label:mmTrim(m[4]||'')});continue;}}"
    "if(ctx.classes.length===0||ctx.classes.length>24||ctx.edges.length>64)return null;return ctx}"
    "function mmRenderClass(view,ctx){"
    "var fontPx=mmBodyFontPx(),cols=Math.min(3,Math.max(1,ctx.classes.length)),boxW=Math.max(190,Math.round(fontPx*11.0)),rowH=Math.max(128,Math.round(fontPx*7.4)),padX=Math.max(28,Math.round(fontPx*1.6)),padY=Math.max(26,Math.round(fontPx*1.5)),gapX=Math.max(38,Math.round(fontPx*2.2)),gapY=Math.max(34,Math.round(fontPx*2.0));"
    "var rows=Math.ceil(ctx.classes.length/cols),width=padX*2+cols*boxW+(cols-1)*gapX,height=padY*2+rows*rowH+(rows-1)*gapY;"
    "var svg=mmSvgEl('svg'),defs=mmSvgEl('defs'),mk=mmSvgEl('marker'),path=mmSvgEl('path'),i,c,r,x,y,rect,line,rowLabelSlots={},gapSlots={};"
    "svg.setAttribute('viewBox','0 0 '+width+' '+height);svg.setAttribute('width',width);svg.setAttribute('height',height);svg.setAttribute('preserveAspectRatio','xMidYMin meet');mk.setAttribute('id','mdv-mm-arrow');mk.setAttribute('markerWidth','10');mk.setAttribute('markerHeight','10');mk.setAttribute('refX','8');mk.setAttribute('refY','3');mk.setAttribute('orient','auto');path.setAttribute('d','M0,0 L0,6 L9,3 z');path.setAttribute('fill','currentColor');mk.appendChild(path);defs.appendChild(mk);svg.appendChild(defs);"
    "for(i=0;i<ctx.classes.length;i++){c=ctx.classes[i];r=Math.floor(i/cols);x=padX+(i%cols)*(boxW+gapX);y=padY+r*(rowH+gapY);c.row=r;c.col=(i%cols);c.x=x;c.y=y;c.w=boxW;c.cx=x+boxW/2;c.h=Math.max(Math.round(fontPx*4.0),Math.round(fontPx*4.0)+Math.min(5,c.members.length)*Math.round(fontPx*1.25));c.cy=y+c.h/2;rect=mmSvgEl('rect');rect.setAttribute('x',x);rect.setAttribute('y',y);rect.setAttribute('width',boxW);rect.setAttribute('height',c.h);rect.setAttribute('rx','6');rect.setAttribute('class','mdv-mm-node');svg.appendChild(rect);line=mmSvgEl('line');line.setAttribute('x1',x);line.setAttribute('y1',y+Math.round(fontPx*1.9));line.setAttribute('x2',x+boxW);line.setAttribute('y2',y+Math.round(fontPx*1.9));line.setAttribute('class','mdv-mm-edge');svg.appendChild(line);mmPushText(svg,c.cx,y+Math.round(fontPx*1.35),[c.title],'mdv-mm-text');for(r=0;r<Math.min(5,c.members.length);r++)mmPushText(svg,c.cx,y+Math.round(fontPx*3.15)+r*Math.round(fontPx*1.25),[c.members[r]],'mdv-mm-text');}"
    "for(i=0;i<ctx.edges.length;i++){var e=ctx.edges[i],from=ctx.map[e.from],to=ctx.map[e.to],dx,dy,x1,y1,x2,y2,lx,ly,midY,p,rowKey,rowSlot,gapKey,slotIndex,slotOffset;if(!from||!to)continue;dx=to.cx-from.cx;dy=to.cy-from.cy;"
    "if(from.row===to.row){rowKey='r'+from.row+'-'+(dx>=0?'f':'b');rowSlot=rowLabelSlots[rowKey]||0;rowLabelSlots[rowKey]=rowSlot+1;"
    "x1=from.cx+(dx>=0?from.w/2:-from.w/2);y1=from.cy;x2=to.cx-(dx>=0?to.w/2:-to.w/2);y2=to.cy;lx=(x1+x2)/2+((dx>=0)?1:-1)*rowSlot*Math.max(14,Math.round(fontPx*1.0));ly=(y1+y2)/2-Math.max(10,Math.round(fontPx*0.6))-rowSlot*Math.max(14,Math.round(fontPx*1.1));"
    "line=mmSvgEl('line');line.setAttribute('x1',x1);line.setAttribute('y1',y1);line.setAttribute('x2',x2);line.setAttribute('y2',y2);line.setAttribute('class','mdv-mm-edge'+(e.kind.indexOf('..')>=0?' dash':''));if(e.kind.indexOf('>')>=0||e.kind.indexOf('|>')>=0)line.setAttribute('marker-end','url(#mdv-mm-arrow)');svg.appendChild(line);if(e.label)mmPushText(svg,lx,ly,[e.label],'mdv-mm-text','middle');}"
    "else{x1=from.cx;y1=from.cy+(dy>=0?from.h/2:-from.h/2);x2=to.cx;y2=to.cy-(dy>=0?to.h/2:-to.h/2);gapKey=(Math.min(from.row,to.row))+'-'+(Math.max(from.row,to.row));slotIndex=gapSlots[gapKey]||0;gapSlots[gapKey]=slotIndex+1;slotOffset=mmSlotOffset(slotIndex,Math.max(14,Math.round(fontPx*1.0)));"
    "midY=(dy>=0)?Math.round(from.y+from.h+((to.y-(from.y+from.h))/2)+slotOffset):Math.round(to.y+to.h+((from.y-(to.y+to.h))/2)+slotOffset);"
    "p=mmSvgEl('path');p.setAttribute('d','M'+x1+','+y1+' L'+x1+','+midY+' L'+x2+','+midY+' L'+x2+','+y2);p.setAttribute('class','mdv-mm-edge'+(e.kind.indexOf('..')>=0?' dash':''));if(e.kind.indexOf('>')>=0||e.kind.indexOf('|>')>=0)p.setAttribute('marker-end','url(#mdv-mm-arrow)');svg.appendChild(p);"
    "if(e.label){lx=Math.round((x1+x2)/2);ly=midY-(slotOffset>=0?Math.max(10,Math.round(fontPx*0.6)):Math.max(14,Math.round(fontPx*0.85)));mmPushText(svg,lx,ly,[e.label],'mdv-mm-text','middle');}}}"
    "view.innerHTML='';view.appendChild(svg);return 1}"
    "function mmStateNode(ctx,name,label){var n,id=name;if(name==='[*]'){id='[*]#'+(++ctx.autoId);n={id:id,label:label||'',pseudo:1};ctx.map[id]=n;ctx.states.push(n);return n;}n=ctx.map[id];if(!n){n={id:id,label:label||id,pseudo:0};ctx.map[id]=n;ctx.states.push(n);}else if(label)n.label=label;return n}"
    "function mmParseState(src){"
    "var ctx={kind:'state',states:[],edges:[],map:{},autoId:0},lines,i,line,m,a,b;"
    "if(!src||src.length>8192)return null;lines=src.replace(/\\r/g,'').split('\\n');"
    "for(i=0;i<lines.length;i++){line=mmTrim(lines[i]);if(!line||line.substr(0,2)==='%%')continue;if(i===0){if(!/^stateDiagram(?:-v2)?\\b/i.test(line))return null;continue;}"
    "if((m=/^state\\s+\"([^\"]+)\"\\s+as\\s+([A-Za-z0-9_:-]+)$/i.exec(line))){mmStateNode(ctx,m[2],m[1]);continue;}"
    "if((m=/^(\\[\\*\\]|[A-Za-z0-9_:-]+)\\s*--?>\\s*(\\[\\*\\]|[A-Za-z0-9_:-]+)(?:\\s*:\\s*(.+))?$/i.exec(line))){a=mmStateNode(ctx,m[1],m[1]==='[*]'?'':m[1]);b=mmStateNode(ctx,m[2],m[2]==='[*]'?'':m[2]);ctx.edges.push({from:a.id,to:b.id,label:mmTrim(m[3]||'')});continue;}"
    "if((m=/^([A-Za-z0-9_:-]+)$/.exec(line))){mmStateNode(ctx,m[1],m[1]);continue;}}"
    "if(ctx.states.length===0||ctx.states.length>24||ctx.edges.length>64)return null;return ctx}"
    "function mmRenderState(view,ctx){"
    "var fontPx=mmBodyFontPx(),cols=(ctx.states.length>8?2:1),boxW=Math.max(140,Math.round(fontPx*8.4)),rowH=Math.max(90,Math.round(fontPx*5.4)),padX=Math.max(28,Math.round(fontPx*1.6)),padY=Math.max(24,Math.round(fontPx*1.4)),gapX=Math.max(42,Math.round(fontPx*2.5)),gapY=Math.max(34,Math.round(fontPx*2.0)),labelLane=Math.max(96,Math.round(fontPx*6.0));"
    "var rows=Math.ceil(ctx.states.length/cols),width=padX*2+labelLane*2+cols*boxW+(cols-1)*gapX,height=padY*2+rows*rowH+(rows-1)*gapY;"
    "var svg=mmSvgEl('svg'),defs=mmSvgEl('defs'),mk=mmSvgEl('marker'),path=mmSvgEl('path'),i,s,x,y,shape,line;"
    "svg.setAttribute('viewBox','0 0 '+width+' '+height);svg.setAttribute('width',width);svg.setAttribute('height',height);svg.setAttribute('preserveAspectRatio','xMidYMin meet');mk.setAttribute('id','mdv-mm-arrow');mk.setAttribute('markerWidth','10');mk.setAttribute('markerHeight','10');mk.setAttribute('refX','8');mk.setAttribute('refY','3');mk.setAttribute('orient','auto');path.setAttribute('d','M0,0 L0,6 L9,3 z');path.setAttribute('fill','currentColor');mk.appendChild(path);defs.appendChild(mk);svg.appendChild(defs);"
    "for(i=0;i<ctx.states.length;i++){var boxH=Math.max(52,Math.round(fontPx*3.1)),pr=Math.max(10,Math.round(fontPx*0.65));s=ctx.states[i];s.index=i;x=padX+labelLane+(i%cols)*(boxW+gapX);y=padY+Math.floor(i/cols)*(rowH+gapY);s.x=x;s.y=y;s.w=boxW;"
    "if(s.pseudo){s.r=pr;s.cx=x+boxW/2;s.cy=y+pr+2;shape=mmSvgEl('circle');shape.setAttribute('cx',s.cx);shape.setAttribute('cy',s.cy);shape.setAttribute('r',s.r);shape.setAttribute('class','mdv-mm-node');svg.appendChild(shape);}"
    "else{s.h=boxH;s.cx=x+boxW/2;s.cy=y+boxH/2;shape=mmSvgEl('rect');shape.setAttribute('x',x);shape.setAttribute('y',y);shape.setAttribute('width',boxW);shape.setAttribute('height',boxH);shape.setAttribute('rx',Math.max(16,Math.round(fontPx*1.0)));shape.setAttribute('class','mdv-mm-node');svg.appendChild(shape);mmPushText(svg,s.cx,y+Math.round(fontPx*1.85),[s.label],'mdv-mm-text');}}"
    "for(i=0;i<ctx.edges.length;i++){var e=ctx.edges[i],from=ctx.map[e.from],to=ctx.map[e.to],x1,y1,x2,y2,lx,ly,startX,startY,endX,endY,railX,p;if(!from||!to)continue;"
    "if(from.pseudo){x1=from.cx;y1=from.cy+from.r;}else{x1=from.cx;y1=from.y+from.h;}"
    "if(to.pseudo){x2=to.cx;y2=to.cy-to.r;}else{x2=to.cx;y2=to.y;}"
    "if(from.cx===to.cx&&from.index!=null&&to.index!=null&&to.index===from.index+1){"
    "line=mmSvgEl('line');line.setAttribute('x1',x1);line.setAttribute('y1',y1);line.setAttribute('x2',x2);line.setAttribute('y2',y2);line.setAttribute('class','mdv-mm-edge');line.setAttribute('marker-end','url(#mdv-mm-arrow)');svg.appendChild(line);"
    "if(e.label)mmPushText(svg,padX,y1+Math.round((y2-y1)/2)-Math.max(4,Math.round(fontPx*0.2)),[e.label],'mdv-mm-text','start');}"
    "else{startX=from.pseudo?(from.cx+from.r):(from.x+from.w);startY=from.pseudo?from.cy:from.cy;endX=to.pseudo?(to.cx+to.r):(to.x+to.w);endY=to.pseudo?to.cy:to.cy;railX=Math.max(startX,endX)+Math.max(38,Math.round(fontPx*2.4));"
    "p=mmSvgEl('path');p.setAttribute('d','M'+startX+','+startY+' L'+railX+','+startY+' L'+railX+','+endY+' L'+endX+','+endY);p.setAttribute('class','mdv-mm-edge');p.setAttribute('marker-end','url(#mdv-mm-arrow)');svg.appendChild(p);"
    "if(e.label)mmPushText(svg,railX+Math.max(18,Math.round(fontPx*1.1)),startY+Math.round((endY-startY)/2),[e.label],'mdv-mm-text','start');}}"
    "view.innerHTML='';view.appendChild(svg);return 1}"
    "function mmParse(src){"
    "var first;if(!src||src.length>8192)return null;first=mmTrim(src.replace(/\\r/g,'').split('\\n')[0]||'');"
    "if(/^sequenceDiagram\\b/i.test(first))return mmParseSequence(src);"
    "if(/^classDiagram\\b/i.test(first))return mmParseClass(src);"
    "if(/^stateDiagram(?:-v2)?\\b/i.test(first))return mmParseState(src);"
    "if(/^(graph|flowchart)\\b/i.test(first))return mmParseFlow(src);"
    "return null}"
    "function mmRender(block){"
    "var srcEl=block.querySelector('.mdv-mermaid-src'),view=block.querySelector('.mdv-mermaid-view'),ctx,ok=0;"
    "if(!srcEl||!view)return;ctx=mmParse(srcEl.innerText||srcEl.textContent||'');if(!ctx)return;"
    "if(ctx.kind==='flow')ok=mmRenderFlow(view,ctx);"
    "else if(ctx.kind==='sequence')ok=mmRenderSequence(view,ctx);"
    "else if(ctx.kind==='class')ok=mmRenderClass(view,ctx);"
    "else if(ctx.kind==='state')ok=mmRenderState(view,ctx);"
    "if(ok){block.className+=(block.className?' ':'')+'ok';syncMermaidTypography();if(window.setTimeout)window.setTimeout(syncMermaidTypography,0);}}"
    "function initMermaid(){var blocks=document.querySelectorAll('.mdv-mermaid[data-mdv-mermaid]');for(var i=0;i<blocks.length;i++)mmRender(blocks[i])}"
#if _WIN32_WINNT < 0x0600
    "function fitMermaidSvg(svg){return;}"
    "function syncMermaidTypography(){return;}"
    "function initMermaid(){return;}"
#endif
    "function pd(e){if(e.preventDefault)e.preventDefault();else e.returnValue=false}"
    "document.onkeydown=function(e){"
    "e=e||window.event;var c=e.ctrlKey||e.metaKey,k=e.keyCode;"
    "if(c&&(k===187||k===107)){pd(e);zi();return false}"
    "if(c&&(k===189||k===109)){pd(e);zo();return false}"
    "if(c&&(k===48||k===96)){pd(e);zr();return false}"
    "if(c&&k===68){pd(e);td();return false}"
    "if(c&&k===84){pd(e);ttoc();return false}"
    "if(c&&k===70){pd(e);sf();return false}"
    "if(c&&k===80){pd(e);window.print();return false}"
    "if(c&&k===71){pd(e);window.scrollTo(0,0);return false}"
    "if(c&&k===76){pd(e);tl();return false}"
    "if(c&&k===87){pd(e);if(e.shiftKey)cn();else cw();return false}"
    "if(k===112||(c&&k===191)){pd(e);th();return false}"
    "if(k===27){hf();document.getElementById('mdv-toc').className='';document.body.style.marginRight='0';"
    "document.getElementById('mdv-help').className='';return false}"
    "if(k===13&&document.activeElement===document.getElementById('mdv-fi')){"
    "pd(e);if(e.shiftKey)fp();else fn();return false}"
    "};"

    /* Init */
    "window.onresize=function(){syncMermaidTypography()};"
    "window.onload=function(){"
    "var inp=document.getElementById('mdv-fi');if(inp){"
    "var trig=function(){var s=inp;if(s._db)clearTimeout(s._db);"
    "s._db=setTimeout(function(){df(s.value,0,0)},200)};"
    "inp.onkeyup=trig;"
    "inp.oninput=trig;"
    "}"
    "initMermaid();shAll();initCollapse();initLinkTooltips();"
    "if(ln)tl();"  /* apply line numbers if saved */
    "ucc();"
    "up()};"
    "</script>");
}

/* ── HTML UI elements ────────────────────────────────────────────────── */

static const char* get_ui(void) {
    return
    "<div id=\"mdv-prog\"></div>"
    "<div id=\"mdv-fb\">"
    "<input id=\"mdv-fi\" type=\"text\" placeholder=\"Find in document...\" autocomplete=\"off\">"
    "<span id=\"mdv-fc\"></span>"
    "<button class=\"fb\" onclick=\"fp()\">&laquo;</button>"
    "<button class=\"fb\" onclick=\"fn()\">&raquo;</button>"
    "<button class=\"fb\" onclick=\"hf()\">&times;</button>"
    "</div>"
    "<div id=\"mdv-char\"></div>"
    "<div id=\"mdv-toc\"><div id=\"mdv-toc-t\">Table of Contents</div></div>"
    "<div id=\"mdv-toast\"></div>"
    "<div id=\"mdv-help\">"
    "<h3>MDView Keyboard Shortcuts</h3>"

    "<div class=\"hrow\"><span>Copy to Clipboard</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">C</span></span></div>"
    "<div class=\"hrow\"><span>Zoom in</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">+</span></span></div>"
    "<div class=\"hrow\"><span>Zoom out</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">&minus;</span></span></div>"
    "<div class=\"hrow\"><span>Reset zoom</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">0</span></span></div>"
    "<div class=\"help-sep\"></div>"

    "<div class=\"hrow\"><span>Widen columns</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">W</span></span></div>"
    "<div class=\"hrow\"><span>Narrow columns</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">Shift</span><span class=\"kc-plus\">+</span><span class=\"kc\">W</span></span></div>"
    "<div class=\"help-sep\"></div>"

    "<div class=\"hrow\"><span>Split view markdown + raw </span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">M</span></span></div>"
    "<div class=\"hrow\"><span>Toggle dark / light</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">D</span></span></div>"
    "<div class=\"hrow\"><span>Line numbers</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">L</span></span></div>"
    "<div class=\"help-sep\"></div>"

    "<div class=\"hrow\"><span>Table of Contents</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">T</span></span></div>"
    "<div class=\"hrow\"><span>Print</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">P</span></span></div>"
    "<div class=\"hrow\"><span>Go to top</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">G</span></span></div>"
    "<div class=\"help-sep\"></div>"

    "<div class=\"hrow\"><span>Find in page</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">F</span></span></div>"
    "<div class=\"hrow\"><span>Select all in active view</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">A</span></span></div>"
    "<div class=\"hrow\"><span>Continue Find forward</span><span class=\"hkeys\"><span class=\"kc\">F3</span></span></div>"
    "<div class=\"hrow\"><span>Continue Find backward</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">F3</span></span></div>"
    "<div class=\"help-sep\"></div>"

    "<div class=\"hrow\"><span>Close viewer</span><span class=\"hkeys\"><span class=\"kc\">Esc</span></span></div>"
    "<div class=\"hrow\"><span>This help</span><span class=\"hkeys\"><span class=\"kc\">F1</span></span></div>"
    "<div class=\"help-sep\"></div>"

    "<div class=\"help-foot\">MDView v3.0 &middot; Settings auto-saved &middot; Press Esc to close</div>"
    "</div>";
}

/* ── Lister search integration (Ctrl+F / F3 / Shift+F3) ─────────────── */

/* WLX SDK search flags (ListSearchText searchParameter) */
#ifndef LCS_FINDFIRST
    #define LCS_FINDFIRST   1
    #define LCS_MATCHCASE   2
    #define LCS_WHOLEWORDS  4
    #define LCS_BACKWARDS   8
#endif

static void js_find_apply(MDViewData* d, const wchar_t* needle, int matchCase, int wholeWords) {
    if (!d || !d->pBrowser) return;
    if (!needle) needle = L"";

    /*
       Push the search string into the find bar input, then call df(...).
       We escape backslashes and quotes to keep the JS string valid.
    */
    wchar_t esc[1024];
    size_t oi = 0;
    for (const wchar_t* p = needle; *p && oi + 2 < (sizeof(esc) / sizeof(esc[0])); ++p) {
        if (*p == L'\\' || *p == L'\'' ) {
            esc[oi++] = L'\\';
        }
        if (*p == L'\r') { esc[oi++] = L'\\'; esc[oi++] = L'r'; continue; }
        if (*p == L'\n') { esc[oi++] = L'\\'; esc[oi++] = L'n'; continue; }
        esc[oi++] = *p;
    }
    esc[oi] = 0;

    wchar_t js[1400];
    _snwprintf_s(js, _countof(js), _TRUNCATE,
        L"(function(){var i=document.getElementById('mdv-fi');"
        L"if(i){i.value='%s';}"
        L"if(typeof df==='function'){df('%s',%d,%d);}"
        L"})();",
        esc, esc, matchCase ? 1 : 0, wholeWords ? 1 : 0);

    exec_js(d->pBrowser, js);
}

static void js_find_step(MDViewData* d, int backwards) {
    if (!d || !d->pBrowser) return;
    exec_js(d->pBrowser, backwards ? L"fp()" : L"fn()" );
}

/* ── File Reading ────────────────────────────────────────────────────── */

static char* read_file_utf8(const char* fn) {
    FILE* f=fopen(fn,"rb"); if(!f)return NULL;
    if (fseek(f,0,SEEK_END) != 0) { fclose(f); return NULL; }
    long sz=ftell(f);
    if (sz < 0 || sz > MDVIEW_MAX_FILE_SIZE) { fclose(f); return NULL; }
    if (fseek(f,0,SEEK_SET) != 0) { fclose(f); return NULL; }
    char* buf=(char*)malloc((size_t)sz+1); if(!buf){fclose(f);return NULL;}
    size_t read = fread(buf,1,(size_t)sz,f);
    fclose(f);
    if (read != (size_t)sz) { free(buf); return NULL; }
    buf[sz]='\0';
    if(sz>=3&&(unsigned char)buf[0]==0xEF&&(unsigned char)buf[1]==0xBB&&(unsigned char)buf[2]==0xBF)
        memmove(buf,buf+3,sz-2);
    return buf;
}

/* ── WebBrowser Control ──────────────────────────────────────────────── */

#ifndef READYSTATE_LOADED
#define READYSTATE_LOADED      2
#define READYSTATE_INTERACTIVE 3
#define READYSTATE_COMPLETE    4
#endif

static HRESULT create_browser(HWND hwnd, IWebBrowser2** ppB, IOleObject** ppO, SiteImpl** ppS) {
    CLSID clsid; CLSIDFromString(L"{8856F961-340A-11D0-A96B-00C04FD705A2}",&clsid);
    IOleObject* pO=NULL;
    HRESULT hr=CoCreateInstance(&clsid,NULL,CLSCTX_INPROC_SERVER|CLSCTX_LOCAL_SERVER,&IID_IOleObject,(void**)&pO);
    if(FAILED(hr))return hr;
    SiteImpl* site=CreateSiteImpl(hwnd); *ppS=site;
    IOleObject_SetClientSite(pO,(IOleClientSite*)&site->clientSite);
    RECT rc; GetClientRect(hwnd,&rc);
    IOleObject_DoVerb(pO,OLEIVERB_INPLACEACTIVATE,NULL,(IOleClientSite*)&site->clientSite,0,hwnd,&rc);
    IWebBrowser2* pB=NULL;
    hr=IOleObject_QueryInterface(pO,&IID_IWebBrowser2,(void**)&pB);
    if(FAILED(hr)){IOleObject_Release(pO);return hr;}
    *ppB=pB; *ppO=pO; return S_OK;
}

static void navigate_to_html(IWebBrowser2* pB, const char* html) {
    VARIANT ve; VariantInit(&ve);
    BSTR url=SysAllocString(L"about:blank");
    IWebBrowser2_Navigate(pB,url,&ve,&ve,&ve,&ve); SysFreeString(url);

    READYSTATE rs; int to=100;
    do{ MSG msg; while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        IWebBrowser2_get_ReadyState(pB,&rs);
        if(rs!=READYSTATE_LOADED&&rs!=READYSTATE_INTERACTIVE&&rs!=READYSTATE_COMPLETE)Sleep(10);
    }while(rs!=READYSTATE_LOADED&&rs!=READYSTATE_INTERACTIVE&&rs!=READYSTATE_COMPLETE&&--to>0);

    IDispatch* pD=NULL; IWebBrowser2_get_Document(pB,&pD); if(!pD)return;
    IHTMLDocument2* pDoc=NULL; IDispatch_QueryInterface(pD,&IID_IHTMLDocument2,(void**)&pDoc); IDispatch_Release(pD);
    if(!pDoc)return;

    int wl=MultiByteToWideChar(CP_UTF8,0,html,-1,NULL,0);
    wchar_t* wh=(wchar_t*)malloc(wl*sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8,0,html,-1,wh,wl);
    BSTR bh=SysAllocString(wh); free(wh);

    SAFEARRAY* sa=SafeArrayCreateVector(VT_VARIANT,0,1);
    VARIANT* pv; SafeArrayAccessData(sa,(void**)&pv);
    pv->vt=VT_BSTR; pv->bstrVal=bh; SafeArrayUnaccessData(sa);
    IHTMLDocument2_write(pDoc,sa); IHTMLDocument2_close(pDoc);
    SafeArrayDestroy(sa); IHTMLDocument2_Release(pDoc);
}

/* ── Window Procedure ────────────────────────────────────────────────── */

static BOOL CALLBACK FindIEServerProc(HWND hwnd, LPARAM lParam) {
    wchar_t cls[64]; GetClassNameW(hwnd, cls, 64);
    if (wcscmp(cls, L"Internet Explorer_Server") == 0) { *(HWND*)lParam = hwnd; return FALSE; }
    return TRUE;
}

/* Read current JS state and save to INI */
static void save_current_settings(IWebBrowser2* pB) {
    if (!pB || !g_iniPath[0]) return;
    /* Read values from JS globals via execScript + document.title hack */
    /* We set document.title to a serialized string, then read it */
    exec_js(pB, L"document.title=''+fs+','+mw+','+ln+','+(document.body.className.indexOf('dark')>=0?1:0)");
    /* Now read the title back */
    IDispatch* pDisp = NULL;
    IWebBrowser2_get_Document(pB, &pDisp);
    if (!pDisp) return;
    IHTMLDocument2* pDoc = NULL;
    IDispatch_QueryInterface(pDisp, &IID_IHTMLDocument2, (void**)&pDoc);
    IDispatch_Release(pDisp);
    if (!pDoc) return;
    BSTR bTitle = NULL;
    IHTMLDocument2_get_title(pDoc, &bTitle);
    IHTMLDocument2_Release(pDoc);
    if (bTitle) {
        char title[128];
        WideCharToMultiByte(CP_UTF8, 0, bTitle, -1, title, sizeof(title), NULL, NULL);
        SysFreeString(bTitle);
        int rfs=19, rmw=0, rln=0, rdk=0;
        sscanf_s(title, "%d,%d,%d,%d", &rfs, &rmw, &rln, &rdk);
        save_setting_int("FontSize", rfs);
        save_setting_int("MaxWidth", rmw);
        save_setting_int("LineNumbers", rln);
        save_setting_int("DarkMode", rdk);
        save_setting_int("RawFontSize", g_settings.rawFontSize);
        save_setting_str("RawFontName", g_settings.rawFontName);
    }
}

static LRESULT CALLBACK ContainerWndProc(HWND hwnd, UINT msg, WPARAM wP, LPARAM lP) {
    MDViewData* d=(MDViewData*)mdview_get_window_ptr(hwnd, GWLP_USERDATA);
    switch(msg){
    case WM_SIZE:
        if (d && d->pBrowser) {
            layout_views(d);
        }
        return 0;
    case WM_SETFOCUS:
        if (d) {
            if (d->hwndIEServer) SetFocus(d->hwndIEServer);
            else if (d->hwndText) SetFocus(d->hwndText);
        }
        return 0;
    case WM_DESTROY:
        if(d){
            /* Save settings to INI before closing */
            save_current_settings(d->pBrowser);
            if(d->hwndIEServer && d->origIEProc){
        mdview_set_window_ptr(d->hwndIEServer, GWLP_WNDPROC, (LONG_PTR)d->origIEProc);
                RemovePropW(d->hwndIEServer, L"MDViewData");
            }
            if (d->hwndText && d->origTextProc) {
        mdview_set_window_ptr(d->hwndText, GWLP_WNDPROC, (LONG_PTR)d->origTextProc);
                RemovePropW(d->hwndText, L"MDViewData");
                DestroyWindow(d->hwndText);
                d->hwndText = NULL;
            }
            if (d->hTextFont && d->hTextFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT)) {
                DeleteObject(d->hTextFont);
            }
            d->hTextFont = NULL;
            if (d->mdUtf8) { free(d->mdUtf8); d->mdUtf8 = NULL; }
            if(d->pBrowser) IWebBrowser2_Release(d->pBrowser);
            if(d->pOleObj){ IOleObject_Close(d->pOleObj,OLECLOSE_NOSAVE); IOleObject_Release(d->pOleObj); }
    free(d); mdview_set_window_ptr(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wP,lP);
}

/* ── DLL Entry ───────────────────────────────────────────────────────── */

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID res) {
    if(reason==DLL_PROCESS_ATTACH){g_hInstance=hInst;DisableThreadLibraryCalls(hInst);}
    return TRUE;
}

/* Force IE11 edge mode for embedded WebBrowser control */
static void ensure_ie11_emulation(void) {
    static int done = 0;
    if (done) return; done = 1;
    wchar_t path[MAX_PATH]; GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t* exe = wcsrchr(path, L'\\'); exe = exe ? exe + 1 : path;
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Internet Explorer\\Main\\FeatureControl\\FEATURE_BROWSER_EMULATION",
            0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD val = 11001;
        RegSetValueExW(hKey, exe, 0, REG_DWORD, (LPBYTE)&val, sizeof(val));
        RegCloseKey(hKey);
    }
}

/* ── TC Lister Plugin Exports ────────────────────────────────────────── */

__declspec(dllexport) HWND __stdcall ListLoad(HWND pw, char* file, int flags) {
    ensure_ie11_emulation();
    load_settings();

    if(!g_classRegistered){
        WNDCLASSEXW wc={0}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=ContainerWndProc;
        wc.hInstance=g_hInstance; wc.lpszClassName=CLASS_NAME;
        RegisterClassExW(&wc); g_classRegistered=1;
    }

    char* md=read_file_utf8(file); if(!md)return NULL;
    char* body=md_to_html(md);
    if(!body){
        free(md);
        return NULL;
    }

    /* Determine theme: saved preference, or auto-detect */
    int dark = (g_settings.isDark >= 0) ? g_settings.isDark : is_dark_theme();

    /* Build CSS and JS dynamically with current settings */
    StrBuf cssBuf; sb_init(&cssBuf); build_css(&cssBuf);
    StrBuf jsBuf;  sb_init(&jsBuf);  build_js(&jsBuf);
    const char* ui = get_ui();

    size_t fl=strlen(body)+cssBuf.len+jsBuf.len+strlen(ui)+1024;
    char* full=(char*)malloc(fl);
    if (!full) {
        free(body);
        free(cssBuf.data);
        free(jsBuf.data);
        free(md);
        return NULL;
    }
    snprintf(full,fl,
        "<!DOCTYPE html><html%s><head>"
        "<meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">"
        "<meta charset=\"utf-8\"><style>%s</style></head><body%s>"
        "%s<div id=\"mdv-ct\">%s</div>%s</body></html>",
        dark?" style=\"background:#1e1e1e\"":"", cssBuf.data, dark?" class=\"dark\"":"", ui, body, jsBuf.data);
    free(body); free(cssBuf.data); free(jsBuf.data);

    RECT rc; GetClientRect(pw,&rc);
    HWND hwnd=CreateWindowExW(0,CLASS_NAME,L"MDView",
        WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,0,0,rc.right,rc.bottom,pw,NULL,g_hInstance,NULL);
    if(!hwnd){free(full); free(md); return NULL;}

    OleInitialize(NULL);
    MDViewData* data=(MDViewData*)calloc(1,sizeof(MDViewData));
    if (!data) {
        free(full);
        free(md);
        DestroyWindow(hwnd);
        return NULL;
    }
    data->hwndContainer = hwnd;
    data->rawCharCount = utf8_char_count(md);
    data->mdUtf8 = md;
    mdview_set_window_ptr(hwnd, GWLP_USERDATA, (LONG_PTR)data);

    SiteImpl* site=NULL;
    HRESULT hr=create_browser(hwnd,&data->pBrowser,&data->pOleObj,&site);
    if(FAILED(hr)){
        if (data->mdUtf8) free(data->mdUtf8);
        free(data);
        free(full);
        DestroyWindow(hwnd);
        return NULL;
    }

    layout_views(data);
    IWebBrowser2_put_Silent(data->pBrowser, VARIANT_TRUE);
    navigate_to_html(data->pBrowser,full); free(full);

    IOleObject_DoVerb(data->pOleObj, OLEIVERB_UIACTIVATE, NULL,
                      (IOleClientSite*)&site->clientSite, 0, hwnd, &rc);

    /* Subclass the IE Server window for hotkeys */
    {
        HWND ieWnd = NULL;
        EnumChildWindows(hwnd, FindIEServerProc, (LPARAM)&ieWnd);
        if (ieWnd) {
            data->hwndIEServer = ieWnd;
            SetPropW(ieWnd, L"MDViewData", (HANDLE)data);
            data->origIEProc = (WNDPROC)mdview_set_window_ptr(ieWnd, GWLP_WNDPROC, (LONG_PTR)IEServerSubclassProc);
            SetFocus(ieWnd);
        }
    }

    return hwnd;
}

__declspec(dllexport) HWND __stdcall ListLoadW(HWND pw, WCHAR* file, int flags) {
    if (!file) return NULL;

    int len=WideCharToMultiByte(CP_UTF8,0,file,-1,NULL,0,NULL,NULL);
    if (len <= 0) return NULL;

    char* u=(char*)malloc((size_t)len);
    if (!u) return NULL;

    if (WideCharToMultiByte(CP_UTF8,0,file,-1,u,len,NULL,NULL) <= 0) {
        free(u);
        return NULL;
    }

    HWND r=ListLoad(pw,u,flags);
    free(u);
    return r;
}

__declspec(dllexport) void __stdcall ListCloseWindow(HWND w) { DestroyWindow(w); }

__declspec(dllexport) void __stdcall ListGetDetectString(char* ds, int mx) {
    strncpy_s(ds, (size_t)mx, "EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MKD\" | EXT=\"MKDN\"", _TRUNCATE);
}

/*
   Total Commander calls this for Ctrl+F and F3/Shift+F3 text search.
   If we return LISTPLUGIN_ERROR here, TC shows "Not found" immediately.
   We map TC's search calls to the internal JS highlighter-based search.
*/
__declspec(dllexport) int __stdcall ListSearchText(HWND w, char* searchString, int searchParameter) {
    MDViewData* d = (MDViewData*)mdview_get_window_ptr(w, GWLP_USERDATA);
    if (!d || !d->pBrowser) return LISTPLUGIN_ERROR;

    /* Convert ANSI (system codepage) to UTF-16 */
    wchar_t* ws = NULL;
    if (searchString && searchString[0]) {
        int wl = MultiByteToWideChar(CP_ACP, 0, searchString, -1, NULL, 0);
        if (wl > 0) {
            ws = (wchar_t*)calloc((size_t)wl, sizeof(wchar_t));
            if (ws) MultiByteToWideChar(CP_ACP, 0, searchString, -1, ws, wl);
        }
    }

    int findFirst  = (searchParameter & LCS_FINDFIRST) ? 1 : 0;
    int matchCase  = (searchParameter & LCS_MATCHCASE) ? 1 : 0;
    int wholeWords = (searchParameter & LCS_WHOLEWORDS) ? 1 : 0;
    int backwards  = (searchParameter & LCS_BACKWARDS) ? 1 : 0;

    /* If a string is provided, apply the search. For "find next", TC may pass NULL. */
    if (ws && ws[0]) {
        /* Always re-apply on find-first, otherwise apply once then step */
        js_find_apply(d, ws, matchCase, wholeWords);
        if (!findFirst) {
            js_find_step(d, backwards);
        }
        free(ws);
        return LISTPLUGIN_OK;
    }

    /* No string: treat as "find next/prev" */
    js_find_step(d, backwards);
    if (ws) free(ws);
    return LISTPLUGIN_OK;
}

__declspec(dllexport) int __stdcall ListSearchTextW(HWND w, WCHAR* searchString, int searchParameter) {
    MDViewData* d = (MDViewData*)mdview_get_window_ptr(w, GWLP_USERDATA);
    if (!d || !d->pBrowser) return LISTPLUGIN_ERROR;

    int findFirst  = (searchParameter & LCS_FINDFIRST) ? 1 : 0;
    int matchCase  = (searchParameter & LCS_MATCHCASE) ? 1 : 0;
    int wholeWords = (searchParameter & LCS_WHOLEWORDS) ? 1 : 0;
    int backwards  = (searchParameter & LCS_BACKWARDS) ? 1 : 0;

    if (searchString && searchString[0]) {
        js_find_apply(d, searchString, matchCase, wholeWords);
        if (!findFirst) {
            js_find_step(d, backwards);
        }
        return LISTPLUGIN_OK;
    }

    js_find_step(d, backwards);
    return LISTPLUGIN_OK;
}
__declspec(dllexport) int __stdcall ListSendCommand(HWND w, int c, int p) { return LISTPLUGIN_OK; }

__declspec(dllexport) void __stdcall ListSetDefaultParams(ListDefaultParamStruct* p) {
    if (p && p->DefaultIniName[0]) {
        strncpy_s(g_iniPath, MAX_PATH, p->DefaultIniName, _TRUNCATE);
    }
}
