/*
 * MDView - Total Commander Lister Plugin for Markdown
 * =========================================================
 * Lightweight WLX plugin: built-in Markdown->HTML, embedded MSHTML, zero deps.
 *
 * Hotkeys:
 *   Ctrl+Plus/Minus/0  Zoom in / out / reset
 *   Ctrl+MouseWheel    Zoom in / out
 *   Ctrl+D             Toggle dark/light mode
 *   Ctrl+T             Toggle Table of Contents sidebar
 *   Ctrl+F             Find in page with highlighting
 *   Ctrl+P             Print document
 *   Ctrl+G             Go to top
 *   Ctrl+A             Select all text in the active view
 *   Ctrl+C             Copy text to clipboard (HTML if focus is in rendered view, raw markdown if focus is in text view)
 *   Escape             Close find bar / TOC / help
 *   Ctrl+M             Toggle split view (Markdown render + raw text side by side)
 *   Ctrl+1 / Ctrl+2    Preview / Code
 *   Ctrl+W             Toggle source word wrap
 *   Ctrl+Y             Sync split-view panes around the current visible location
 *   F1                 Show keyboard shortcuts help
 *   CTRL-F             Find in page (with highlighting)
 *   Shift+F3           Find in page (reverse)
 *   F3				    Find next
 *   F7                 Open find / find next when a query already exists
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
#include <ole2.h>
#include <exdisp.h>
#include <mshtml.h>
#include <mshtmhst.h>
#include <docobj.h>
#include <shellapi.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "resource.h"

/* --- TC Lister Plugin Interface --- */

#define LISTPLUGIN_OK    0
#define LISTPLUGIN_ERROR 1
#define LCP_DARKMODE      128

typedef struct {
    int   size;
    DWORD PluginInterfaceVersionLow;
    DWORD PluginInterfaceVersionHi;
    char  DefaultIniName[MAX_PATH];
} ListDefaultParamStruct;

typedef struct MDViewData MDViewData;

static LRESULT CALLBACK ContainerWndProc(HWND, UINT, WPARAM, LPARAM);
static char* read_file_utf8(const char*);
static char* md_to_html(const char*, const char*);
static char* md_to_raw_html(const char*);
static int   resolve_color_theme(int);
static void  toggle_color_theme(MDViewData*);
static void  navigate_to_html(IWebBrowser2*, const char*);
static LONG_PTR mdview_set_window_ptr(HWND, int, LONG_PTR);
static LONG_PTR mdview_get_window_ptr(HWND, int);
static void exec_js(IWebBrowser2*, const wchar_t*);
static int get_document_title_utf8(IWebBrowser2*, char*, int);
static wchar_t* utf8_to_wide_dup(const char*);
static int set_clipboard_text_wide(HWND, const wchar_t*);
static wchar_t* get_element_text_by_id(MDViewData*, const wchar_t*);
static void sync_panes_here(MDViewData*);
static void sync_html_point(MDViewData*, int, int);

static const wchar_t CLASS_NAME[] = L"MDViewWLXContainer";
static HINSTANCE g_hInstance = NULL;
static int g_classRegistered = 0;

typedef enum { VIEW_PREVIEW, VIEW_CODE, VIEW_SPLIT } ViewMode;

struct MDViewData {
    IWebBrowser2* pBrowser;
    IOleObject*   pOleObj;
    HWND          hwndIEServer;  /* The actual IE rendering window */
    WNDPROC       origIEProc;    /* Original wndproc of IE Server */
    HWND          hwndContainer; /* Our container window */
    int           darkTheme;
    int           listerFlags;
    DWORD         themeCheck;
    unsigned      paletteRevision;
    ViewMode      viewMode;
    int           ctrlWheelRemainder; /* Accumulates high-resolution Ctrl+wheel zoom deltas */
    size_t        rawCharCount;  /* UTF-8 codepoint count for raw markdown */
    char*         mdUtf8;        /* raw markdown (UTF-8), owned */
    char*         currentFile;   /* current file path (UTF-8), owned */
};

static int load_file_into_existing_view(MDViewData*, const char*);
static void js_find_apply(MDViewData*, const wchar_t*, int, int);

#define MDVIEW_SYNC_TIMER_ID 1
#define MDVIEW_SYNC_TIMER_MS 120

/* --- INI Settings Persistence --- */

#define MDVIEW_ZOOM_MIN 9
#define MDVIEW_ZOOM_DEFAULT 19
#define MDVIEW_ZOOM_MAX (MDVIEW_ZOOM_DEFAULT * 5)
#define MDVIEW_ZOOM_STEP 2

static char g_iniPath[MAX_PATH] = { 0 };
static int g_hasListerHost;

typedef struct {
    int fontSize;    /* Document font size in pixels */
    int colorTheme;  /* 0 = Windows, 1 = light, 2 = dark, 3 = Total Commander */
    int maxWidth;    /* 0 = fit to window */
    int lineNums;    /* 0 or 1 */
    int rawFontSize; /* raw text font size */
    wchar_t rawFontName[64]; /* raw text font name */
    wchar_t themeFile[2][MAX_PATH];
    int wordWrap;
} MDVSettings;

static MDVSettings g_settings = { MDVIEW_ZOOM_DEFAULT, 0, 0, 0, MDVIEW_RAW_FONT_PT, MDVIEW_RAW_FONT_NAME, {{0}}, 0 };

static unsigned g_paletteRevision;

static void load_settings(void) {
    if (!g_iniPath[0]) return;
    g_settings.fontSize = GetPrivateProfileIntA("MDView", "FontSize", MDVIEW_ZOOM_DEFAULT, g_iniPath);
    g_settings.colorTheme = GetPrivateProfileIntA("MDView", "colorTheme", -1, g_iniPath);
    if (g_settings.colorTheme == -1) {
        int legacy=GetPrivateProfileIntA("MDView","DarkMode",-1,g_iniPath);
        g_settings.colorTheme=legacy==0 ? 1 : legacy==1 ? 2 : 0;
    }
    if (g_settings.colorTheme<0 || g_settings.colorTheme>3) g_settings.colorTheme=0;
    wchar_t ini[MAX_PATH], name[MAX_PATH];
    MultiByteToWideChar(CP_ACP,0,g_iniPath,-1,ini,MAX_PATH);
    for (int dark=0;dark<2;++dark) {
        GetPrivateProfileStringW(L"MDView",dark?L"darkTheme":L"lightTheme",L"",name,MAX_PATH,ini);
        if (wcscmp(name,g_settings.themeFile[dark])!=0) {
            wcscpy_s(g_settings.themeFile[dark],MAX_PATH,name);
            ++g_paletteRevision;
        }
    }
    g_settings.maxWidth = 0;
    g_settings.lineNums = GetPrivateProfileIntA("MDView", "LineNumbers", 0, g_iniPath);
    g_settings.wordWrap = GetPrivateProfileIntA("MDView","WordWrap",0,g_iniPath)!=0;
    g_settings.rawFontSize = GetPrivateProfileIntA("MDView", "RawFontSize", MDVIEW_RAW_FONT_PT, g_iniPath);
    char fontNameA[64];
    GetPrivateProfileStringA("MDView", "RawFontName", MDVIEW_RAW_FONT_NAME_A, fontNameA, sizeof(fontNameA), g_iniPath);
    MultiByteToWideChar(CP_ACP, 0, fontNameA, -1, g_settings.rawFontName, 64);

    /* Clamp */
    if (g_settings.fontSize < MDVIEW_ZOOM_MIN) g_settings.fontSize = MDVIEW_ZOOM_MIN;
    if (g_settings.fontSize > MDVIEW_ZOOM_MAX) g_settings.fontSize = MDVIEW_ZOOM_MAX;
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

/* --- OLE command helper (copy/paste) --- */

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
    char title[64];
    if (!d) return;
    if (d->pBrowser) {
        exec_js(d->pBrowser,
            L"(function(){var t='',e;if(window.mdvUsingSource&&mdvUsingSource())t=mdvRawSelectedText();"
            L"if(t){e=document.getElementById('mdv-raw-copy-buffer');if(!e){e=document.createElement('div');e.id='mdv-raw-copy-buffer';"
            L"e.style.position='absolute';e.style.left='-99999px';e.style.top='-99999px';e.style.whiteSpace='pre';document.body.appendChild(e);}e.innerText=t;document.title='MDVRAWCOPY:1';}"
            L"else document.title='MDVRAWCOPY:0';})();");
        if (get_document_title_utf8(d->pBrowser, title, (int)sizeof(title)) &&
            strcmp(title, "MDVRAWCOPY:1") == 0) {
            wchar_t* text = get_element_text_by_id(d, L"mdv-raw-copy-buffer");
            exec_js(d->pBrowser, L"document.title='MDView';");
            if (text) {
                set_clipboard_text_wide(d->hwndContainer, text);
                free(text);
                return;
            }
        }
        exec_js(d->pBrowser, L"document.title='MDView';if(window.mdvCopyActive)mdvCopyActive();else document.execCommand&&document.execCommand('copy');");
    }
}

static wchar_t* get_element_text_by_id(MDViewData* d, const wchar_t* id) {
    IDispatch* pDisp = NULL;
    IHTMLDocument2* pDoc2 = NULL;
    IHTMLDocument3* pDoc3 = NULL;
    IHTMLElement* pEl = NULL;
    BSTR bId = NULL;
    BSTR bText = NULL;
    wchar_t* out = NULL;

    if (!d || !d->pBrowser || !id) return NULL;

    IWebBrowser2_get_Document(d->pBrowser, &pDisp);
    if (!pDisp) return NULL;

    IDispatch_QueryInterface(pDisp, &IID_IHTMLDocument2, (void**)&pDoc2);
    IDispatch_Release(pDisp);
    if (!pDoc2) return NULL;

    IHTMLDocument2_QueryInterface(pDoc2, &IID_IHTMLDocument3, (void**)&pDoc3);
    IHTMLDocument2_Release(pDoc2);
    if (!pDoc3) return NULL;

    bId = SysAllocString(id);
    if (!bId) {
        IHTMLDocument3_Release(pDoc3);
        return NULL;
    }

    IHTMLDocument3_getElementById(pDoc3, bId, &pEl);
    SysFreeString(bId);
    IHTMLDocument3_Release(pDoc3);
    if (!pEl) return NULL;

    IHTMLElement_get_innerText(pEl, &bText);
    IHTMLElement_Release(pEl);
    if (!bText) return NULL;

    out = _wcsdup(bText);
    SysFreeString(bText);
    return out;
}

static wchar_t* get_rendered_content_text(MDViewData* d) {
    return get_element_text_by_id(d, L"mdv-ct");
}

static void do_copy_text(MDViewData* d) {
    wchar_t* text;

    if (!d) return;

    text = d->viewMode==VIEW_CODE ? NULL : get_rendered_content_text(d);
    if (!text && d->mdUtf8) {
        text = utf8_to_wide_dup(d->mdUtf8);
    }
    if (!text) return;

    set_clipboard_text_wide(d->hwndContainer, text);
    free(text);
}

/* Ctrl+A behaviour:
   - if focus is in raw view: select all raw markdown
   - if focus is in rendered view: select all rendered content */
static void do_select_all(MDViewData* d) {
    if (!d) return;
    if (d->pBrowser) {
        exec_js(d->pBrowser,
            L"if(window.mdvSelectActive)mdvSelectActive();");
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
    IDM_HELP,
    IDM_COPY,
    IDM_PASTE,
    IDM_TOGGLE_SPLIT,
    IDM_SYNC_PANES,
    IDM_CLOSE_SEARCH,
    IDM_VIEW_PREVIEW,
    IDM_VIEW_CODE,
    IDM_WORD_WRAP
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

#define IDM_COLOR_THEME 2000
#define IDM_PALETTE 2010
#define MDV_MAX_PALETTES 96
typedef struct { wchar_t name[MAX_PATH]; int dark; } ThemeChoice;
static int append_theme_menu(HMENU, ThemeChoice*);
static void apply_theme_command(MDViewData*, int, const ThemeChoice*, int);

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

static char* wide_to_utf8_dup(const wchar_t* ws) {
    char* s;
    int len;

    if (!ws) return NULL;
    len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    s = (char*)malloc((size_t)len);
    if (!s) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, ws, -1, s, len, NULL, NULL) <= 0) {
        free(s);
        return NULL;
    }
    return s;
}

static int set_clipboard_text_wide(HWND owner, const wchar_t* text) {
    size_t chars;
    HGLOBAL hMem;
    wchar_t* dst;

    if (!text) text = L"";
    chars = wcslen(text) + 1;
    hMem = GlobalAlloc(GMEM_MOVEABLE, chars * sizeof(wchar_t));
    if (!hMem) return 0;

    dst = (wchar_t*)GlobalLock(hMem);
    if (!dst) {
        GlobalFree(hMem);
        return 0;
    }
    memcpy(dst, text, chars * sizeof(wchar_t));
    GlobalUnlock(hMem);

    if (!OpenClipboard(owner)) {
        GlobalFree(hMem);
        return 0;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
        CloseClipboard();
        GlobalFree(hMem);
        return 0;
    }
    CloseClipboard();
    return 1;
}

static char* mdview_strdup(const char* s) {
    size_t n;
    char* out;
    if (!s) return NULL;
    n = strlen(s) + 1;
    out = (char*)malloc(n);
    if (!out) return NULL;
    memcpy(out, s, n);
    return out;
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

static int hex_value(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static char* url_percent_decode_dup(const char* s, size_t len) {
    size_t i, outLen = 0;
    char* out;
    if (!s) return NULL;
    out = (char*)malloc(len + 1);
    if (!out) return NULL;
    for (i = 0; i < len; ++i) {
        if (s[i] == '%' && i + 2 < len) {
            int hi = hex_value((unsigned char)s[i + 1]);
            int lo = hex_value((unsigned char)s[i + 2]);
            if (hi >= 0 && lo >= 0 && (hi || lo)) {
                out[outLen++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out[outLen++] = s[i];
    }
    out[outLen] = '\0';
    return out;
}

static char* file_url_to_path_utf8_dup(const char* url) {
    const char* p;
    char* decoded;
    size_t i;

    if (!url || _strnicmp(url, "file://", 7) != 0) return NULL;
    p = url + 7;
    decoded = url_percent_decode_dup(p,strcspn(p,"?#"));
    if (!decoded) return NULL;

    if (decoded[0] == '/' &&
        ((decoded[1] >= 'A' && decoded[1] <= 'Z') || (decoded[1] >= 'a' && decoded[1] <= 'z')) &&
        decoded[2] == ':') {
        memmove(decoded, decoded + 1, strlen(decoded));
    }

    for (i = 0; decoded[i]; ++i) {
        if (decoded[i] == '/') decoded[i] = '\\';
    }
    return decoded;
}

static int is_markdown_file_path(const char* path) {
    const char* dot;
    if (!path) return 0;
    dot = strrchr(path, '.');
    if (!dot) return 0;
    return _stricmp(dot, ".md") == 0 ||
           _stricmp(dot, ".markdown") == 0 ||
           _stricmp(dot, ".mkd") == 0 ||
           _stricmp(dot, ".mkdn") == 0;
}

static void handle_pending_link_command(MDViewData* d) {
    char title[4096];
    char* decoded = NULL;
    char* localPath = NULL;

    if (!d || !d->pBrowser) return;
    if (!get_document_title_utf8(d->pBrowser, title, (int)sizeof(title))) return;
    if (strncmp(title, "MDVLINK:", 8) != 0) return;

    decoded = url_percent_decode_dup(title + 8,strlen(title + 8));
    if (!decoded) goto clear;

    if (_strnicmp(decoded, "file://", 7) == 0) {
        localPath = file_url_to_path_utf8_dup(decoded);
        if (localPath && is_markdown_file_path(localPath)) {
            load_file_into_existing_view(d, localPath);
        } else {
            wchar_t* w = utf8_to_wide_dup(localPath ? localPath : decoded);
            if (w) {
                ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
                free(w);
            }
        }
    } else {
        wchar_t* w = utf8_to_wide_dup(decoded);
        if (w) {
            ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
            free(w);
        }
    }

clear:
    if (decoded) free(decoded);
    if (localPath) free(localPath);
    exec_js(d->pBrowser, L"document.title='MDView';");
}

/* --- Split-view scroll and source-line sync helpers --- */

static void sync_panes_here(MDViewData* d) {
    if (!d || !d->pBrowser) return;
    exec_js(d->pBrowser, L"if(window.mdvSyncHere)mdvSyncHere();");
}

static void sync_html_point(MDViewData* d, int x, int y) {
    wchar_t js[160];
    if (!d || !d->pBrowser) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    _snwprintf_s(js, _countof(js), _TRUNCATE,
        L"if(window.mdvSyncPoint)mdvSyncPoint(%d,%d);", x, y);
    exec_js(d->pBrowser, js);
}

typedef struct {
    int fontSize;
    int maxWidth;
    int lineNums;
    int darkMode;
    int findVisible;
    int tocVisible;
    int helpVisible;
    int hasMatches;
    int editableActive;
    int viewMode;
    int wordWrap;
} MDViewHtmlState;

static int get_html_state(MDViewData* d, MDViewHtmlState* st) {
    char t[160];
    if (!d || !d->pBrowser || !st) return 0;
    ZeroMemory(st, sizeof(*st));
    exec_js(d->pBrowser,
        L"(function(){var fb=document.getElementById('mdv-fb'),toc=document.getElementById('mdv-toc'),"
        L"h=document.getElementById('mdv-help'),a=document.activeElement,"
        L"ed=(!!a&&((a.tagName==='INPUT')||(a.tagName==='TEXTAREA')||a.isContentEditable));"
        L"document.title='MDVSTATE:'+fs+','+mw+','+ln+','+(document.body.className.indexOf('dark')>=0?1:0)+','+"
        L"((fb&&fb.className.indexOf('on')>=0)?1:0)+','+((toc&&toc.className.indexOf('on')>=0)?1:0)+','+"
        L"((h&&h.className.indexOf('on')>=0)?1:0)+','+((typeof fm!=='undefined'&&fm&&fm.length)?1:0)+','+(ed?1:0)+','+mdvViewMode+','+mdvWordWrap;})();");
    if (!get_document_title_utf8(d->pBrowser, t, (int)sizeof(t))) return 0;
    if (strncmp(t, "MDVSTATE:", 9) != 0) return 0;
    return sscanf_s(t + 9, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
        &st->fontSize, &st->maxWidth, &st->lineNums, &st->darkMode,
        &st->findVisible, &st->tocVisible, &st->helpVisible,
        &st->hasMatches, &st->editableActive, &st->viewMode, &st->wordWrap) == 11;
}

static void close_html_overlays(MDViewData* d) {
    if (!d || !d->pBrowser) return;
    exec_js(d->pBrowser,
        L"(function(){var toc=document.getElementById('mdv-toc'),h=document.getElementById('mdv-help');"
        L"if(typeof hf==='function')hf();"
        L"else{var fb=document.getElementById('mdv-fb');if(fb)fb.className='';if(typeof cf==='function')cf();}"
        L"if(toc)toc.className='';if(document.body)document.body.style.marginRight='0';if(h)h.className='';})();");
}

static HWND get_root_parent(HWND hwnd) {
    HWND w = hwnd;
    while (w) {
        HWND p = GetParent(w);
        if (!p) break;
        w = p;
    }
    return w;
}

static void forward_key_to_lister(HWND hwnd, WPARAM wP, LPARAM lP) {
    HWND root = get_root_parent(hwnd);
    if (root) PostMessageW(root, WM_KEYDOWN, wP, lP);
}

static void layout_views(MDViewData* d) {
    if (!d || !d->hwndContainer || !d->pBrowser) return;

    RECT rc; GetClientRect(d->hwndContainer, &rc);
    int w = rc.right;
    int h = rc.bottom;

    IWebBrowser2_put_Left(d->pBrowser, 0);
    IWebBrowser2_put_Top(d->pBrowser, 0);
    IWebBrowser2_put_Width(d->pBrowser, w);
    IWebBrowser2_put_Height(d->pBrowser, h);

    if (d->pOleObj) {
        IOleInPlaceObject* pIPO = NULL;
        IOleObject_QueryInterface(d->pOleObj, &IID_IOleInPlaceObject, (void**)&pIPO);
        if (pIPO) {
            RECT rB = { 0, 0, w, h };
            IOleInPlaceObject_SetObjectRects(pIPO, &rB, &rB);
            IOleInPlaceObject_Release(pIPO);
        }
    }

    HWND child = GetWindow(d->hwndContainer, GW_CHILD);
    while (child) {
        MoveWindow(child, 0, 0, w, h, TRUE);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

static void set_view_mode(MDViewData* d, ViewMode mode) {
    wchar_t js[64];
    if (!d || !d->pBrowser) return;
    d->viewMode=mode;
    _snwprintf_s(js,_countof(js),_TRUNCATE,L"mdvSetView(%d)",mode);
    exec_js(d->pBrowser,js);
}

static void toggle_split_view(MDViewData* d) {
    set_view_mode(d,d->viewMode==VIEW_SPLIT ? VIEW_PREVIEW : VIEW_SPLIT);
}


static void show_context_menu(MDViewData* d, HWND hwnd, int x, int y) {
    if (!d) return;
    MDViewHtmlState st;
    int hasState = get_html_state(d, &st);
    if (hasState) d->viewMode=(ViewMode)st.viewMode;

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    ThemeChoice themes[MDV_MAX_PALETTES];
    int themeCount=append_theme_menu(hMenu,themes);
    AppendMenuW(hMenu, MF_STRING, IDM_COPY,  L"Copy\tCtrl+C");
    AppendMenuW(hMenu, MF_STRING, IDM_PASTE, L"Paste\tCtrl+V");
    AppendMenuW(hMenu, MF_STRING | (d->viewMode==VIEW_PREVIEW ? MF_CHECKED : 0), IDM_VIEW_PREVIEW, L"Preview\tCtrl+1");
    AppendMenuW(hMenu, MF_STRING | (d->viewMode==VIEW_CODE ? MF_CHECKED : 0), IDM_VIEW_CODE, L"Code\tCtrl+2");
    AppendMenuW(hMenu, MF_STRING | (d->viewMode==VIEW_SPLIT ? MF_CHECKED : 0), IDM_TOGGLE_SPLIT, L"Split view\tCtrl+M");
    AppendMenuW(hMenu, MF_STRING | ((hasState && st.wordWrap) ? MF_CHECKED : 0), IDM_WORD_WRAP, L"Wrap source lines\tCtrl+W");
    if (d->viewMode==VIEW_SPLIT) {
        AppendMenuW(hMenu, MF_STRING, IDM_SYNC_PANES, L"Sync panes here\tCtrl+Y");
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING, IDM_ZOOM_IN,    L"Zoom in\tCtrl++");
    AppendMenuW(hMenu, MF_STRING, IDM_ZOOM_OUT,   L"Zoom out\tCtrl+-");
    {
        wchar_t zoomLabel[96];
        int zoomPct = 100;
        if (hasState && st.fontSize > 0) zoomPct = (st.fontSize * 100 + MDVIEW_ZOOM_DEFAULT / 2) / MDVIEW_ZOOM_DEFAULT;
        _snwprintf_s(zoomLabel, _countof(zoomLabel), _TRUNCATE, L"Reset zoom\tCtrl+0 (%d%%)", zoomPct);
        AppendMenuW(hMenu, MF_STRING, IDM_ZOOM_RESET, zoomLabel);
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING | ((hasState && st.darkMode) ? MF_CHECKED : 0), IDM_TOGGLE_DARK,     L"Toggle dark mode\tCtrl+D");
    AppendMenuW(hMenu, MF_STRING | ((hasState && st.tocVisible) ? MF_CHECKED : 0), IDM_TOGGLE_TOC,      L"Toggle table of contents\tCtrl+T");
    AppendMenuW(hMenu, MF_STRING, IDM_SEARCH,          L"Search\tCtrl+F / F7");
    if (hasState && st.findVisible) {
        AppendMenuW(hMenu, MF_STRING, IDM_CLOSE_SEARCH, L"Close search bar\tEsc");
    }
    AppendMenuW(hMenu, MF_STRING, IDM_PRINT,           L"Print\tCtrl+P");
    AppendMenuW(hMenu, MF_STRING, IDM_TOP,             L"Scroll to top\tCtrl+G");
    AppendMenuW(hMenu, MF_STRING | ((hasState && st.lineNums) ? MF_CHECKED : 0), IDM_TOGGLE_LINEWRAP, L"Line numbers\tCtrl+L");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING | ((!hasState || st.maxWidth == 0) ? MF_CHECKED : 0), IDM_WIDTH_TOGGLE, L"Fit to window");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(hMenu, MF_STRING, IDM_HELP, L"Help\tF1");
    if (d->viewMode!=VIEW_PREVIEW) {
        wchar_t rawInfo[80];
        _snwprintf_s(rawInfo, _countof(rawInfo), _TRUNCATE, L"RAW chars: %llu", (unsigned long long)d->rawCharCount);
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, rawInfo);
    }

    UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY;
    int cmd = (int)TrackPopupMenu(hMenu, flags, x, y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd>=IDM_COLOR_THEME) { apply_theme_command(d,cmd,themes,themeCount); return; }
    switch (cmd) {
    case IDM_COPY:
        do_copy(d);
        break;
    case IDM_PASTE:
        browser_exec_olecmd(d->pBrowser, OLECMDID_PASTE);
        break;
    case IDM_TOGGLE_SPLIT:
        toggle_split_view(d);
        break;
    case IDM_VIEW_PREVIEW:
        set_view_mode(d,VIEW_PREVIEW);
        break;
    case IDM_VIEW_CODE:
        set_view_mode(d,VIEW_CODE);
        break;
    case IDM_WORD_WRAP:
        exec_js(d->pBrowser,L"mdvToggleWrap()");
        break;
    case IDM_SYNC_PANES:
        sync_panes_here(d);
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
        toggle_color_theme(d);
        break;
    case IDM_TOGGLE_TOC:
        exec_js(d->pBrowser, L"ttoc()");
        break;
    case IDM_SEARCH:
        exec_js(d->pBrowser, L"sf()");
        break;
    case IDM_CLOSE_SEARCH:
        exec_js(d->pBrowser, L"hf()");
        break;
    case IDM_PRINT:
        exec_js(d->pBrowser, L"window.print()");
        break;
    case IDM_TOP:
        exec_js(d->pBrowser, L"mdvSetScrollY(0)");
        break;
    case IDM_TOGGLE_LINEWRAP:
        exec_js(d->pBrowser, L"tl()");
        break;
    case IDM_WIDTH_TOGGLE:
        exec_js(d->pBrowser, L"fw()");
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

    if (msg == WM_MOUSEWHEEL) {
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            int delta = GET_WHEEL_DELTA_WPARAM(wP);
            d->ctrlWheelRemainder += delta;
            while (d->ctrlWheelRemainder >= WHEEL_DELTA) {
                exec_js(d->pBrowser, L"zi()");
                d->ctrlWheelRemainder -= WHEEL_DELTA;
            }
            while (d->ctrlWheelRemainder <= -WHEEL_DELTA) {
                exec_js(d->pBrowser, L"zo()");
                d->ctrlWheelRemainder += WHEEL_DELTA;
            }
            return 0;
        }
        d->ctrlWheelRemainder = 0;
    }
    if (msg == WM_KEYDOWN) {
        MDViewHtmlState st;
        int hasState = get_html_state(d, &st);
        if (hasState) d->viewMode=(ViewMode)st.viewMode;
        int ctrl = GetKeyState(VK_CONTROL) & 0x8000;
        int shift = GetKeyState(VK_SHIFT) & 0x8000;
        if (!ctrl && hasState && !st.editableActive &&
            ((wP >= '1' && wP <= '9') || wP == 'N' || wP == 'P')) {
            forward_key_to_lister(hwnd, wP, lP);
            return 0;
        }
        if (ctrl) {
            switch (wP) {
            case VK_OEM_PLUS: case VK_ADD:
                exec_js(d->pBrowser, L"zi()"); return 0;
            case VK_OEM_MINUS: case VK_SUBTRACT:
                exec_js(d->pBrowser, L"zo()"); return 0;
            case '0': case VK_NUMPAD0:
                exec_js(d->pBrowser, L"zr()"); return 0;
            case '1': case VK_NUMPAD1:
                set_view_mode(d,VIEW_PREVIEW); return 0;
            case '2': case VK_NUMPAD2:
                set_view_mode(d,VIEW_CODE); return 0;
            case 'W':
                exec_js(d->pBrowser,L"mdvToggleWrap()"); return 0;
            case 'D':
                toggle_color_theme(d); return 0;
            case 'T':
                exec_js(d->pBrowser, L"ttoc()"); return 0;
            case 'F':
                exec_js(d->pBrowser, L"sf()"); return 0;
            case 'P':
                exec_js(d->pBrowser, L"window.print()"); return 0;
            case 'G':
                exec_js(d->pBrowser, L"mdvSetScrollY(0)"); return 0;
            case 'L':
                exec_js(d->pBrowser, L"tl()"); return 0;
            case VK_OEM_2:
                exec_js(d->pBrowser, L"th()"); return 0;
            case 'A':
                do_select_all(d); return 0;
            case 'C':
            case VK_INSERT:
                do_copy(d); return 0;
            case 'V':
                browser_execwb(d->pBrowser, OLECMDID_PASTE); return 0;
            case 'M':
                toggle_split_view(d); return 0;
            case 'Y':
                sync_panes_here(d); return 0;
            }
        }
        if (shift && wP == VK_INSERT && hasState && st.editableActive) {
            browser_execwb(d->pBrowser, OLECMDID_PASTE);
            return 0;
        }
        if (wP == VK_F1) {
            exec_js(d->pBrowser, L"th()"); return 0;
        }
        if (wP == VK_F7) {
            exec_js(d->pBrowser, L"f7f()"); return 0;
        }
        if (wP == VK_F3) {
            if (GetKeyState(VK_SHIFT) & 0x8000)
                exec_js(d->pBrowser, L"hkf(1)");
            else
                exec_js(d->pBrowser, L"hkf(0)");
            return 0;
        }
        
        /* Escape: the IE control eats it, so forward to TC's lister parent */
        if (wP == VK_ESCAPE) {
            if (hasState && (st.findVisible || st.tocVisible || st.helpVisible)) {
                close_html_overlays(d);
                return 0;
            }
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
        if (msg == WM_LBUTTONDBLCLK) {
            sync_html_point(d, GET_X_LPARAM(lP), GET_Y_LPARAM(lP));
        }
        return r;
    }
}

/* --- Minimal COM Site Implementation --- */

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

/* --- String Buffer --- */

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

#include "mdview_math.inc"

static int is_inline_html_tag_at(const char* t, size_t len, size_t pos, size_t* endPos) {
    size_t j;
    char quote=0;
    if (!t || pos >= len || t[pos] != '<') return 0;
    if (len-pos>=4 && memcmp(t+pos,"<!--",4)==0) {
        for (j=pos+4;j+2<len;++j) {
            if (memcmp(t+j,"-->",3)==0) {
                if (endPos) *endPos=j+3;
                return 1;
            }
        }
        return 0;
    }
    j = pos + 1;
    if (j < len && (t[j] == '!' || t[j] == '?' || t[j] == '/')) j++;
    if (j >= len || !isalpha((unsigned char)t[j])) return 0;
    while (j<len && (isalnum((unsigned char)t[j]) || t[j]=='-')) ++j;
    if (j==len || !(isspace((unsigned char)t[j]) || t[j]=='>' ||
                    (t[j]=='/' && j+1<len && t[j+1]=='>'))) return 0;
    for (;j<len;++j) {
        if (quote) { if (t[j]==quote) quote=0; }
        else if (t[j]=='"' || t[j]=='\'') quote=t[j];
        else if (t[j]=='>') break;
    }
    if (j >= len) return 0;
    if (endPos) *endPos = j + 1;
    return 1;
}

static int html_tag_name_matches(const char* s, const char* tag) {
    size_t n=strlen(tag);
    return _strnicmp(s,tag,n)==0 &&
           (s[n]=='>' || s[n]=='/' || isspace((unsigned char)s[n]));
}

static const char* html_block_name(const char* line) {
    static const char* tags[]={"table","details","p","div","center","figure"};
    if (line[0]=='<') {
        for (size_t i=0;i<_countof(tags);++i)
            if (html_tag_name_matches(line+1,tags[i])) return tags[i];
    }
    return NULL;
}

static int is_mermaid_lang(const char* lang) {
    if (!lang) return 0;
    while (*lang == ' ' || *lang == '\t') lang++;
    return _strnicmp(lang, "mermaid", 7) == 0 &&
           (lang[7] == '\0' || lang[7] == ' ' || lang[7] == '\t' || lang[7] == '`' || lang[7] == '~');
}

static void sb_append_line_attr(StrBuf* sb, int line);
static void sb_append_line_range_attr(StrBuf* sb, int startLine, int endLine);

static void append_mermaid_block(StrBuf* sb, const char* src, size_t n, int startLine, int endLine) {
    sb_append(sb, "<div class=\"mdv-mermaid\" data-mdv-mermaid=\"1\"");
    sb_append_line_range_attr(sb, startLine, endLine);
    sb_append(sb, "><pre class=\"mdv-mermaid-src\"><code>");
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

static int is_file_url_scheme(const char* url, size_t len) {
    if (!url || len < 7) return 0;
    return _strnicmp(url, "file://", 7) == 0;
}

static int is_image_data_url_scheme(const char* url, size_t len) {
    if (!url || len < 17) return 0;
    if (_strnicmp(url, "data:image/", 11) != 0) return 0;
    return strstr(url, ";base64,") != NULL || strstr(url, ";BASE64,") != NULL;
}

static char* trim_url_slice_dup(const char* url, size_t len) {
    size_t start = 0;
    size_t end = len;
    char* out;

    while (start < len && (url[start] == ' ' || url[start] == '\t' || url[start] == '\r' || url[start] == '\n')) {
        start++;
    }
    while (end > start && (url[end - 1] == ' ' || url[end - 1] == '\t' || url[end - 1] == '\r' || url[end - 1] == '\n')) {
        end--;
    }

    out = (char*)malloc(end - start + 1);
    if (!out) return NULL;
    if (end > start) memcpy(out, url + start, end - start);
    out[end - start] = '\0';
    return out;
}

static int is_windows_absolute_path_utf8(const char* path) {
    if (!path || !path[0]) return 0;
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
        return 1;
    }
    if ((path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/')) return 1;
    return 0;
}

static int is_probably_relative_markdown_url(const char* url) {
    if (!url || !url[0]) return 0;
    if (url[0] == '#') return 0;
    if (is_safe_url_scheme(url, strlen(url), 1)) return 0;
    if (is_image_data_url_scheme(url, strlen(url)) || is_file_url_scheme(url, strlen(url))) return 0;
    if (is_windows_absolute_path_utf8(url)) return 1;
    if (url[0] == '/' || url[0] == '\\') return 1;
    return 1;
}

static void sb_append_url_encoded(StrBuf* sb, const char* s) {
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        unsigned char ch = *p++;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/' || ch == ':') {
            char cbuf[2];
            cbuf[0] = (char)ch;
            cbuf[1] = '\0';
            sb_append(sb, cbuf);
        } else {
            char enc[4];
            enc[0] = '%';
            enc[1] = hex[(ch >> 4) & 0x0F];
            enc[2] = hex[ch & 0x0F];
            enc[3] = '\0';
            sb_append(sb, enc);
        }
    }
}

static char* file_url_from_utf8_path(const char* pathUtf8) {
    StrBuf sb;
    const char* p;

    if (!pathUtf8 || !pathUtf8[0]) return NULL;
    sb_init(&sb);
    if (!sb.data) return NULL;

    if (is_windows_absolute_path_utf8(pathUtf8) &&
        (pathUtf8[0] == '\\' || pathUtf8[0] == '/') &&
        (pathUtf8[1] == '\\' || pathUtf8[1] == '/')) {
        sb_append(&sb, "file:");
    } else {
        sb_append(&sb, "file:///");
    }

    for (p = pathUtf8; *p; ++p) {
        if (*p == '\\') {
            sb_append(&sb, "/");
        } else {
            char chunk[2];
            chunk[0] = *p;
            chunk[1] = '\0';
            sb_append_url_encoded(&sb, chunk);
        }
    }

    return sb.data;
}

static char* resolve_markdown_url(const char* currentFile, const char* url, size_t len) {
    char* trimmed;
    char* fragment = NULL;
    char* query = NULL;
    char* pathPart;
    char* result = NULL;
    char* fileUrl = NULL;
    wchar_t* currentWide = NULL;
    wchar_t* baseWide = NULL;
    wchar_t* relWide = NULL;
    wchar_t joined[MAX_PATH];
    wchar_t absolute[MAX_PATH];
    wchar_t* filePart;
    DWORD fullLen;
    char* absoluteUtf8 = NULL;
    char* decodedPath = NULL;
    char* qmark;
    char* hash;
    size_t resultLen;

    trimmed = trim_url_slice_dup(url, len);
    if (!trimmed) return NULL;
    if (!trimmed[0]) return trimmed;
    if (!is_probably_relative_markdown_url(trimmed) || !currentFile || !currentFile[0]) return trimmed;

    hash = strchr(trimmed, '#');
    if (hash) {
        fragment = mdview_strdup(hash);
        *hash = '\0';
    }
    qmark = strchr(trimmed, '?');
    if (qmark) {
        query = mdview_strdup(qmark);
        *qmark = '\0';
    }
    pathPart = trimmed;

    currentWide = utf8_to_wide_dup(currentFile);
    decodedPath = url_percent_decode_dup(pathPart,strlen(pathPart));
    relWide = utf8_to_wide_dup(decodedPath ? decodedPath : pathPart);
    if (!currentWide || !relWide) goto done;

    if (is_windows_absolute_path_utf8(pathPart)) {
        fullLen = GetFullPathNameW(relWide, MAX_PATH, absolute, NULL);
    } else {
        wchar_t combined[MAX_PATH];
        fullLen=GetFullPathNameW(currentWide,MAX_PATH,joined,&filePart);
        if (!fullLen || fullLen>=MAX_PATH || !filePart) goto done;
        *filePart=0;
        baseWide=joined;
        if (wcslen(baseWide)+wcslen(relWide)>=MAX_PATH) goto done;
        wcscpy_s(combined,MAX_PATH,baseWide);
        wcscat_s(combined,MAX_PATH,relWide);
        fullLen = GetFullPathNameW(combined, MAX_PATH, absolute, NULL);
    }
    if (fullLen == 0 || fullLen >= MAX_PATH) goto done;

    absoluteUtf8 = wide_to_utf8_dup(absolute);
    if (!absoluteUtf8) goto done;
    fileUrl = file_url_from_utf8_path(absoluteUtf8);
    if (!fileUrl) goto done;

    resultLen = strlen(fileUrl) + (query ? strlen(query) : 0) + (fragment ? strlen(fragment) : 0) + 1;
    result = (char*)malloc(resultLen);
    if (!result) goto done;
    snprintf(result, resultLen, "%s%s%s", fileUrl, query ? query : "", fragment ? fragment : "");

done:
    if (!result) result = trimmed; else free(trimmed);
    if (fragment) free(fragment);
    if (query) free(query);
    if (currentWide) free(currentWide);
    if (relWide) free(relWide);
    if (absoluteUtf8) free(absoluteUtf8);
    if (fileUrl) free(fileUrl);
    free(decodedPath);
    return result;
}

typedef struct {
    const char* shortcode;
    const char* utf8;
} EmojiMap;

static const char* lookup_emoji_shortcode(const char* shortcode, size_t len) {
    static const EmojiMap kEmojiMap[] = {
        { "+1",       "\xF0\x9F\x91\x8D" },
        { "-1",       "\xF0\x9F\x91\x8E" },
        { "grinning", "\xF0\x9F\x98\x80" },
        { "smile",    "\xF0\x9F\x98\x84" },
        { "smiley",   "\xF0\x9F\x98\x83" },
        { "laughing", "\xF0\x9F\x98\x86" },
        { "wink",     "\xF0\x9F\x98\x89" },
        { "blush",    "\xF0\x9F\x98\x8A" },
        { "heart_eyes","\xF0\x9F\x98\x8D" },
        { "thinking", "\xF0\x9F\xA4\x94" },
        { "cry",      "\xF0\x9F\x98\xA2" },
        { "sob",      "\xF0\x9F\x98\xAD" },
        { "angry",    "\xF0\x9F\x98\xA0" },
        { "sunglasses","\xF0\x9F\x98\x8E" },
        { "partying_face","\xF0\x9F\xA5\xB3" },
        { "heart",    "\xE2\x9D\xA4\xEF\xB8\x8F" },
        { "yellow_heart","\xF0\x9F\x92\x9B" },
        { "green_heart","\xF0\x9F\x92\x9A" },
        { "blue_heart","\xF0\x9F\x92\x99" },
        { "purple_heart","\xF0\x9F\x92\x9C" },
        { "broken_heart","\xF0\x9F\x92\x94" },
        { "fire",     "\xF0\x9F\x94\xA5" },
        { "star",     "\xE2\xAD\x90" },
        { "sparkles", "\xE2\x9C\xA8" },
        { "boom",     "\xF0\x9F\x92\xA5" },
        { "warning",  "\xE2\x9A\xA0\xEF\xB8\x8F" },
        { "white_check_mark","\xE2\x9C\x85" },
        { "x",        "\xE2\x9D\x8C" },
        { "thumbsup", "\xF0\x9F\x91\x8D" },
        { "thumbsdown","\xF0\x9F\x91\x8E" },
        { "clap",     "\xF0\x9F\x91\x8F" },
        { "wave",     "\xF0\x9F\x91\x8B" },
        { "pray",     "\xF0\x9F\x99\x8F" },
        { "rocket",   "\xF0\x9F\x9A\x80" },
        { "tada",     "\xF0\x9F\x8E\x89" },
        { "bulb",     "\xF0\x9F\x92\xA1" },
        { "zap",      "\xE2\x9A\xA1" },
        { "bug",      "\xF0\x9F\x90\x9B" },
        { "memo",     "\xF0\x9F\x93\x9D" },
        { "book",     "\xF0\x9F\x93\x96" },
        { "computer", "\xF0\x9F\x92\xBB" },
        { "desktop_computer", "\xF0\x9F\x96\xA5\xEF\xB8\x8F" },
        { "iphone",   "\xF0\x9F\x93\xB1" },
        { "coffee",   "\xE2\x98\x95" },
        { "muscle",   "\xF0\x9F\x92\xAA" },
        { "eyes",     "\xF0\x9F\x91\x80" },
        { "mag",      "\xF0\x9F\x94\x8D" },
        { "lock",     "\xF0\x9F\x94\x92" },
        { "key",      "\xF0\x9F\x94\x91" },
        { "link",     "\xF0\x9F\x94\x97" },
        { "pushpin",  "\xF0\x9F\x93\x8C" },
        { "hourglass", "\xE2\x8C\x9B" },
        { "calendar", "\xF0\x9F\x93\x85" },
        { "checkered_flag", "\xF0\x9F\x8F\x81" }
    };
    size_t i;
    for (i = 0; i < sizeof(kEmojiMap) / sizeof(kEmojiMap[0]); ++i) {
        if (strlen(kEmojiMap[i].shortcode) == len &&
            _strnicmp(kEmojiMap[i].shortcode, shortcode, len) == 0) {
            return kEmojiMap[i].utf8;
        }
    }
    return NULL;
}

/* --- Markdown Inline Parser --- */

typedef struct {
    char* label;
    char* url;
} MdReference;

typedef struct {
    char* label;
    char* text;
    char* html;
    int firstLine, lastLine;
    int number, referenceCount;
} MdFootnote;

typedef struct {
    MdFootnote* items;
    int* order;
    int count, capacity, used;
} MdFootnotes;

typedef struct MdReferenceMap {
    MdReference* refs;
    int count;
    int cap;
    MdFootnotes* notes;
    const struct MdReferenceMap* parent;
} MdReferenceMap;

static char* normalize_reference_label(const char* s, size_t len) {
    size_t start = 0;
    size_t end = len;
    StrBuf out;
    int pendingSpace = 0;

    while (start < len && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) start++;
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) end--;

    if (!sb_init(&out)) return NULL;
    for (size_t i = start; i < end; ++i) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            pendingSpace = 1;
            continue;
        }
        if (pendingSpace && out.len > 0) sb_append_char(&out, ' ');
        pendingSpace = 0;
        if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch + ('a' - 'A'));
        sb_append_char(&out, (char)ch);
    }
    return out.data;
}

static void free_reference_map(MdReferenceMap* map) {
    int i;
    if (!map) return;
    for (i = 0; i < map->count; ++i) {
        free(map->refs[i].label);
        free(map->refs[i].url);
    }
    free(map->refs);
    map->refs = NULL;
    map->count = 0;
    map->cap = 0;
}

static const char* lookup_reference_url(const MdReferenceMap* map, const char* label, size_t len) {
    char* norm;
    int i;
    const char* url = NULL;
    if (!map || !label) return NULL;
    norm = normalize_reference_label(label, len);
    if (!norm) return NULL;
    for (;map && !url;map=map->parent) {
        for (i = 0; i < map->count; ++i) {
            if (_stricmp(map->refs[i].label,norm)==0) {
                url=map->refs[i].url;
                break;
            }
        }
    }
    free(norm);
    return url;
}

static int parse_reference_definition_line(const char* line, char** labelOut, char** urlOut) {
    const char* p = line;
    const char* labelStart;
    const char* labelEnd;
    const char* urlStart;
    const char* urlEnd;
    char* label;
    char* url;

    if (labelOut) *labelOut = NULL;
    if (urlOut) *urlOut = NULL;
    if (!line) return 0;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '[') return 0;
    labelStart = ++p;
    if (*labelStart=='^') return 0;
    while (*p && *p != ']') p++;
    if (*p != ']' || p == labelStart || p[1] != ':') return 0;
    labelEnd = p;
    p += 2;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return 0;

    if (*p == '<') {
        urlStart = ++p;
        while (*p && *p != '>') p++;
        if (*p != '>') return 0;
        urlEnd = p;
    } else {
        urlStart = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        urlEnd = p;
    }

    label = normalize_reference_label(labelStart, (size_t)(labelEnd - labelStart));
    url = trim_url_slice_dup(urlStart, (size_t)(urlEnd - urlStart));
    if (!label || !url || !label[0] || !url[0]) {
        if (label) free(label);
        if (url) free(url);
        return 0;
    }

    if (labelOut) *labelOut = label; else free(label);
    if (urlOut) *urlOut = url; else free(url);
    return 1;
}

static int is_reference_definition_line(const char* line) {
    return parse_reference_definition_line(line, NULL, NULL);
}

/* A label is a single token. Use the same normalisation as reference links. */
static const char* footnote_label_end(const char* p, const char* end) {
    const char* q=p;
    while (q<end && *q!=']' && *q!='[' && !isspace((unsigned char)*q)) ++q;
    return q>p && q<end && *q==']' ? q : NULL;
}

static int find_footnote(const MdFootnotes* notes, const char* label) {
    for (int i=0;i<notes->count;++i)
        if (strcmp(notes->items[i].label,label)==0) return i;
    return -1;
}

static size_t append_footnote_ref(StrBuf* sb, MdFootnotes* notes, const char* p, const char* end) {
    const char* labelEnd;
    char* label;
    int index;
    MdFootnote* note;
    char html[220];
    if (!notes || !notes->order || end-p<4 || p[0]!='[' || p[1]!='^') return 0;
    labelEnd=footnote_label_end(p+2,end);
    if (!labelEnd) return 0;
    label=normalize_reference_label(p+2,labelEnd-(p+2));
    if (!label) return 0;
    index=find_footnote(notes,label);
    free(label);
    if (index<0) return 0;
    note=&notes->items[index];
    if (!note->number) {
        notes->order[notes->used++]=index;
        note->number=notes->used;
    }
    ++note->referenceCount;
    sprintf_s(html,sizeof(html),"<sup class=\"mdv-footnote-ref\"><a id=\"mdv-fnref.%d.%d\" href=\"#mdv-fn.%d\">%d</a></sup>",
              note->number,note->referenceCount,note->number,note->number);
    sb_append(sb,html);
    return labelEnd+1-p;
}

static char* resolve_link_destination(const char* currentFile, const char* url, size_t len) {
    const char* p = url;
    const char* end = url + len;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (p < end && *p == '<') {
        const char* s = ++p;
        while (p < end && *p != '>') p++;
        return resolve_markdown_url(currentFile, s, (size_t)(p - s));
    } else {
        const char* s = p;
        while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        return resolve_markdown_url(currentFile, s, (size_t)(p - s));
    }
}

static int is_renderable_image_url(const char* url) {
    size_t len;
    if (!url) return 0;
    len = strlen(url);
    return is_safe_url_scheme(url, len, 0) ||
           is_file_url_scheme(url, len) ||
           is_image_data_url_scheme(url, len) ||
           is_probably_relative_markdown_url(url);
}

static int is_renderable_link_url(const char* url) {
    size_t len;
    if (!url) return 0;
    len = strlen(url);
    return is_safe_url_scheme(url, len, 1) ||
           is_file_url_scheme(url, len) ||
           url[0] == '#';
}

static void sb_append_slice(StrBuf* sb, const char* src, size_t n) {
    if (!sb_ensure(sb,n)) return;
    memcpy(sb->data+sb->len,src,n);
    sb->len+=n;
    sb->data[sb->len]=0;
}

static void append_html_tag(StrBuf* sb, const char* tag, size_t len, const char* currentFile) {
    const char* p=tag+1, *end=tag+len-1;
    if (currentFile && html_tag_name_matches(p,"img")) {
        p+=3;
        while (p<end) {
            const char* name, *value, *after;
            char quote;
            size_t nameLen;
            while (p<end && isspace((unsigned char)*p)) ++p;
            if (p==end || *p=='/') break;
            name=p;
            while (p<end && !isspace((unsigned char)*p) && *p!='=') ++p;
            nameLen=p-name;
            while (p<end && isspace((unsigned char)*p)) ++p;
            if (p==end || *p!='=') continue;
            ++p;
            while (p<end && isspace((unsigned char)*p)) ++p;
            quote=p<end && (*p=='"' || *p=='\'') ? *p++ : 0;
            value=p;
            while (p<end && (quote ? *p!=quote : !isspace((unsigned char)*p))) ++p;
            after=p;
            if (quote && p<end) ++p;
            if (nameLen==3 && _strnicmp(name,"src",3)==0 && after>value) {
                StrBuf src;
                if (!sb_init(&src)) break;
                for (const char* ch=value;ch<after;++ch) {
                    if (after-ch>=5 && memcmp(ch,"&amp;",5)==0) ch+=4;
                    sb_append_char(&src,*ch);
                }
                char* url=trim_url_slice_dup(src.data,src.len);
                free(src.data);
                if (url && url[0] && url[0]!='#' && strncmp(url,"//",2)!=0 &&
                    (!strchr(url,':') || is_windows_absolute_path_utf8(url))) {
                    char* resolved=resolve_markdown_url(currentFile,url,strlen(url));
                    if (resolved && is_file_url_scheme(resolved,strlen(resolved))) {
                        sb_append_slice(sb,tag,(value-tag)-(quote!=0));
                        sb_append_char(sb,'"');
                        sb_append_esc(sb,resolved,strlen(resolved));
                        sb_append_char(sb,'"');
                        sb_append_slice(sb,p,tag+len-p);
                        free(resolved);
                        free(url);
                        return;
                    }
                    free(resolved);
                }
                free(url);
                break;
            }
        }
    }
    sb_append_slice(sb,tag,len);
}

static void append_html_block(StrBuf* sb, const char* src, size_t len, const char* currentFile) {
    size_t start=0, pos=0, end;
    while (pos<len) {
        if (src[pos]=='<' && is_inline_html_tag_at(src,len,pos,&end)) {
            sb_append_slice(sb,src+start,pos-start);
            append_html_tag(sb,src+pos,end-pos,currentFile);
            pos=start=end;
        } else ++pos;
    }
    sb_append_slice(sb,src+start,len-start);
}

static int html_block_delta(const char* line, const char* tag) {
    size_t pos=0, end, len=strlen(line);
    int depth=0;
    while (pos<len) {
        if (line[pos]=='<' && is_inline_html_tag_at(line,len,pos,&end)) {
            const char* name=line+pos+1;
            if (*name=='/' && html_tag_name_matches(name+1,tag)) --depth;
            else if (html_tag_name_matches(name,tag) && line[end-2]!='/') ++depth;
            pos=end;
        } else ++pos;
    }
    return depth;
}

#include "mdview_autolink.inc"

static int is_underscore_boundary(unsigned char ch) {
    return isspace(ch) || ispunct(ch);
}

static void parse_inline_impl(StrBuf* sb, const char* t, size_t len, const char* currentFile,
                              const MdReferenceMap* refs, int allowLinks) {
    size_t i = 0;
    int rawLink=0, rawCode=0;
    while (i < len) {
        size_t htmlEnd = 0, consumed;
        int linkAllowed=allowLinks && !rawLink && !rawCode;
        /* Backslash escape */
        if (t[i]=='\\' && i+1<len) {
            char nx=t[i+1];
            if(nx=='*'||nx=='_'||nx=='`'||nx=='['||nx==']'||nx=='('||nx==')'||nx=='#'||nx=='~'||nx=='!'||nx=='|'||nx=='\\'||nx=='-'||nx=='$')
            { sb_append_esc(sb,&t[i+1],1); i+=2; continue; }
        }
        /* Line break (2+ trailing spaces + \n) */
        if (t[i]==' ' && i+1<len && t[i+1]==' ') {
            size_t j=i+2; while(j<len&&t[j]==' ')j++;
            if(j<len&&t[j]=='\n'){ sb_append(sb,"<br>\n"); i=j+1; continue; }
        }
        if (t[i]=='<' && (consumed=append_autolink(sb,t,len,i,linkAllowed))!=0) {
            i+=consumed;
            continue;
        }
        /* Inline HTML tag passthrough */
        if (t[i] == '<' && is_inline_html_tag_at(t, len, i, &htmlEnd)) {
            const char* name=t+i+1;
            int closing=*name=='/';
            if (closing) ++name;
            if (html_tag_name_matches(name,"a")) rawLink=!closing;
            if (html_tag_name_matches(name,"code")) rawCode=!closing;
            append_html_tag(sb,t+i,htmlEnd-i,currentFile);
            i = htmlEnd;
            continue;
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

        /* A closing dollar followed by a digit is usually currency. */
        if (t[i]=='$' && i+1<len && t[i+1]!='$' && !isspace((unsigned char)t[i+1]) &&
            (i==0 || t[i-1]!='$')) {
            size_t end=i+1;
            while (end<len && t[end]!='\n' && t[end]!='$' && t[end]!='`') {
                if (t[end]=='\\' && end+1<len && t[end+1]=='$') ++end;
                ++end;
            }
            if (end<len && t[end]=='$' && end>i+1 && !isspace((unsigned char)t[end-1]) &&
                (end+1==len || (t[end+1]!='$' && !isdigit((unsigned char)t[end+1])))) {
                if (!append_math(sb,t+i+1,end-i-1,0)) sb_append_esc(sb,t+i,end+1-i);
                i=end+1;
                continue;
            }
        }
        /* Image ![alt](url) and ![alt][ref] */
        if (t[i]=='!' && i+1<len && t[i+1]=='[') {
            size_t as=i+2,j=as; int d=1;
            while(j<len&&d>0){if(t[j]=='[')d++;else if(t[j]==']')d--;if(d>0)j++;}
            if(j<len&&j+1<len&&t[j+1]=='('){
                size_t us=j+2,ue=us; while(ue<len&&t[ue]!=')')ue++;
                if(ue<len){
                    char* resolved = resolve_link_destination(currentFile, t + us, ue - us);
                    if (resolved && is_renderable_image_url(resolved)) {
                        sb_append(sb,"<img class=\"mdv-md-image\" alt=\""); sb_append_esc(sb,t+as,j-as);
                        sb_append(sb,"\" src=\""); sb_append_esc(sb,resolved,strlen(resolved));
                        sb_append(sb,"\" style=\"max-width:100%\">");
                    } else {
                        sb_append_esc(sb, t + i, ue + 1 - i);
                    }
                    if (resolved) free(resolved);
                    i=ue+1; continue;
                }
            } else if (j<len&&j+1<len&&t[j+1]=='[') {
                size_t rs=j+2,re=rs;
                while(re<len&&t[re]!=']')re++;
                if(re<len){
                    const char* refUrl = lookup_reference_url(refs, re > rs ? t + rs : t + as, re > rs ? re - rs : j - as);
                    if(refUrl){
                        char* resolved = resolve_link_destination(currentFile, refUrl, strlen(refUrl));
                        if (resolved && is_renderable_image_url(resolved)) {
                            sb_append(sb,"<img class=\"mdv-md-image\" alt=\""); sb_append_esc(sb,t+as,j-as);
                            sb_append(sb,"\" src=\""); sb_append_esc(sb,resolved,strlen(resolved));
                            sb_append(sb,"\" style=\"max-width:100%\">");
                        } else {
                            sb_append_esc(sb, t + i, re + 1 - i);
                        }
                        if (resolved) free(resolved);
                        i=re+1; continue;
                    }
                }
            } else {
                const char* refUrl = lookup_reference_url(refs, t + as, j - as);
                if(refUrl){
                    char* resolved = resolve_link_destination(currentFile, refUrl, strlen(refUrl));
                    if (resolved && is_renderable_image_url(resolved)) {
                        sb_append(sb,"<img class=\"mdv-md-image\" alt=\""); sb_append_esc(sb,t+as,j-as);
                        sb_append(sb,"\" src=\""); sb_append_esc(sb,resolved,strlen(resolved));
                        sb_append(sb,"\" style=\"max-width:100%\">");
                    } else {
                        sb_append_esc(sb, t + i, j + 1 - i);
                    }
                    if (resolved) free(resolved);
                    i=j+1; continue;
                }
            }
        }
        /* Link [text](url) and [text][ref] */
        if (linkAllowed && t[i]=='[') {
            consumed=append_footnote_ref(sb,refs->notes,t+i,t+len);
            if (consumed) { i+=consumed; continue; }
            size_t ts=i+1,j=ts; int d=1;
            while(j<len&&d>0){if(t[j]=='[')d++;else if(t[j]==']')d--;if(d>0)j++;}
            if(j<len&&j+1<len&&t[j+1]=='('){
                size_t us=j+2,ue=us; while(ue<len&&t[ue]!=')')ue++;
                if(ue<len){
                    char* resolved = resolve_link_destination(currentFile, t + us, ue - us);
                    if (resolved && is_renderable_link_url(resolved)) {
                        sb_append(sb,"<a href=\""); sb_append_esc(sb,resolved,strlen(resolved));
                        sb_append(sb,"\">"); parse_inline_impl(sb,t+ts,j-ts,currentFile,refs,0); sb_append(sb,"</a>");
                    } else {
                        sb_append_esc(sb, t + i, ue + 1 - i);
                    }
                    if (resolved) free(resolved);
                    i=ue+1; continue;
                }
            } else if (j<len&&j+1<len&&t[j+1]=='[') {
                size_t rs=j+2,re=rs;
                while(re<len&&t[re]!=']')re++;
                if(re<len){
                    const char* refUrl = lookup_reference_url(refs, re > rs ? t + rs : t + ts, re > rs ? re - rs : j - ts);
                    if(refUrl){
                        char* resolved = resolve_link_destination(currentFile, refUrl, strlen(refUrl));
                        if (resolved && is_renderable_link_url(resolved)) {
                            sb_append(sb,"<a href=\""); sb_append_esc(sb,resolved,strlen(resolved));
                            sb_append(sb,"\">"); parse_inline_impl(sb,t+ts,j-ts,currentFile,refs,0); sb_append(sb,"</a>");
                        } else {
                            sb_append_esc(sb, t + i, re + 1 - i);
                        }
                        if (resolved) free(resolved);
                        i=re+1; continue;
                    }
                }
            } else {
                const char* refUrl = lookup_reference_url(refs, t + ts, j - ts);
                if(refUrl){
                    char* resolved = resolve_link_destination(currentFile, refUrl, strlen(refUrl));
                    if (resolved && is_renderable_link_url(resolved)) {
                        sb_append(sb,"<a href=\""); sb_append_esc(sb,resolved,strlen(resolved));
                        sb_append(sb,"\">"); parse_inline_impl(sb,t+ts,j-ts,currentFile,refs,0); sb_append(sb,"</a>");
                    } else {
                        sb_append_esc(sb, t + i, j + 1 - i);
                    }
                    if (resolved) free(resolved);
                    i=j+1; continue;
                }
            }
        }
        /* Strikethrough ~~text~~ */
        if (t[i]=='~'&&i+1<len&&t[i+1]=='~') {
            size_t s2=i+2,e2=s2; while(e2+1<len&&!(t[e2]=='~'&&t[e2+1]=='~'))e2++;
            if(e2+1<len){ sb_append(sb,"<del>"); parse_inline_impl(sb,t+s2,e2-s2,currentFile,refs,linkAllowed); sb_append(sb,"</del>"); i=e2+2; continue; }
        }
        /* Highlight ==text== */
        if (t[i]=='='&&i+1<len&&t[i+1]=='=') {
            size_t s2=i+2,e2=s2; while(e2+1<len&&!(t[e2]=='='&&t[e2+1]=='='))e2++;
            if(e2+1<len&&e2>s2){ sb_append(sb,"<span class=\"mdv-highlight\">"); parse_inline_impl(sb,t+s2,e2-s2,currentFile,refs,linkAllowed); sb_append(sb,"</span>"); i=e2+2; continue; }
        }
        /* Underscore emphasis requires delimiters at the edges of the text. */
        if (t[i]=='_') {
            size_t start=i+1;
            while (start<len && t[start]=='_') ++start;
            size_t width=start-i;
            if (width<=3 && start<len && !isspace((unsigned char)t[start]) &&
                (i==0 || is_underscore_boundary((unsigned char)t[i-1]))) {
                size_t end=start;
                while (end<len) {
                    if (t[end]=='\\' && end+1<len && (t[end+1]=='_' || t[end+1]=='\\')) {
                        end+=2;
                        continue;
                    }
                    if (t[end]!='_') { ++end; continue; }
                    size_t after=end+1;
                    while (after<len && t[after]=='_') ++after;
                    if (after-end==width && !isspace((unsigned char)t[end-1]) &&
                        (after==len || is_underscore_boundary((unsigned char)t[after]))) {
                        if (width>=2) sb_append(sb,"<strong>");
                        if (width!=2) sb_append(sb,"<em>");
                        parse_inline_impl(sb,t+start,end-start,currentFile,refs,linkAllowed);
                        if (width!=2) sb_append(sb,"</em>");
                        if (width>=2) sb_append(sb,"</strong>");
                        i=after;
                        break;
                    }
                    end=after;
                }
                if (end<len) continue;
            }
            sb_append_slice(sb,t+i,width);
            i=start;
            continue;
        }
        /* Bold+Italic ***text*** */
        if (t[i]=='*'&&i+2<len&&t[i+1]==t[i]&&t[i+2]==t[i]) {
            char m=t[i]; size_t s3=i+3,e3=s3;
            while(e3+2<len&&!(t[e3]==m&&t[e3+1]==m&&t[e3+2]==m))e3++;
            if(e3+2<len){ sb_append(sb,"<strong><em>"); parse_inline_impl(sb,t+s3,e3-s3,currentFile,refs,linkAllowed); sb_append(sb,"</em></strong>"); i=e3+3; continue; }
        }
        /* Bold **text** */
        if (t[i]=='*'&&i+1<len&&t[i+1]==t[i]) {
            char m=t[i]; size_t s2=i+2,e2=s2;
            while(e2+1<len&&!(t[e2]==m&&t[e2+1]==m))e2++;
            if(e2+1<len&&e2>s2){ sb_append(sb,"<strong>"); parse_inline_impl(sb,t+s2,e2-s2,currentFile,refs,linkAllowed); sb_append(sb,"</strong>"); i=e2+2; continue; }
        }
        /* Italic *text* */
        if (t[i]=='*'&&i+1<len&&t[i+1]!=t[i]&&t[i+1]!=' ') {
            char m=t[i]; size_t s1=i+1,e1=s1; while(e1<len&&t[e1]!=m)e1++;
            if(e1<len&&e1>s1&&t[e1-1]!=' '){ sb_append(sb,"<em>"); parse_inline_impl(sb,t+s1,e1-s1,currentFile,refs,linkAllowed); sb_append(sb,"</em>"); i=e1+1; continue; }
        }
        if (linkAllowed && (consumed=append_autolink(sb,t,len,i,1))!=0) {
            i+=consumed;
            continue;
        }
        /* Limited emoji shortcodes like :smile: */
        if (t[i] == ':') {
            size_t j = i + 1;
            while (j < len &&
                   ((t[j] >= 'a' && t[j] <= 'z') ||
                    (t[j] >= 'A' && t[j] <= 'Z') ||
                    (t[j] >= '0' && t[j] <= '9') ||
                    t[j] == '_' || t[j] == '+' || t[j] == '-')) {
                ++j;
            }
            if (j > i + 1 && j < len && t[j] == ':') {
                const char* emoji = lookup_emoji_shortcode(t + i + 1, j - (i + 1));
                if (emoji) {
                    sb_append(sb, "<span class=\"mdv-emoji\">");
                    sb_append(sb, emoji);
                    sb_append(sb, "</span>");
                    i = j + 1;
                    continue;
                }
            }
        }
        /* Plain char */
        sb_append_esc(sb,&t[i],1); i++;
    }
}

static void parse_inline(StrBuf* sb, const char* t, size_t len, const char* currentFile,
                         const MdReferenceMap* refs) {
    parse_inline_impl(sb,t,len,currentFile,refs,1);
}

/* --- Markdown Block Parser Helpers --- */

static int count_leading(const char* l, char c) { int n=0; while(l[n]==c)n++; return n; }
typedef struct { char** lines; int count; } Lines;
#include "mdview_highlight.inc"
static void free_lines(Lines* l);
static Lines split_lines(const char* text) {
    Lines r; r.count=0; int cap=256; r.lines=(char**)malloc(cap*sizeof(char*));
    if (!r.lines) return r;
    const char* p=text;
    while(*p){ const char* eol=p; while(*eol&&*eol!='\r'&&*eol!='\n')eol++;
        size_t ll=eol-p;
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
        r.lines[r.count++]=line; p=eol; if(*p=='\r')p++; if(*p=='\n')p++;
    } return r;
}
static void free_lines(Lines* l) { for(int i=0;i<l->count;i++) free(l->lines[i]); free(l->lines); }

static void collect_reference_definitions(const Lines* lines, MdReferenceMap* map, const MdFootnotes* omit) {
    int i, note=0;
    if (!map) return;
    ZeroMemory(map, sizeof(*map));
    if (!lines) return;
    for (i = 0; i < lines->count; ++i) {
        char* label = NULL;
        char* url = NULL;
        if (omit && note<omit->count && omit->items[note].firstLine==i) {
            i=omit->items[note++].lastLine;
            continue;
        }
        if (!parse_reference_definition_line(lines->lines[i], &label, &url)) continue;
        if (map->count >= map->cap) {
            int newCap = map->cap ? map->cap * 2 : 16;
            MdReference* newRefs = (MdReference*)realloc(map->refs, newCap * sizeof(MdReference));
            if (!newRefs) {
                free(label);
                free(url);
                continue;
            }
            map->refs = newRefs;
            map->cap = newCap;
        }
        map->refs[map->count].label = label;
        map->refs[map->count].url = url;
        map->count++;
    }
}

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

static const char* skip_indent(const char* line) {
    while (*line==' ' || *line=='\t') ++line;
    return line;
}

static int get_indent(const char* l) {
    int columns=0;
    for (;*l==' ' || *l=='\t';++l) columns+=*l=='\t' ? 4-(columns%4) : 1;
    return columns;
}

static const char* strip_indent(const char* line, int columns) {
    int used=0;
    while (used<columns && (*line==' ' || *line=='\t')) {
        used+=*line=='\t' ? 4-(used%4) : 1;
        ++line;
    }
    return line;
}
static int is_ul(const char* t) { return(t[0]=='-'||t[0]=='*'||t[0]=='+')&&t[1]==' '; }
static int is_ol(const char* t) { int i=0; while(t[i]>='0'&&t[i]<='9')i++; if(i==0||i>9)return 0; if((t[i]=='.'||t[i]==')')&&t[i+1]==' ')return i+2; return 0; }
static int is_dash_separator(const char* t) { int n=0; while(*t==' ')t++; while(*t=='-'){n++;t++;} while(*t==' ')t++; return n>=2&&*t=='\0'; }
static int is_compact_changelog_entry(const char* t) {
    const char* p = t;
    if (!p || *p++ != '-' || !((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))) return 0;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) p++;
    return *p == ':';
}

static int is_same_level_list_line(const Lines* lines, int idx, int baseIndent, int ordered) {
    const char* l;
    int indent;
    const char* t;
    if (!lines || idx < 0 || idx >= lines->count) return 0;
    l = lines->lines[idx];
    indent = get_indent(l);
    t = skip_indent(l);
    if (indent > baseIndent + 1) return 0;
    return ordered ? (is_ol(t) != 0) : is_ul(t);
}

/* --- Markdown Block Parser --- */

static void sb_append_line_attr(StrBuf* sb, int line) {
    char tmp[48];
    _snprintf(tmp, sizeof(tmp), " data-mdv-line=\"%d\"", line);
    sb_append(sb, tmp);
}

static void sb_append_line_range_attr(StrBuf* sb, int startLine, int endLine) {
    char tmp[96];
    if (endLine < startLine) endLine = startLine;
    _snprintf(tmp, sizeof(tmp), " data-mdv-line=\"%d\" data-mdv-line-end=\"%d\"", startLine, endLine);
    sb_append(sb, tmp);
}

static int math_block_end(const Lines* lines, int i) {
    const char* text=skip_indent(lines->lines[i]);
    const char* end;
    if (text[0]!='$' || text[1]!='$' || get_indent(lines->lines[i])>=4) return -1;
    text+=2;
    while (*text==' ' || *text=='\t') ++text;
    if (*text) {
        end=strstr(text,"$$");
        if (!end || end==text) return -1;
        end+=2;
        while (*end==' ' || *end=='\t') ++end;
        return *end ? -1 : i;
    }
    for (int j=i+1;j<lines->count;j++) {
        const char* line=skip_indent(lines->lines[j]);
        if (!*line || strncmp(line,"```",3)==0 || strncmp(line,"~~~",3)==0) break;
        if (line[0]=='$' && line[1]=='$') {
            line+=2;
            while (*line==' ' || *line=='\t') ++line;
            if (!*line) return j;
        }
    }
    return -1;
}

static const char* footnote_definition(const char* line) {
    const char* p=skip_indent(line), *end;
    if (get_indent(line)>=4 || p[0]!='[' || p[1]!='^') return NULL;
    end=footnote_label_end(p+2,p+strlen(p));
    return end && end[1]==':' ? end : NULL;
}

static int fence_length(const char* p) {
    int n=(*p=='`' || *p=='~') ? count_leading(p,*p) : 0;
    return n>=3 ? n : 0;
}

static int fence_closes(const char* line, char marker, int openingLength) {
    const char* p=skip_indent(line);
    int n=count_leading(p,marker);
    return n>=openingLength && !*skip_indent(p+n);
}

static int atx_heading_level(const char* text) {
    int hashes=count_leading(text,'#');
    return hashes>=1 && hashes<=6 && (!text[hashes] || text[hashes]==' ' || text[hashes]=='\t') ? hashes : 0;
}

static int interrupts_lazy_text(const Lines* lines, int i) {
    const char* p=skip_indent(lines->lines[i]);
    return (get_indent(lines->lines[i])<4 && atx_heading_level(p)) ||
           *p=='>' || is_ul(p) || is_ol(p) || is_hr(p) || fence_length(p) ||
           html_block_name(p) || is_reference_definition_line(p) ||
           footnote_definition(lines->lines[i]) ||
           (i+1<lines->count && strchr(p,'|') && is_table_sep(lines->lines[i+1])) ||
           (*p=='$' && math_block_end(lines,i)>=i);
}

static void collect_footnotes(const Lines* lines, MdFootnotes* notes) {
    for (int i=0;i<lines->count;++i) {
        const char* line=lines->lines[i], *p=skip_indent(line), *labelEnd, *htmlTag;
        int fence=fence_length(p);
        int end;
        if (get_indent(line)>=4) continue;
        if (i==0 && strcmp(p,"---")==0) {
            for (end=1;end<lines->count && strcmp(skip_indent(lines->lines[end]),"---")!=0;++end) {}
            if (end<lines->count) { i=end; continue; }
        }
        if (fence) {
            do { ++i; } while (i<lines->count && !fence_closes(lines->lines[i],*p,fence));
            continue;
        }
        if ((htmlTag=html_block_name(p))!=NULL) {
            int depth=html_block_delta(line,htmlTag);
            while (depth>0 && i+1<lines->count) depth+=html_block_delta(lines->lines[++i],htmlTag);
            continue;
        }
        if (strncmp(p,"<!--",4)==0) {
            while (!strstr(lines->lines[i],"-->") && i+1<lines->count) ++i;
            continue;
        }
        end=math_block_end(lines,i);
        if (end>=i) { i=end; continue; }
        labelEnd=footnote_definition(line);
        if (labelEnd) {
            MdFootnote note={0};
            StrBuf content;
            int next;
            const char* body=skip_indent(labelEnd+2);
            note.label=normalize_reference_label(p+2,labelEnd-(p+2));
            if (!note.label) break;
            if (!sb_init(&content)) { free(note.label); break; }
            sb_append(&content,body);
            for (next=i+1;next<lines->count;++next) {
                const char* part=lines->lines[next];
                if (!*skip_indent(part)) {
                    int after=next+1;
                    while (after<lines->count && !*skip_indent(lines->lines[after])) ++after;
                    if (after==lines->count || get_indent(lines->lines[after])<4) break;
                    while (next<after) { sb_append_char(&content,'\n'); ++next; }
                    part=lines->lines[next];
                }
                if (get_indent(part)>=4) part=strip_indent(part,4);
                else if (!content.len || interrupts_lazy_text(lines,next)) break;
                sb_append_char(&content,'\n');
                sb_append(&content,part);
            }
            if (notes->count==notes->capacity) {
                int capacity=notes->capacity ? notes->capacity*2 : 16;
                MdFootnote* items=(MdFootnote*)realloc(notes->items,capacity*sizeof(*items));
                if (!items) { free(content.data); free(note.label); break; }
                notes->items=items;
                notes->capacity=capacity;
            }
            note.text=content.data;
            note.firstLine=i;
            note.lastLine=next-1;
            notes->items[notes->count++]=note;
            i=next-1;
        }
    }
    if (notes->count) notes->order=(int*)malloc(notes->count*sizeof(*notes->order));
}

static const char* quote_content(const char* line) {
    const char* p=skip_indent(line);
    if (*p!='>' || get_indent(line)>=4) return NULL;
    return p[1]==' ' || p[1]=='\t' ? p+2 : p+1;
}

static const char* alert_title(const char* line) {
    static const char* titles[]={"Note","Tip","Important","Warning","Caution"};
    if (line[0]!='[' || line[1]!='!') return NULL;
    for (int i=0;i<(int)_countof(titles);++i) {
        size_t n=strlen(titles[i]);
        if (_strnicmp(line+2,titles[i],n)==0 && line[2+n]==']' &&
            !*skip_indent(line+3+n)) return titles[i];
    }
    return NULL;
}

static char* md_to_html_inner(const char* markdown, const char* currentFile,
                              const MdReferenceMap* parent, int offset) {
    StrBuf sb; sb_init(&sb);
    Lines lines = split_lines(markdown);
    MdReferenceMap refs;
    MdFootnotes localNotes={0};
    if (!sb.data || !lines.lines) {
        if (sb.data) free(sb.data);
        if (lines.lines) free_lines(&lines);
        return NULL;
    }
    if (!parent) collect_footnotes(&lines,&localNotes);
    collect_reference_definitions(&lines,&refs,parent ? NULL : &localNotes);
    refs.notes=parent ? parent->notes : &localNotes;
    refs.parent=parent;
    int noteCursor=0;
    int i = 0;

    while (i < lines.count) {
        int limit=noteCursor<localNotes.count ? localNotes.items[noteCursor].firstLine : lines.count;
        if (i==limit) {
            i=localNotes.items[noteCursor++].lastLine+1;
            continue;
        }
        const char* line = lines.lines[i];
        int indent = get_indent(line);
        const char* tr = skip_indent(line);

        if (tr[0]=='\0' || is_reference_definition_line(tr)) { i++; continue; }

        /* YAML front matter at document start */
        if (i == 0 && strcmp(tr, "---") == 0) {
            int j = i + 1;
            while (j < limit) {
                const char* endLine = lines.lines[j];
                const char* endTr = skip_indent(endLine);
                if (strcmp(endTr, "---") == 0) break;
                j++;
            }
            if (j < limit) {
                int blockStart = i;
                sb_append(&sb, "<pre"); sb_append_line_range_attr(&sb, offset+(blockStart), offset+(j)); sb_append(&sb, "><code class=\"language-yaml\">");
                i++;
                while (i < j) {
                    if (sb.data[sb.len - 1] != '>') sb_append(&sb, "\n");
                    sb_append_esc(&sb, lines.lines[i], strlen(lines.lines[i]));
                    i++;
                }
                sb_append(&sb, "</code></pre>\n");
                i++; /* consume closing --- */
                continue;
            }
        }

        if (tr[0]=='$' && tr[1]=='$') {
            int end=math_block_end(&lines,i);
            if (end>=i) {
                StrBuf formula;
                sb_init(&formula);
                if (end==i) {
                    const char* close=strstr(tr+2,"$$");
                    for (const char* ch=tr+2;ch<close;++ch) sb_append_char(&formula,*ch);
                } else {
                    for (int j=i+1;j<end;j++) {
                        sb_append(&formula,lines.lines[j]);
                        sb_append_char(&formula,'\n');
                    }
                }
                sb_append(&sb,"<div class=\"mdv-math-display\"");
                sb_append_line_range_attr(&sb, offset+(i), offset+(end));
                sb_append(&sb,">");
                if (!append_math(&sb,formula.data,formula.len,1)) {
                    sb_append(&sb,"<pre><code>");
                    for (int j=i;j<=end;j++) {
                        if (j>i) sb_append_char(&sb,'\n');
                        sb_append_esc(&sb,lines.lines[j],strlen(lines.lines[j]));
                    }
                    sb_append(&sb,"</code></pre>");
                }
                sb_append(&sb,"</div>\n");
                free(formula.data);
                i=end+1;
                continue;
            }
        }

        /* Fenced code block */
        int fence=fence_length(tr);
        if (fence && indent<4) {
            char fc=tr[0]; const char* lang=skip_indent(tr+fence);
            const char* langEnd=lang+strcspn(lang," \t`~");
            CodeLanguage lexer=code_language(lang,langEnd-lang);
            CodeLexer state={0};
            int isMermaid = is_mermaid_lang(lang);
            int blockStart = i;
            StrBuf mermaid;
            StrBuf code;
            if (isMermaid) {
                sb_init(&mermaid);
            } else {
                sb_init(&code);
            }
            i++;
            while(i<limit){ const char* cl=lines.lines[i];
                if(fence_closes(cl,fc,fence)){i++;break;}
                if (isMermaid) {
                    if (mermaid.len > 0) sb_append(&mermaid,"\n");
                    sb_append(&mermaid,cl);
                } else {
                    char next=0;
                    if (i>blockStart+1) sb_append_char(&code,'\n');
                    if ((lexer==CODE_JSON || lexer==CODE_JSONC) && strchr(cl,'"'))
                        next=code_next_nonspace(&lines,i+1,limit);
                    highlight_code_line(&code,cl,cl+strlen(cl),lexer,&state,0,next);
                }
                i++;
            }
            if (isMermaid) {
                append_mermaid_block(&sb,mermaid.data,mermaid.len,offset+blockStart,offset+(i>blockStart ? i-1 : blockStart));
                free(mermaid.data);
            } else {
                sb_append(&sb,"<pre"); sb_append_line_range_attr(&sb, offset+(blockStart), offset+(i > blockStart ? i - 1 : blockStart)); sb_append(&sb, "><code");
                if(langEnd>lang){ sb_append(&sb," class=\"language-"); sb_append_esc(&sb,lang,langEnd-lang); sb_append(&sb,"\""); }
                if(lexer!=CODE_PLAIN) sb_append(&sb," data-mdv-highlighted=\"1\"");
                sb_append(&sb,">");
                sb_append(&sb, code.data ? code.data : "");
                sb_append(&sb,"</code></pre>\n");
                free(code.data);
            }
            continue;
        }

        /* Indented code block */
        if (indent>=4 && !is_ul(tr) && !is_ol(tr)) {
            StrBuf code;
            int blockStart = i;
            sb_init(&code);
            while(i<limit){ const char* cl=lines.lines[i];
                if(cl[0]=='\0'){if(i+1<limit&&get_indent(lines.lines[i+1])>=4){sb_append(&code,"\n");i++;continue;}break;}
                if(get_indent(cl)<4)break;
                if(code.len>0) sb_append(&code,"\n");
                const char* content=strip_indent(cl,4);
                sb_append_esc(&code,content,strlen(content)); i++;
            }
            sb_append(&sb,"<pre"); sb_append_line_range_attr(&sb, offset+(blockStart), offset+(i > blockStart ? i - 1 : blockStart)); sb_append(&sb, "><code>");
            sb_append(&sb, code.data ? code.data : "");
            sb_append(&sb,"</code></pre>\n");
            free(code.data);
            continue;
        }

        /* ATX headings and paragraph interruptions use the same recognition rules. */
        {
            int level=atx_heading_level(tr);
            if (level) {
                const char* content=skip_indent(tr+level);
                const char* end=content+strlen(content);
                const char* closing;
                while (end>content && (end[-1]==' ' || end[-1]=='\t')) --end;
                closing=end;
                while (closing>content && closing[-1]=='#') --closing;
                if (closing<end && (closing==content || closing[-1]==' ' || closing[-1]=='\t')) {
                    end=closing;
                    while (end>content && (end[-1]==' ' || end[-1]=='\t')) --end;
                }
                char tag[4], idnum[24];
                _snprintf(tag,sizeof(tag),"h%d",level);
                _snprintf(idnum,sizeof(idnum),"%d",offset+i);
                sb_append(&sb,"<"); sb_append(&sb,tag); sb_append(&sb," id=\"mdv-h");
                sb_append(&sb,idnum); sb_append(&sb,"\"");
                sb_append_line_attr(&sb,offset+i); sb_append(&sb,">");
                parse_inline(&sb,content,end-content,currentFile,&refs);
                sb_append(&sb,"</"); sb_append(&sb,tag); sb_append(&sb,">\n");
                ++i; continue;
            }
        }

        /* Setext headings */
        if (i+1<limit && tr[0]!='\0') {
            const char* nx=lines.lines[i+1]; while(*nx==' ')nx++; int nl=(int)strlen(nx);
            if(nl>=1){ int ae=1,ad=1; for(int j=0;j<nl;j++){if(nx[j]!='=')ae=0;if(nx[j]!='-')ad=0;}
                if(ae){ sb_append(&sb,"<h1"); sb_append_line_attr(&sb, offset+i); sb_append(&sb, ">"); parse_inline(&sb,tr,strlen(tr),currentFile,&refs); sb_append(&sb,"</h1>\n"); i+=2; continue; }
                if(ad&&!is_hr(lines.lines[i+1])){ sb_append(&sb,"<h2"); sb_append_line_attr(&sb, offset+i); sb_append(&sb, ">"); parse_inline(&sb,tr,strlen(tr),currentFile,&refs); sb_append(&sb,"</h2>\n"); i+=2; continue; }
            }
        }

        /* HR */
        if (is_hr(line)) { sb_append(&sb,"<hr"); sb_append_line_attr(&sb, offset+i); sb_append(&sb, ">\n"); i++; continue; }

        /* Top-level GitHub alerts share blockquote parsing with ordinary quotes. */
        const char* quoted=quote_content(line);
        if (quoted) {
            const char* alert=parent ? NULL : alert_title(quoted);
            int blockLine=i, bodyLine=i+(alert!=NULL), previousContent=0;
            StrBuf bq;
            sb_init(&bq);
            if (alert) ++i;
            while (i<limit) {
                const char* body=quote_content(lines.lines[i]);
                if (body) {
                    if (alert && alert_title(body)) break;
                } else {
                    if (alert || !previousContent || !*skip_indent(lines.lines[i]) ||
                        interrupts_lazy_text(&lines,i)) break;
                    body=lines.lines[i];
                }
                if (i>bodyLine) sb_append_char(&bq,'\n');
                sb_append(&bq,body);
                previousContent=*skip_indent(body)!=0;
                ++i;
            }
            char* inner=bq.data ? md_to_html_inner(bq.data,currentFile,&refs,offset+bodyLine) : NULL;
            if (alert) {
                sb_append(&sb,"<div class=\"mdv-alert mdv-alert-");
                sb_append(&sb,alert);
                sb_append(&sb,"\" role=\"note\"");
            } else sb_append(&sb,"<blockquote");
            sb_append_line_range_attr(&sb, offset+(blockLine), offset+(i-1));
            sb_append(&sb,">\n");
            if (alert) {
                sb_append(&sb,"<div class=\"mdv-alert-title\">");
                sb_append(&sb,alert);
                sb_append(&sb,"</div>\n");
            }
            if (inner) sb_append(&sb,inner);
            sb_append(&sb,alert ? "</div>\n" : "</blockquote>\n");
            free(inner);
            free(bq.data);
            continue;
        }

        /* Preserve supported HTML containers, including their source-line range. */
        const char* htmlTag=html_block_name(tr);
        if (htmlTag && indent<4) {
            int blockStart=i, depth=0;
            StrBuf rawHtml;
            sb_init(&rawHtml);
            while (i<limit) {
                const char* raw=lines.lines[i++];
                depth+=html_block_delta(raw,htmlTag);
                sb_append(&rawHtml,raw);
                sb_append_char(&rawHtml,'\n');
                if (depth<=0) break;
            }
            sb_append(&sb,"<div class=\"mdv-src-block\"");
            sb_append_line_range_attr(&sb, offset+(blockStart), offset+(i-1));
            sb_append(&sb,">");
            append_html_block(&sb,rawHtml.data,rawHtml.len,currentFile);
            sb_append(&sb,"</div>\n");
            free(rawHtml.data);
            continue;
        }

        /* Table */
        if (i+1<limit && is_table_sep(lines.lines[i+1])) {
            char cells[64][1024]; char al[64]; memset(al,'l',sizeof(al));
            int nc=parse_trow(line,cells,64); parse_talign(lines.lines[i+1],al,64);
            sb_append(&sb,"<table"); sb_append_line_attr(&sb, offset+i); sb_append(&sb, ">\n<thead>\n<tr"); sb_append_line_attr(&sb, offset+i); sb_append(&sb, ">\n");
            for(int c=0;c<nc;c++){
                sb_append(&sb,"<th"); if(al[c]=='c')sb_append(&sb," style=\"text-align:center\""); else if(al[c]=='r')sb_append(&sb," style=\"text-align:right\"");
                sb_append(&sb,">"); parse_inline(&sb,cells[c],strlen(cells[c]),currentFile,&refs); sb_append(&sb,"</th>\n");
            }
            sb_append(&sb,"</tr>\n</thead>\n<tbody>\n"); i+=2;
            while(i<limit){ const char* rl=lines.lines[i]; while(*rl==' ')rl++;
                if(rl[0]=='\0'||!strchr(rl,'|'))break;
                int rc=parse_trow(lines.lines[i],cells,64); sb_append(&sb,"<tr"); sb_append_line_attr(&sb, offset+i); sb_append(&sb, ">\n");
                for(int c=0;c<nc;c++){
                    sb_append(&sb,"<td"); if(al[c]=='c')sb_append(&sb," style=\"text-align:center\""); else if(al[c]=='r')sb_append(&sb," style=\"text-align:right\"");
                    sb_append(&sb,">"); if(c<rc)parse_inline(&sb,cells[c],strlen(cells[c]),currentFile,&refs); sb_append(&sb,"</td>\n");
                }
                sb_append(&sb,"</tr>\n"); i++;
            }
            sb_append(&sb,"</tbody>\n</table>\n"); continue;
        }

        /* Lists */
        if (is_ul(tr) || is_ol(tr)) {
            int ordered=is_ol(tr); int bi=indent;
            sb_append(&sb,ordered?"<ol":"<ul"); sb_append_line_attr(&sb, offset+i); sb_append(&sb, ">\n");
            while(i<limit){
                const char* ll=lines.lines[i]; int li=get_indent(ll); const char* lt=skip_indent(ll);
                int iu=is_ul(lt)&&li<=bi+1; int om=is_ol(lt); int io=om&&li<=bi+1;
                if(lt[0]=='\0'){i++;continue;}
                if(!iu&&!io&&li<=bi)break;
                if(iu||io){
                    const char* ic=iu?lt+2:lt+om;
                    int task=0,chk=0;
                    if(strncmp(ic,"[ ] ",4)==0){task=1;ic+=4;}
                    else if(strncmp(ic,"[x] ",4)==0||strncmp(ic,"[X] ",4)==0){task=1;chk=1;ic+=4;}
                    sb_append(&sb,"<li"); sb_append_line_attr(&sb, offset+i); sb_append(&sb, ">");
                    if(task) sb_append(&sb,chk?"<input type=\"checkbox\" checked disabled> ":"<input type=\"checkbox\" disabled> ");
                    parse_inline(&sb,ic,strlen(ic),currentFile,&refs); i++;
                    StrBuf nest; sb_init(&nest); int hn=0, nestLine=i;
                    while(i<limit){ const char* nl=lines.lines[i]; int ni=get_indent(nl); const char* nt=skip_indent(nl);
                        if(nt[0]=='\0'){if(i+1<limit&&get_indent(lines.lines[i+1])>bi+1){sb_append(&nest,"\n");i++;hn=1;continue;}break;}
                        if(ni>bi+1){if(nest.len>0)sb_append(&nest,"\n");sb_append(&nest,nl);hn=1;i++;}else break;
                    }
                    while(ordered && i<limit && !is_same_level_list_line(&lines, i, bi, ordered)){
                        const char* nl=lines.lines[i]; int ni=get_indent(nl); const char* nt=skip_indent(nl);
                        if(nt[0]=='\0') break;
                        if(ni>bi+1) break;
                        if(is_ul(nt) || is_ol(nt) || nt[0]=='#' || is_hr(nl) ||
                           is_dash_separator(nt) ||
                           (nt[0]=='>' && (nt[1]==' ' || nt[1]=='\0')) ||
                           strncmp(nt,"```",3)==0 || strncmp(nt,"~~~",3)==0 ||
                           html_block_name(nt)!=NULL ||
                           (i+1<limit && is_table_sep(lines.lines[i+1]))) break;
                        if(nest.len>0)sb_append(&nest,"\n");
                        sb_append(&nest,nl);
                        hn=1; i++; continue;
                    }
                    if(hn){char* nh=md_to_html_inner(nest.data,currentFile,&refs,offset+nestLine);if(nh) {sb_append(&sb,"\n");sb_append(&sb,nh);free(nh);}}
                    free(nest.data); sb_append(&sb,"</li>\n");
                } else i++;
            }
            sb_append(&sb,ordered?"</ol>\n":"</ul>\n"); continue;
        }

        /* Paragraph */
        { StrBuf para; int paraLine = i; sb_init(&para);
            int previousCompactEntry = 0;
            while(i<limit){
                const char* pl=lines.lines[i]; int pi=get_indent(pl); const char* pt=skip_indent(pl);
                int compactEntry;
                if(!pt[0] || (pi<4 && atx_heading_level(pt)) || is_hr(pl))break;
                if(quote_content(pl))break;
                if(pi<4 && fence_length(pt))break;
                if((pi<4 && html_block_name(pt)) || (pt[0]=='$' && math_block_end(&lines,i)>=0))break;
                if(is_ul(pt)||is_ol(pt))break;
                if(i+1<limit&&strchr(pt,'|')&&is_table_sep(lines.lines[i+1]))break;
                compactEntry = is_compact_changelog_entry(pt);
                if(para.len>0) sb_append(&para, previousCompactEntry && compactEntry ? "  \n" : "\n");
                sb_append(&para,tr); i++;
                previousCompactEntry = compactEntry;
                if(i<limit){ const char* nx=lines.lines[i]; while(*nx==' ')nx++; int nl2=(int)strlen(nx);
                    if(nl2>=1){int ae=1,ad=1;for(int j=0;j<nl2;j++){if(nx[j]!='=')ae=0;if(nx[j]!='-')ad=0;}if(ae||ad)break;}
                    tr=skip_indent(lines.lines[i]);
                }
            }
            sb_append(&sb,"<p"); sb_append_line_range_attr(&sb, offset+(paraLine), offset+(i > paraLine ? i - 1 : paraLine)); sb_append(&sb, ">"); parse_inline(&sb,para.data,para.len,currentFile,&refs); sb_append(&sb,"</p>\n");
            free(para.data);
        }
    }
    /* Render note bodies before writing backlinks. References inside notes can
       introduce another note or add a backlink to one already in the list. */
    if (localNotes.used) {
        for (int n=0;n<localNotes.used;++n) {
            MdFootnote* note=&localNotes.items[localNotes.order[n]];
            note->html=md_to_html_inner(note->text,currentFile,&refs,offset+note->firstLine);
        }
        sb_append(&sb,"<div class=\"mdv-footnotes\" role=\"doc-endnotes\"><div class=\"mdv-footnotes-title\">Footnotes</div><ol>\n");
        for (int n=0;n<localNotes.used;++n) {
            const MdFootnote* note=&localNotes.items[localNotes.order[n]];
            char tag[180];
            sprintf_s(tag,sizeof(tag),"<li id=\"mdv-fn.%d\"",note->number);
            sb_append(&sb,tag);
            sb_append_line_range_attr(&sb,offset+note->firstLine,offset+note->lastLine);
            sb_append(&sb,">");
            if (note->html) sb_append(&sb,note->html);
            for (int r=1;r<=note->referenceCount;++r) {
                sprintf_s(tag,sizeof(tag),"<a class=\"mdv-footnote-back\" href=\"#mdv-fnref.%d.%d\" aria-label=\"Back to reference %d\">&#8617;</a>",
                          note->number,r,r);
                sb_append(&sb,tag);
            }
            sb_append(&sb,"</li>\n");
        }
        sb_append(&sb,"</ol></div>\n");
    }
    for (int n=0;n<localNotes.count;++n) {
        free(localNotes.items[n].label);
        free(localNotes.items[n].text);
        free(localNotes.items[n].html);
    }
    free(localNotes.items);
    free(localNotes.order);
    free_reference_map(&refs);
    free_lines(&lines);
    return sb.data;
}

static char* md_to_html(const char* markdown, const char* currentFile) {
    return md_to_html_inner(markdown,currentFile,NULL,0);
}

/* --- Theme Detection --- */

static char* md_to_raw_html(const char* markdown) {
    StrBuf sb;
    Lines lines = split_lines(markdown ? markdown : "");
    CodeLanguage language=CODE_PLAIN;
    CodeLexer state={0};
    char marker=0;
    int fence=0;
    if (!sb_init(&sb) || !lines.lines) {
        if (sb.data) free(sb.data);
        if (lines.lines) free_lines(&lines);
        return NULL;
    }

    for (int i = 0; i < lines.count; ++i) {
        char tmp[96];
        const char* line=lines.lines[i];
        const char* end=line+strlen(line);
        const char* p=skip_indent(line);
        _snprintf(tmp, sizeof(tmp), "<span class=\"mdv-raw-line\" data-raw-line=\"%d\">", i);
        sb_append(&sb, tmp);
        if (fence) {
            if (fence_closes(line,marker,fence)) {
                code_token(&sb,line,end,"sh-pp",1);
                fence=0;
            } else {
                char next=0;
                if ((language==CODE_JSON || language==CODE_JSONC) && strchr(line,'"'))
                    next=code_next_nonspace(&lines,i+1,lines.count);
                highlight_code_line(&sb,line,end,language,&state,1,next);
            }
        } else if (get_indent(line)<4 && (fence=fence_length(p))!=0) {
            const char* name=skip_indent(p+fence);
            language=code_language(name,strcspn(name," \t`~"));
            marker=*p;
            ZeroMemory(&state,sizeof(state));
            code_token(&sb,line,end,"sh-pp",1);
        } else {
            int hashes=count_leading(p,'#'), ordered=is_ol(p);
            if (hashes>0 && hashes<=6 && (!p[hashes] || p[hashes]==' ' || p[hashes]=='\t'))
                code_token(&sb,line,end,"sh-md-heading",1);
            else {
                if (*p=='>' || is_ul(p) || ordered) {
                    code_text(&sb,line,p,2);
                    code_token(&sb,p,p+(ordered ? ordered-1 : 1),"sh-kw",1);
                    p+=ordered ? ordered-1 : 1;
                } else p=line;
                while (p<end) {
                    const char* tick=strchr(p,'`'), *close=tick ? strchr(tick+1,'`') : NULL;
                    if (!close) { code_text(&sb,p,end,p==line ? 2 : 1); break; }
                    code_text(&sb,p,tick,p==line ? 2 : 1);
                    code_token(&sb,tick,close+1,"sh-str",1);
                    p=close+1;
                }
            }
        }
        sb_append(&sb, "</span>");
    }

    free_lines(&lines);
    return sb.data;
}

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

static int resolve_color_theme(int flags) {
    switch (g_settings.colorTheme) {
    case 1: return 0;
    case 2: return 1;
    case 3:
        if (g_hasListerHost || (flags & LCP_DARKMODE)) return (flags & LCP_DARKMODE)!=0;
        break;
    }
    return is_dark_theme();
}

#include "mdview_theme.inc"

static int append_theme_menu(HMENU parent, ThemeChoice* themes) {
    static const wchar_t* labels[]={L"Follow Windows",L"Always light",L"Always dark",L"Follow Total Commander"};
    HMENU menu=CreatePopupMenu(), palette[2];
    WIN32_FIND_DATAW info;
    wchar_t path[MAX_PATH];
    HANDLE find;
    int count=2;
    if (!menu) return 0;
    for (int i=0;i<4;++i)
        AppendMenuW(menu,MF_STRING | (i==g_settings.colorTheme ? MF_CHECKED : 0),IDM_COLOR_THEME+i,labels[i]);
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    for (int dark=0;dark<2;++dark) {
        palette[dark]=CreatePopupMenu();
        themes[dark].dark=dark; themes[dark].name[0]=0;
        if (palette[dark]) {
            AppendMenuW(palette[dark],MF_STRING | (!g_settings.themeFile[dark][0] ? MF_CHECKED : 0),
                        IDM_PALETTE+dark,L"Built-in");
            AppendMenuW(menu,MF_POPUP,(UINT_PTR)palette[dark],dark?L"Dark palette":L"Light palette");
        }
    }
    if (theme_directory(path) && wcscat_s(path,MAX_PATH,L"*.json")==0) {
        find=FindFirstFileW(path,&info);
        if (find!=INVALID_HANDLE_VALUE) {
            do {
                char* text;
                int dark;
                if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                text=theme_read(info.cFileName);
                dark=theme_type(theme_root(text));
                free(text);
                if (dark<0 || !palette[dark]) continue;
                wcscpy_s(themes[count].name,MAX_PATH,info.cFileName);
                themes[count].dark=dark;
                AppendMenuW(palette[dark],MF_STRING |
                    (_wcsicmp(g_settings.themeFile[dark],info.cFileName)==0 ? MF_CHECKED : 0),
                    IDM_PALETTE+count,info.cFileName);
                ++count;
            } while (count<MDV_MAX_PALETTES && FindNextFileW(find,&info));
            FindClose(find);
        }
    }
    AppendMenuW(parent,MF_POPUP,(UINT_PTR)menu,L"Color theme");
    return count;
}

static void refresh_color_theme(MDViewData* d) {
    int dark=resolve_color_theme(d->listerFlags);
    if (d->paletteRevision!=g_paletteRevision) {
        StrBuf script;
        if (sb_init(&script)) {
            wchar_t* w;
            sb_append(&script,"mdvPalette('");
            append_theme_css(&script,0); append_theme_css(&script,1);
            sb_append(&script,"')");
            w=utf8_to_wide_dup(script.data);
            if (w) {
                exec_js(d->pBrowser,w);
                free(w);
                d->paletteRevision=g_paletteRevision;
            }
            free(script.data);
        }
    }
    if (dark!=d->darkTheme) {
        exec_js(d->pBrowser,dark ? L"mdvSetTheme(1)" : L"mdvSetTheme(0)");
        d->darkTheme=dark;
    }
}

static void set_color_theme(MDViewData* d, int mode) {
    g_settings.colorTheme=mode;
    save_setting_int("colorTheme",mode);
    refresh_color_theme(d);
}

static void toggle_color_theme(MDViewData* d) {
    set_color_theme(d,d->darkTheme ? 1 : 2);
}

static void apply_theme_command(MDViewData* d, int cmd, const ThemeChoice* themes, int count) {
    if (cmd>=IDM_COLOR_THEME && cmd<IDM_COLOR_THEME+4) set_color_theme(d,cmd-IDM_COLOR_THEME);
    else if (cmd>=IDM_PALETTE && cmd<IDM_PALETTE+count) {
        const ThemeChoice* theme=&themes[cmd-IDM_PALETTE];
        wchar_t ini[MAX_PATH];
        wcscpy_s(g_settings.themeFile[theme->dark],MAX_PATH,theme->name);
        if (g_iniPath[0] && MultiByteToWideChar(CP_ACP,0,g_iniPath,-1,ini,MAX_PATH))
            WritePrivateProfileStringW(L"MDView",theme->dark?L"darkTheme":L"lightTheme",theme->name,ini);
        ++g_paletteRevision;
        refresh_color_theme(d);
    }
}

/* --- CSS --- */

static void build_css(StrBuf* sb) {
    sb_append(sb,
    "*{box-sizing:border-box}"
    "html{background:#fff;height:100%;width:100%}html.mdv-dark{background:#1e1e1e}");

    /* Body - full viewport background */
    sb_append(sb, "body{font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;");
    { char tmp[64]; _snprintf(tmp, sizeof(tmp), "font-size:%dpx;", g_settings.fontSize); sb_append(sb, tmp); }
    sb_append(sb, "line-height:1.7;color:#24292e;background:#fff;margin:0;padding:0;"
    "transition:background .2s,color .2s}");
    sb_append(sb, "body.dark{color:#d4d4d4;background:#1e1e1e}");

    sb_append(sb,
    "#mdv-layout{min-height:100vh}"
    "#mdv-render-pane{min-width:0}"
    "#mdv-raw-pane{display:none;margin:0;border:0;border-radius:0;background:#fff;color:#24292e;"
    "white-space:pre;word-wrap:normal;line-height:1.45;padding:12px 14px 42px}"
    "body.mdv-split,body.mdv-code{height:100%;overflow:hidden}"
    "body.mdv-split #mdv-layout,body.mdv-code #mdv-layout{position:relative;height:100%;min-height:0}"
    "body.mdv-split #mdv-render-pane{position:absolute;top:0;bottom:0;left:0;width:50%;overflow:auto;border-right:1px solid #d0d7de}"
    "body.dark.mdv-split #mdv-render-pane{border-right-color:#444}"
    "body.mdv-split #mdv-raw-pane,body.mdv-code #mdv-raw-pane{display:block;position:absolute;top:0;bottom:0;left:50%;right:0;overflow:auto}"
    "body.mdv-code #mdv-raw-pane{left:0}"
    "body.mdv-code #mdv-render-pane{display:none}");
    { char tmp[192]; char fontA[96];
      WideCharToMultiByte(CP_ACP, 0, g_settings.rawFontName, -1, fontA, sizeof(fontA), NULL, NULL);
      _snprintf(tmp, sizeof(tmp), "#mdv-raw-pane{font-family:'%s',Consolas,'Courier New',monospace;font-size:%dpt;}", fontA, g_settings.rawFontSize);
      sb_append(sb, tmp);
    }
    sb_append(sb,
    "body.dark #mdv-raw-pane{background:#1e1e1e;color:#d4d4d4}"
    ".mdv-raw-line{display:block;min-height:1.45em}"
    ".mdv-source-tab{display:inline-block;width:2.4em}"
    "body.mdv-wrap #mdv-raw-pane{white-space:pre-wrap;word-wrap:break-word;overflow-x:hidden}"
    "body.mdv-wrap.mdv-legacy-wrap #mdv-raw-pane{white-space:normal}"
    "body.mdv-wrap.mdv-legacy-wrap .mdv-raw-line{width:100%;word-wrap:break-word}"
    "body.mdv-wrap.mdv-legacy-wrap .mdv-source-space{display:inline-block;white-space:pre}"
    ".mdv-raw-hit{background:#ffe08a;color:#24292e}"
    "body.dark .mdv-raw-hit{background:#6b5b00;color:#fff7cc}"
    ".mdv-sync-hit{outline:2px solid #d73a49;background:#fff3f3}"
    "body.dark .mdv-sync-hit{outline-color:#ff7b72;background:#3a2424}"
    "@media print{#mdv-raw-char,body.mdv-split #mdv-raw-pane{display:none!important}"
    "body.mdv-split,body.mdv-code{height:auto;overflow:visible}"
    "body.mdv-split #mdv-layout,body.mdv-code #mdv-layout{height:auto}"
    "body.mdv-split #mdv-render-pane,body.mdv-code #mdv-raw-pane{position:static;width:auto;height:auto;overflow:visible;border:0}}");

    /* Content container - centered, optional max-width */
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
    ".mdv-mermaid-view{display:none;margin-top:10px;padding:16px;background:#fff;border:1px solid transparent;border-radius:6px;overflow:auto;font-size:1em;text-align:center}"
    "body.dark .mdv-mermaid-view{background:#1e1e1e}"
    ".mdv-mermaid.ok .mdv-mermaid-view{display:block}"
    ".mdv-mermaid.ok .mdv-mermaid-src{display:none}"
    ".mdv-mermaid svg{display:block;width:auto;max-width:100%;height:auto;margin:0 auto;font-family:inherit;font-size:inherit}"
    ".mdv-mm-node{fill:#e1edf7;stroke:#0969da;stroke-width:1.5}"
    ".mdv-mm-marker{fill:#57606a;stroke:#57606a;stroke-width:1}"
    ".mdv-mm-marker.open{fill:none!important}"
    ".mdv-mm-marker.hollow,.mdv-mm-label{fill:#fff}"
    "body.dark .mdv-mm-marker{fill:#9da7b3;stroke:#9da7b3}"
    "body.dark .mdv-mm-marker.hollow,body.dark .mdv-mm-label{fill:#1e1e1e}"
    ".mdv-mm-edge{stroke:#57606a;stroke-width:1.6;fill:none}"
    ".mdv-mm-edge.dash{stroke-dasharray:6 4}"
    ".mdv-mm-edge.bold{stroke-width:3}"
    ".mdv-mm-grid{opacity:.27}"
    ".mdv-mm-excluded{stroke:none!important;opacity:.23}"
    ".mdv-mm-task-done{stroke-dasharray:4 2}"
    ".mdv-mm-task-active{stroke-width:2.5}"
    ".mdv-mm-chart-heading{font-weight:bold}"
    ".mdv-mm-accent{stroke:currentColor!important}"
    ".mdv-mm-data-fill{fill:currentColor;fill-opacity:.18}"
    ".mdv-mm-tree-group{fill-opacity:.08}"
    ".mdv-mm-radar-point{fill:currentColor}"
    ".mdv-mm-accent-0{color:#0969da}.mdv-mm-accent-1{color:#1a7f37}"
    ".mdv-mm-accent-2{color:#8250df}.mdv-mm-accent-3{color:#bc4c00}"
    "body.dark .mdv-mm-accent-0{color:#79c0ff}body.dark .mdv-mm-accent-1{color:#7ee787}"
    "body.dark .mdv-mm-accent-2{color:#d2a8ff}body.dark .mdv-mm-accent-3{color:#ffa657}"
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
    ".mdv-alert{margin:1em 0;padding:.5em 1em;border-left:4px solid #0969da}"
    ".mdv-alert-title{font-weight:600;margin:.1em 0 .4em;color:#0969da}"
    ".mdv-alert p{margin:.4em 0}"
    ".mdv-alert-Tip{border-color:#1a7f37}.mdv-alert-Tip .mdv-alert-title{color:#1a7f37}"
    ".mdv-alert-Important{border-color:#8250df}.mdv-alert-Important .mdv-alert-title{color:#8250df}"
    ".mdv-alert-Warning{border-color:#9a6700}.mdv-alert-Warning .mdv-alert-title{color:#9a6700}"
    ".mdv-alert-Caution{border-color:#cf222e}.mdv-alert-Caution .mdv-alert-title{color:#cf222e}"
    "body.dark .mdv-alert-Note{border-color:#58a6ff}body.dark .mdv-alert-Note .mdv-alert-title{color:#58a6ff}"
    "body.dark .mdv-alert-Tip{border-color:#3fb950}body.dark .mdv-alert-Tip .mdv-alert-title{color:#3fb950}"
    "body.dark .mdv-alert-Important{border-color:#a371f7}body.dark .mdv-alert-Important .mdv-alert-title{color:#a371f7}"
    "body.dark .mdv-alert-Warning{border-color:#e3b341}body.dark .mdv-alert-Warning .mdv-alert-title{color:#e3b341}"
    "body.dark .mdv-alert-Caution{border-color:#ff7b72}body.dark .mdv-alert-Caution .mdv-alert-title{color:#ff7b72}"
    ".mdv-footnotes{margin:2em 0 1em;padding-top:.6em;border-top:1px solid #d0d7de;font-size:.9em}"
    "body.dark .mdv-footnotes{border-top-color:#444}"
    ".mdv-footnotes-title{font-weight:600}.mdv-footnotes li p{margin:.3em 0}"
    ".mdv-footnote-back{margin-right:.4em}"

    "table{border-collapse:collapse;width:100%;margin:1em 0}"
    "body.mdv-fit table{table-layout:fixed}"
    "th,td{border:1px solid #dfe2e5;padding:8px 12px}"
    "body.mdv-fit th,body.mdv-fit td{overflow-wrap:anywhere}"
    "body.dark th,body.dark td{border-color:#444}"
    "th{background:#f6f8fa;font-weight:600}"
    "body.dark th{background:#2d2d2d}"
    "tr:nth-child(even){background:#f9f9f9}"
    "body.dark tr:nth-child(even){background:#252526}"
    "hr{border:none;border-top:1px solid #e1e4e8;margin:1.5em 0}"
    "body.dark hr{border-top-color:#444}"
    ".mdv-highlight,mark{background:#fff59d;color:#24292e;padding:0 .18em;border-radius:3px}"
    "body.dark .mdv-highlight,body.dark mark{background:#7a5d00;color:#fff7cc}"
    ".mdv-math{font-family:'Cambria Math','Times New Roman',serif;white-space:nowrap}"
    ".mdv-math-display{text-align:center;margin:1em 0;position:relative;overflow:auto;padding:0 2.5em}"
    ".mdv-math-tag{position:absolute;right:.5em;top:50%;margin-top:-.8em}"
    ".mdv-math-bin{padding:0 .18em}"
    ".mdv-math-frac,.mdv-math-limits,.mdv-math-scripts{display:inline-block;vertical-align:middle;text-align:center;line-height:1.08}"
    ".mdv-math-frac{font-size:.9em;margin:0 .13em}"
    ".mdv-math-frac>span,.mdv-math-limits>span,.mdv-math-scripts>span{display:block;padding:.08em .12em}"
    ".mdv-math-frac>span:first-child{border-bottom:1px solid}"
    ".mdv-math-radical{font-size:1.25em;margin-right:-.035em}"
    ".mdv-math-radicand{display:inline-block;border-top:1px solid;padding:.08em .13em 0}"
    ".mdv-math-big{font-size:1.42em}"
    ".mdv-math-up,.mdv-math-down{font-size:.7em}"
    ".mdv-math-raised{vertical-align:.52em}"
    ".mdv-math-lowered{vertical-align:-.23em}"
    ".mdv-emoji{font-family:'Segoe UI Emoji','Apple Color Emoji','Noto Color Emoji',sans-serif}"
    "details{display:block;margin:.9em 0;padding:.1em 0}"
    "summary{cursor:pointer;font-weight:600;outline:none}"
    "summary:hover{color:#0366d6}body.dark summary:hover{color:#569cd6}"
    "img{display:inline-block;vertical-align:middle;max-width:none;border-radius:4px}"
    "img.mdv-md-image{display:block;height:auto}"
    "body.mdv-fit img{max-width:100%}"
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
    ".sh-pp{color:#8250df}body.dark .sh-pp{color:#d2a8ff}"
    ".sh-md-heading{color:#0969da;font-weight:bold}body.dark .sh-md-heading{color:#79c0ff}"
    ".sh-diff-add{color:#116329}body.dark .sh-diff-add{color:#7ee787}"
    ".sh-diff-del{color:#a40e26}body.dark .sh-diff-del{color:#ffa198}"
    ".sh-diff-meta{color:#57606a}body.dark .sh-diff-meta{color:#aab3bc}"
    ".sh-diff-hunk{color:#8250df}body.dark .sh-diff-hunk{color:#d2a8ff}"

    /* Find bar */
    "#mdv-fb{display:none;position:fixed;top:0;left:0;right:0;z-index:10000;"
    "background:#f0f0f0;border-bottom:1px solid #ccc;"
    "padding:8px 12px;font:13px 'Segoe UI',sans-serif;box-shadow:0 2px 8px rgba(0,0,0,.15)}"
    "body.dark #mdv-fb{background:#2d2d2d;border-bottom-color:#555}"
    "#mdv-fb.on{display:block}"
    "#mdv-fb-row{display:flex;align-items:center;gap:8px;flex-wrap:wrap}"
    "#mdv-fb-row>*{margin-right:8px;margin-bottom:4px}"
    "#mdv-fi{padding:4px 8px;border:1px solid #bbb;border-radius:3px;"
    "font-size:13px;width:280px;outline:none;background:#fff;color:#24292e}"
    "body.dark #mdv-fi{background:#1e1e1e;border-color:#555;color:#d4d4d4}"
    "#mdv-fi:focus{border-color:#0366d6}"
    "body.dark #mdv-fi:focus{border-color:#569cd6}"
    "#mdv-fc{color:#6a737d;min-width:84px;font-size:12px}"
    ".fb{padding:3px 10px;border:1px solid #bbb;border-radius:3px;"
    "background:#e0e0e0;color:#24292e;cursor:pointer;font-size:13px}"
    "body.dark .fb{background:#404040;border-color:#555;color:#d4d4d4}"
    ".fb:hover{background:#d0d0d0}body.dark .fb:hover{background:#505050}"
    ".fb.tgl{min-width:34px;padding:3px 8px}"
    ".fb.primary{font-weight:600}"
    ".fb.close{min-width:34px;font-size:16px;font-weight:700;background:#f6f8fa;border-color:#8c959f}"
    ".fb.close:hover{background:#ffd8d8;border-color:#cf222e;color:#82071e}"
    "body.dark .fb.close{background:#3a3a3a;border-color:#777;color:#f0f0f0}"
    "body.dark .fb.close:hover{background:#5a2b2b;border-color:#ff7b72;color:#fff}"
    "#mdv-fo{display:none;margin-top:6px;padding-top:6px;border-top:1px solid #d0d7de;"
    "color:#57606a;align-items:center;gap:12px;flex-wrap:wrap}"
    "body.dark #mdv-fo{border-top-color:#444;color:#c9d1d9}"
    "#mdv-fb.opts #mdv-fo{display:flex}"
    ".mdv-fopt{display:inline-flex;align-items:center;gap:6px;font-size:12px;white-space:nowrap;margin-right:14px;margin-bottom:4px}"
    ".mdv-fopt input{margin:0 6px 0 0}"
    ".hl{background:#fff59d;color:#000;border-radius:2px}"
    ".hl-a{background:#ff9800;color:#000;font-weight:bold}"
    ".mdv-mm-text .hl,.mdv-mm-note-text .hl{fill:#854d00}"
    ".mdv-mm-text .hl-a,.mdv-mm-note-text .hl-a{fill:#9a3412;text-decoration:underline}"
    "body.dark svg .hl{fill:#f5cd6d}body.dark svg .hl-a{fill:#ffab70}"
    "body.dark .hl{background:#6b5b00}body.dark .hl-a{background:#b37400}"

    /* TOC */
    "#mdv-toc{display:none;position:fixed;top:0;right:0;bottom:0;width:280px;z-index:9999;"
    "background:#f6f8fa;border-left:1px solid #e1e4e8;"
    "overflow-y:auto;padding:16px 0;font-family:inherit;font-size:1em;line-height:1.45;"
    "box-shadow:-2px 0 12px rgba(0,0,0,.1)}"
    "body.dark #mdv-toc{background:#252526;border-left-color:#404040}"
    "#mdv-toc.on{display:block}"
    "#mdv-toc-t{padding:0 16px 12px;font-size:1.08em;font-weight:700;color:#1a1a1a;"
    "border-bottom:1px solid #e1e4e8;margin-bottom:8px}"
    "body.dark #mdv-toc-t{color:#e0e0e0;border-bottom-color:#404040}"
    ".ti{display:block;padding:4px 16px;color:#24292e;text-decoration:none;cursor:pointer;border-left:3px solid transparent;font-size:1em;line-height:1.4}"
    "body.dark .ti{color:#d4d4d4}"
    ".ti:hover{background:#ebeef1;border-left-color:#0366d6}"
    "body.dark .ti:hover{background:#2d2d2d;border-left-color:#569cd6}"
    ".t1{font-weight:600}.t2{padding-left:28px}.t3{padding-left:40px;font-size:.96em}"
    ".t4,.t5,.t6{padding-left:52px;font-size:.92em;color:#6a737d}"
    ".t5{padding-left:64px}.t6{padding-left:76px}"
    "body.dark .t4,body.dark .t5,body.dark .t6{color:#aaa}"

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
    "#mdv-char,#mdv-raw-char{position:fixed;right:16px;bottom:16px;z-index:10002;"
    "padding:6px 10px;border-radius:999px;background:rgba(255,255,255,.92);"
    "border:1px solid #d0d7de;color:#57606a;font:12px 'Segoe UI',sans-serif;"
    "box-shadow:0 3px 12px rgba(0,0,0,.10);pointer-events:none}"
    "#mdv-raw-char{bottom:50px;display:none}"
    "body.mdv-split #mdv-raw-char,body.mdv-code #mdv-raw-char{display:block}"
    "body.mdv-code #mdv-char{display:none}"
    "body.mdv-code #mdv-raw-char{bottom:16px}"
    "body.dark #mdv-char,body.dark #mdv-raw-char{background:rgba(37,37,38,.94);border-color:#444;color:#c9d1d9}"
    "#mdv-status{position:fixed;left:16px;bottom:16px;z-index:10002;"
    "padding:6px 10px;border-radius:999px;background:rgba(255,255,255,.92);"
    "border:1px solid #d0d7de;color:#57606a;font:12px 'Segoe UI',sans-serif;"
    "box-shadow:0 3px 12px rgba(0,0,0,.10);pointer-events:none}"
    "body.dark #mdv-status{background:rgba(37,37,38,.94);border-color:#444;color:#c9d1d9}"

    /* Help overlay */
    "#mdv-help{display:none;position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);"
    "background:#ffffff;border:1px solid #ccc;border-radius:12px;"
    "padding:28px 36px;z-index:10003;box-shadow:0 12px 48px rgba(0,0,0,.35);"
    "font:16px 'Segoe UI',sans-serif;min-width:440px;max-width:520px;"
    "max-height:calc(100vh - 32px);width:calc(100vw - 32px);overflow:auto;color:#24292e}"
    "body.dark #mdv-help{background:#2d2d2d;border-color:#555;color:#d4d4d4}"
    "#mdv-help.on{display:block}"
    "#mdv-help-close{position:absolute;top:10px;right:12px;border:none;background:transparent;"
    "font-size:28px;line-height:1;color:#7a7a7a;cursor:pointer;padding:4px 8px}"
    "#mdv-help-close:hover{color:#111}body.dark #mdv-help-close{color:#bbb}body.dark #mdv-help-close:hover{color:#fff}"
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
    sb_append(sb,"</style><style id=\"mdv-palette\">");
    append_theme_css(sb,0);
    append_theme_css(sb,1);
}

/* --- JavaScript --- */

static void build_js(StrBuf* sb, ViewMode view, int wrap) {
    /* Initial values from settings */
    char init[256];
    _snprintf(init,sizeof(init),"<script>var fs=%d,mw=%d,ln=%d,tt=null,mdvInitialView=%d,mdvWordWrap=%d,mdvRawFont=%d,zmin=%d,zmax=%d,zdef=%d,zstep=%d;",
              g_settings.fontSize,g_settings.maxWidth,g_settings.lineNums,view,wrap,g_settings.rawFontSize,
              MDVIEW_ZOOM_MIN,MDVIEW_ZOOM_MAX,MDVIEW_ZOOM_DEFAULT,MDVIEW_ZOOM_STEP);
    sb_append(sb, init);

    sb_append(sb,
    /* Toast */
    "function toast(m){var t=document.getElementById('mdv-toast');t.innerText=m;t.className='on';"
    "if(tt)clearTimeout(tt);tt=setTimeout(function(){t.className=''},1500)}"
    "var mdvViewMode=0,mdvPreviewY=0,mdvSourceY=0,mdvSourceX=0;"
    "var mdvRawActive=0,mdvRawDragging=0,mdvRenderDragging=0,mdvRawSelStart=-1,mdvRawSelEnd=-1,mdvLastX=8,mdvLastY=8,mdvSyncHitTimer=null,mdvPaneRestoreTimer=null;"
    "function mdvRenderPane(){return document.getElementById('mdv-render-pane')}"
    "function mdvRawPane(){return document.getElementById('mdv-raw-pane')}"
    "function mdvIsSplit(){return mdvViewMode===2}"
    "function mdvUsingSource(){return mdvViewMode===1||(mdvIsSplit()&&mdvRawActive)}"
    "function mdvScrollPane(){return mdvViewMode===1?mdvRawPane():mdvIsSplit()?mdvRenderPane():null}"
    "function mdvScrollY(){var p=mdvScrollPane(),de=document.documentElement,b=document.body;if(p)return p.scrollTop;return (window.pageYOffset||((de&&de.scrollTop)||0)||((b&&b.scrollTop)||0));}"
    "function mdvSetScrollY(y){var p=mdvScrollPane();y=Math.max(0,y);if(p)p.scrollTop=y;else window.scrollTo(0,y);}"
    "function mdvScrollElToTop(el,pad){var st;if(!el)return;pad=(pad||0);st=mdvScrollY();mdvSetScrollY(el.getBoundingClientRect().top+st-pad)}"
    "function mdvAnchorEls(){var ct=document.getElementById('mdv-ct');return ct&&ct.querySelectorAll?ct.querySelectorAll('[data-mdv-line]'):[]}"
    "function mdvLineAtY(y){var els=mdvAnchorEls(),best=null,bestTop=-999999,bestH=2147483647,i,r,top,h,ln;"
    "y=parseInt(y,10);if(isNaN(y)||y<0)y=0;"
    "for(i=0;i<els.length;i++){r=els[i].getBoundingClientRect();top=r.top;"
    "if(top<=y&&r.bottom>=y){h=r.bottom-r.top;if(h<bestH){best=els[i];bestH=h;}}"
    "else if(!best&&top<=y+8&&top>bestTop){best=els[i];bestTop=top;}}"
    "if(!best&&els.length)best=els[0];ln=best?parseInt(best.getAttribute('data-mdv-line'),10):-1;return isNaN(ln)?-1:ln;}"
    "function mdvLineFloatAtY(y){var els=mdvAnchorEls(),best=null,bestTop=-999999,bestH=2147483647,i,r,top,h,ln,en,frac;"
    "y=parseInt(y,10);if(isNaN(y)||y<0)y=0;"
    "for(i=0;i<els.length;i++){r=els[i].getBoundingClientRect();top=r.top;"
    "if(top<=y&&r.bottom>=y){h=r.bottom-r.top;if(h<bestH){best=els[i];bestH=h;}}"
    "else if(!best&&top<=y+8&&top>bestTop){best=els[i];bestTop=top;}}"
    "if(!best&&els.length)best=els[0];if(!best)return -1;"
    "ln=parseInt(best.getAttribute('data-mdv-line'),10);en=parseInt(best.getAttribute('data-mdv-line-end'),10);"
    "if(isNaN(ln))return -1;if(isNaN(en)||en<ln)en=ln;r=best.getBoundingClientRect();"
    "h=Math.max(1,r.bottom-r.top);frac=(y-r.top)/h;if(frac<0)frac=0;if(frac>1)frac=1;"
    "return Math.round((ln+(en-ln)*frac)*1000);}"
    "function mdvTopLine(){return mdvLineAtY(0)}"
    "function mdvFindLineAnchor(line){var els=mdvAnchorEls(),best=null,next=null,bn=-1,nn=2147483647,ln,i;"
    "line=parseInt(line,10);if(isNaN(line)||line<0)return;"
    "for(i=0;i<els.length;i++){ln=parseInt(els[i].getAttribute('data-mdv-line'),10);if(isNaN(ln))continue;"
    "if(ln<=line&&ln>=bn){best=els[i];bn=ln;}if(ln>line&&ln<nn){next=els[i];nn=ln;}}return best||next;}"
    "function mdvElementLine(el){var ct=document.getElementById('mdv-ct'),ln;"
    "while(el&&el!==ct){if(el.getAttribute&&el.getAttribute('data-mdv-line')!==null){"
    "ln=parseInt(el.getAttribute('data-mdv-line'),10);return isNaN(ln)?-1:ln;}el=el.parentNode;}return -1;}"
    "function mdvScrollToLine(line){var best=mdvFindLineAnchor(line);"
    "if(best)mdvScrollElToTop(best,8);}"
    "function mdvScrollToLineAtY(line,y){var best,st,top;"
    "line=parseInt(line,10);y=parseInt(y,10);if(isNaN(line)||line<0)return;if(isNaN(y)||y<0)y=0;"
    "best=mdvFindLineAnchor(line);"
    "if(best){st=mdvScrollY();top=best.getBoundingClientRect().top+st-y;mdvSetScrollY(top);}}"
    "function mdvNorm(s){return String(s||'').toLowerCase().replace(/[^a-z0-9\\u0080-\\uffff]+/g,' ').replace(/^\\s+|\\s+$/g,'').replace(/\\s+/g,' ')}"
    "function mdvScore(t,s){if(t===s)return 0;if(t.indexOf(s)===0)return 1;if(t.indexOf(s)>=0)return 2;if(s.indexOf(t)>=0)return 3;return 999}"
    "function mdvRawLines(){var r=mdvRawPane();return r?r.children:[]}"
    "function mdvRawLineEl(line){return mdvRawLines()[line]||null}"
    "function mdvTextValue(el){if(typeof el.textContent==='string')return el.textContent;"
    "var walker=mdvTextWalker(el),parts=[];while(walker.nextNode())parts.push(walker.currentNode.nodeValue);return parts.join('');}"
    "function mdvRawParts(line){var out=[],w=mdvTextWalker(line),n,p,space=null,encoding,offset=0,value;"
    "while(w.nextNode()){n=w.currentNode;value=n.nodeValue;if(!value)continue;"
    "for(p=n.parentNode;p&&p!==line&&!mdvHasClass(p,'mdv-source-space');p=p.parentNode){}"
    "if(p&&p!==line){if(p!==space){space=p;encoding=p.getAttribute('data-mdv-space',2);offset=0;}"
    "if(encoding){value=encoding.substr(offset,value.length).replace(/t/g,'\t').replace(/s/g,' ');offset+=value.length;}}"
    "out.push({node:n,text:value});}return out;}"
    "function mdvRawLineText(el){var parts=mdvRawParts(el),out=[];for(var i=0;i<parts.length;i++)out.push(parts[i].text);return out.join('');}"
    "function mdvRawText(line){var e=mdvRawLineEl(line);return e?mdvRawLineText(e):''}"
    "function mdvRawRangeText(a,b){var es=mdvRawLines(),out=[],i,tmp;"
    "a=parseInt(a,10);b=parseInt(b,10);if(isNaN(a)||isNaN(b)||a<0||b<0)return '';if(a>b){tmp=a;a=b;b=tmp;}"
    "for(i=a;i<=b&&i<es.length;i++)out.push(mdvRawLineText(es[i]));return out.join('\\r\\n')}"
    "function mdvRawAllText(){return mdvRawRangeText(0,mdvRawLines().length-1)}"
    "function mdvRawPointLine(e){var r=mdvRawPane(),rr,y;if(!r)return -1;e=e||window.event;rr=r.getBoundingClientRect();y=(e.clientY||0)-rr.top;if(y<0)y=0;if(y>rr.bottom-rr.top)y=rr.bottom-rr.top-1;return mdvLineFromRawPoint(y)}"
    "function mdvRawSelectedText(){var t;if(mdvRawSelStart>=0&&mdvRawSelEnd>=0){t=mdvRawRangeText(mdvRawSelStart,mdvRawSelEnd);if(t)return t;}return ''}"
    "function mdvRawSig(line){var s=mdvNorm(mdvRawText(line));if(s.length>=8)return s.substring(0,160);return mdvNorm(mdvRawText(line-1)+' '+mdvRawText(line)+' '+mdvRawText(line+1)).substring(0,160)}"
    "function mdvBestRaw(sig,hint){var es=mdvRawLines(),best=null,bs=999,bd=2147483647,bl=2147483647,i,e,txt,sc,ln,d,tl;for(i=0;i<es.length;i++){e=es[i];txt=mdvNorm(mdvRawLineText(e));if(txt.length<8)continue;sc=mdvScore(txt,sig);if(sc>=999)continue;ln=i;d=isNaN(ln)?0:Math.abs(ln-hint);tl=txt.length;if(sc<bs||(sc===bs&&(d<bd||(d===bd&&tl<bl)))){best=e;bs=sc;bd=d;bl=tl;}}return best}"
    "function mdvBestDom(sig,hint){var els=mdvAnchorEls(),best=null,bs=999,bd=2147483647,bl=2147483647,i,e,txt,sc,ln,d,tl;for(i=0;i<els.length;i++){e=els[i];if(e.tagName&&/^(UL|OL|TABLE)$/i.test(e.tagName))continue;txt=mdvNorm(e.innerText||e.textContent||'');if(txt.length<8)continue;sc=mdvScore(txt,sig);if(sc>=999)continue;ln=parseInt(e.getAttribute('data-mdv-line'),10);d=isNaN(ln)?0:Math.abs(ln-hint);tl=txt.length;if(sc<bs||(sc===bs&&(d<bd||(d===bd&&tl<bl)))){best=e;bs=sc;bd=d;bl=tl;}}return best}"
    "function mdvBlockForRawLine(line){var els=mdvAnchorEls(),best=null,span=2147483647,i,e,ln,en,sp;for(i=0;i<els.length;i++){e=els[i];if(e.tagName&&/^(UL|OL|TABLE)$/i.test(e.tagName))continue;ln=parseInt(e.getAttribute('data-mdv-line'),10);en=parseInt(e.getAttribute('data-mdv-line-end'),10);if(isNaN(ln))continue;if(isNaN(en)||en<ln)en=ln;if(line>=ln&&line<=en){sp=en-ln;if(sp<span){span=sp;best=e;}}}return best}"
    "function mdvClearSyncHits(){var r=mdvRawPane(),i,es;if(mdvSyncHitTimer){clearTimeout(mdvSyncHitTimer);mdvSyncHitTimer=null;}if(r){es=mdvRawLines();for(i=0;i<es.length;i++)es[i].className=es[i].className.replace(/\\bmdv-raw-hit\\b/g,'').replace(/^\\s+|\\s+$/g,'');}es=mdvAnchorEls();for(i=0;i<es.length;i++)es[i].className=(es[i].className||'').replace(/\\bmdv-sync-hit\\b/g,'').replace(/^\\s+|\\s+$/g,'')}"
    "function mdvScheduleClearSyncHits(){if(mdvSyncHitTimer)clearTimeout(mdvSyncHitTimer);mdvSyncHitTimer=setTimeout(function(){mdvSyncHitTimer=null;mdvClearSyncHits();},1500)}"
    "function mdvScrollRawElToY(el,y){var r=mdvRawPane(),st;if(!r||!el)return;if(y<0)y=0;st=r.scrollTop;r.scrollTop=Math.max(0,st+el.getBoundingClientRect().top-r.getBoundingClientRect().top-y);el.className=(el.className?el.className+' ':'')+'mdv-raw-hit';mdvScheduleClearSyncHits()}"
    "function mdvScrollDomElToY(el,y){var st;if(!el)return;if(y<0)y=0;st=mdvScrollY();mdvSetScrollY(Math.max(0,st+el.getBoundingClientRect().top-(mdvIsSplit()?mdvRenderPane().getBoundingClientRect().top:0)-y));el.className=(el.className?el.className+' ':'')+'mdv-sync-hit';mdvScheduleClearSyncHits()}"
    "function mdvLineFromRawPoint(y){var r=mdvRawPane(),rr,el;if(!r)return -1;rr=r.getBoundingClientRect();el=document.elementFromPoint(rr.left+14,rr.top+y);while(el&&el!==r&&!mdvHasClass(el,'mdv-raw-line'))el=el.parentNode;if(el&&el!==r)return parseInt(el.getAttribute('data-raw-line'),10);return -1}"
    "function mdvSyncRawPoint(y){var line=mdvLineFromRawPoint(y),sig,el;if(line<0)return;mdvClearSyncHits();el=mdvBlockForRawLine(line);if(!el){sig=mdvRawSig(line);el=mdvBestDom(sig,line);}if(el)mdvScrollDomElToY(el,y)}"
    "function mdvSyncRenderPoint(x,y){var ct=document.getElementById('mdv-ct'),e=document.elementFromPoint(x,y),n=e,a=null,ln=-1,block,txt,sig,raw;if(!ct)return;while(n&&n!==ct){if(n.getAttribute&&n.getAttribute('data-mdv-line')!==null){a=n;ln=parseInt(n.getAttribute('data-mdv-line'),10);break;}n=n.parentNode;}block=e;while(block&&block!==ct){if(block.tagName&&/^(P|LI|TD|TH|H1|H2|H3|H4|H5|H6|PRE|BLOCKQUOTE)$/i.test(block.tagName))break;block=block.parentNode;}if((!block||block===ct)&&a)block=a;if(!block||block===ct)return;txt=(e&&e.tagName&&/^IMG$/i.test(e.tagName))?((e.alt||'')+' '+(e.getAttribute('src')||'')):(block.innerText||block.textContent||'');sig=mdvNorm(txt).substring(0,160);raw=mdvBestRaw(sig,isNaN(ln)?-1:ln);mdvClearSyncHits();if(raw)mdvScrollRawElToY(raw,y)}"
    "function mdvSyncPoint(x,y){if(!mdvIsSplit())return;var r=mdvRawPane(),rr;if(r&&mdvIsSplit()){rr=r.getBoundingClientRect();if(x>=rr.left&&x<=rr.right&&y>=rr.top&&y<=rr.bottom){mdvRawActive=1;mdvLastX=x;mdvLastY=y;mdvSyncRawPoint(y-rr.top);return;}mdvRawActive=0;}mdvLastX=x;mdvLastY=y;mdvSyncRenderPoint(x,y)}"
    "function mdvSyncHere(){if(!mdvIsSplit())return;var r=mdvRawPane(),rr,y=mdvLastY;if(mdvRawActive){rr=r?r.getBoundingClientRect():null;mdvSyncRawPoint(rr?Math.max(0,y-rr.top):8);return;}mdvSyncRenderPoint(mdvLastX||24,mdvLastY||8)}"
    "function usv(){var s=document.getElementById('mdv-status');if(!s)return;"
    "s.innerText='Zoom '+Math.round((fs*100)/zdef)+'%'+(ln?' | Line #':'');}"

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
    "function rawcc(){return mdvRawAllText().replace(/\\r|\\n/g,'').length}"
    "function ucc(){var el=document.getElementById('mdv-char'),rw=document.getElementById('mdv-raw-char');if(el)el.innerText='MD chars: '+rcc();if(rw)rw.innerText='RAW chars: '+rawcc();}"

    /* Zoom */
    "function zi(){fs=Math.min(fs+zstep,zmax);af()}"
    "function zo(){fs=Math.max(fs-zstep,zmin);af()}"
    "function zr(){fs=zdef;af()}"
    "function mdvToArray(nl){var a=[],i;if(!nl)return a;for(i=0;i<nl.length;i++)a.push(nl[i]);return a}"
    "function mdvHasClass(el,cls){var cn;if(!el||!cls)return false;cn=' '+(el.className||'')+' ';return cn.indexOf(' '+cls+' ')>=0}"
    "function mdvByClass(root,cls,tag){var out=[],els,i;root=root||document;els=root.getElementsByTagName(tag||'*');"
    "for(i=0;i<els.length;i++)if(mdvHasClass(els[i],cls))out.push(els[i]);return out}"
    "function mdvQSA(root,sel){var out=[],i,j,nodes,blocks,svgs;root=root||document;"
    "if(root.querySelectorAll){try{return root.querySelectorAll(sel)}catch(ex){}}"
    "if(sel==='.hl')return mdvByClass(root,'hl');"
    "if(sel==='.ti')return mdvByClass(root,'ti');"
    "if(sel==='.ln-code')return mdvByClass(root,'ln-code');"
    "if(sel==='pre')return root.getElementsByTagName('pre');"
    "if(sel==='pre,blockquote'){nodes=mdvToArray(root.getElementsByTagName('pre'));blocks=root.getElementsByTagName('blockquote');for(i=0;i<blocks.length;i++)nodes.push(blocks[i]);return nodes;}"
    "if(sel==='pre code[class]'){nodes=[];blocks=root.getElementsByTagName('pre');for(i=0;i<blocks.length;i++){var codes=blocks[i].getElementsByTagName('code');for(j=0;j<codes.length;j++)if(codes[j].className)nodes.push(codes[j]);}return nodes;}"
    "if(sel==='h1[id],h2[id],h3[id],h4[id],h5[id],h6[id]'){nodes=[];blocks=root.getElementsByTagName('*');for(i=0;i<blocks.length;i++)if(/^H[1-6]$/i.test(blocks[i].tagName)&&blocks[i].getAttribute('id'))nodes.push(blocks[i]);return nodes;}"
    "if(sel==='.mdv-mermaid[data-mdv-mermaid]'){nodes=mdvByClass(root,'mdv-mermaid');out=[];for(i=0;i<nodes.length;i++)if(nodes[i].getAttribute&&nodes[i].getAttribute('data-mdv-mermaid')!==null)out.push(nodes[i]);return out;}"
    "if(sel==='.mdv-mermaid svg'){nodes=mdvByClass(root,'mdv-mermaid');out=[];for(i=0;i<nodes.length;i++){svgs=nodes[i].getElementsByTagName('svg');for(j=0;j<svgs.length;j++)out.push(svgs[j]);}return out;}"
    "return out}"
    "function mdvQS(root,sel){var list=mdvQSA(root,sel);return(list&&list.length)?list[0]:null}"
    "function fitMermaidSvg(svg){var vb,parts,baseW,baseH,view,availW,targetW,targetH;"
    "if(!svg)return;vb=svg.getAttribute('viewBox');if(!vb)return;parts=vb.split(/\\s+/);if(parts.length!==4)return;"
    "baseW=parseFloat(parts[2]);baseH=parseFloat(parts[3]);if(!(baseW>0)||!(baseH>0))return;"
    "view=svg.parentNode;availW=view?view.clientWidth-32:0;"
    "if(!(availW>0)){if(window.setTimeout&&!svg.getAttribute('data-mdv-fit-pending')){svg.setAttribute('data-mdv-fit-pending','1');window.setTimeout(function(){try{svg.removeAttribute('data-mdv-fit-pending');fitMermaidSvg(svg);}catch(ex){}},0);}return;}"
    "targetW=Math.min(availW,baseW);targetH=Math.ceil(baseH*(targetW/baseW));"
    "svg.setAttribute('width',targetW);svg.setAttribute('height',targetH);svg.style.width=targetW+'px';svg.style.height=targetH+'px';}"
    "function syncMermaidTypography(){var svgs=mdvQSA(document,'.mdv-mermaid svg');"
    "var cs=window.getComputedStyle?window.getComputedStyle(document.body,null):null;"
    "var ff=cs&&cs.fontFamily?cs.fontFamily:document.body.style.fontFamily;"
    "var fz=cs&&cs.fontSize?cs.fontSize:document.body.style.fontSize;"
    "for(var i=0;i<svgs.length;i++){if(ff)svgs[i].style.fontFamily=ff;if(fz)svgs[i].style.fontSize=fz;fitMermaidSvg(svgs[i]);}}"
    "function mmBodyFontPx(){var cs=window.getComputedStyle?window.getComputedStyle(document.body,null):null;"
    "var fz=cs&&cs.fontSize?parseFloat(cs.fontSize):parseFloat(document.body.style.fontSize);"
    "if(!(fz>0))fz=16;return fz;}"
    "function af(){document.body.style.fontSize=fs+'px';mdvSourceZoom();if(mdvViewMode!==1)initMermaid();usv();toast('Zoom: '+Math.round(100*fs/zdef)+'%')}"

    "function mdvPalette(css){var el=document.getElementById('mdv-palette');"
    "if(el.styleSheet)el.styleSheet.cssText=css;else el.textContent=css;}"
    "function mdvSetTheme(dark){var b=document.body,c=b.className||'';"
    "c=c.replace(/\\b(?:dark|mdv-dark|mdv-light)\\b/g,'').replace(/^\\s+|\\s+$/g,'');"
    "document.documentElement.className=dark?'mdv-dark':'mdv-light';"
    "b.className=(c?c+' ':'')+(dark?'mdv-dark dark':'mdv-light');}"

    /* Fit to window */
    "function afw(){var b=document.body,c=b.className||'';"
    "if(mw===0){if(c.indexOf('mdv-fit')<0)b.className=(c?c+' ':'')+'mdv-fit';}"
    "else{b.className=c.replace(/\\bmdv-fit\\b/g,'').replace(/^\\s+|\\s+$/g,'');}}"
    "function fw(){var ct=document.getElementById('mdv-ct');"
    "if(mw===0){mw=960;ct.style.maxWidth=mw+'px';toast('Fit to window OFF')}"
    "else{mw=0;ct.style.maxWidth='none';toast('Fit to window ON')}"
    "afw();syncMermaidTypography();fitImages();usv()}"

    /* Line numbers toggle */
    "function tl(){"
    "var y=mdvScrollY();ln=ln?0:1;var ps=mdvQSA(document,'pre');"
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
    "  var cd2=mdvQS(p,'.ln-code');if(cd2){var code2=p.getElementsByTagName('code')[0];"
    "  if(code2){code2.innerHTML=cd2.innerHTML;}}"
    "  p.className=p.className.replace(/\\bln\\b/g,'').replace(/^\\s+|\\s+$/g,'');p._lnDone=0;"
    "}}"
    "mdvSetScrollY(y);usv();toast(ln?'Line numbers ON':'Line numbers OFF')}"

    /* TOC */
    "function btoc(){var toc=document.getElementById('mdv-toc');"
    "var hs=mdvQSA(document,'h1[id],h2[id],h3[id],h4[id],h5[id],h6[id]');"
    "var old=mdvQSA(toc,'.ti');for(var i=0;i<old.length;i++)old[i].parentNode.removeChild(old[i]);"
    "for(var i=0;i<hs.length;i++){var h=hs[i],a=document.createElement('a');"
    "a.className='ti t'+h.tagName.charAt(1);a.innerText=h.innerText;a.href='#'+h.id;"
    "a.onclick=(function(id){return function(e){e.preventDefault?e.preventDefault():e.returnValue=false;var el=document.getElementById(id);if(el)mdvScrollElToTop(el,12)}})(h.id);"
    "toc.appendChild(a)}initLinkTooltips()}"
    "function ttoc(){if(mdvViewMode===1)return;var toc=document.getElementById('mdv-toc');"
    "if(toc.className.indexOf('on')>=0){toc.className='';document.body.style.marginRight='0'}"
    "else{btoc();toc.className='on';document.body.style.marginRight='280px'}usv()}"

    /* Link tooltip preview */
    "function mdvLinkTitleText(a){var href,raw;if(!a)return '';"
    "raw=a.getAttribute?a.getAttribute('href',2):null;"
    "if(!raw)return '';"
    "if(raw.charAt&&raw.charAt(0)==='#')return raw;"
    "href=a.href||raw;"
    "return href||'';}"
    "function mdvHandleLinkClick(e){"
    "e=e||window.event;var t=e.target||e.srcElement,raw,href,id,el;"
    "while(t&&t.tagName!=='A')t=t.parentNode;"
    "if(!t)return true;"
    "raw=t.getAttribute?t.getAttribute('href',2):null;href=raw||t.href||'';"
    "if(!href)return true;"
    "if(href.charAt(0)==='#'){"
    "id=href.substring(1);el=document.getElementById(id);"
    "if(el){if(e.preventDefault)e.preventDefault();else e.returnValue=false;mdvScrollElToTop(el,12);return false;}"
    "return true;}"
    "if(e.preventDefault)e.preventDefault();else e.returnValue=false;"
    "document.title='MDVLINK:'+encodeURIComponent(href);"
    "return false;}"
    "function initLinkTooltips(){var as=document.getElementsByTagName('a'),i,a,t;"
    "for(i=0;i<as.length;i++){a=as[i];t=mdvLinkTitleText(a);if(!t)continue;"
    "if(typeof a._mdvOrigTitle==='undefined')a._mdvOrigTitle=a.getAttribute('title');"
    "a.title=t;a.onclick=mdvHandleLinkClick;}}"

    /* Find */
    "var fm=[],fi=-1,flq='',flmc=0,flww=0,mdvSplitFindMarks=[];"
    "function mdvTextWalker(root){var nodes=[],stack=[root],n,i,ch;"
    "while(stack.length){n=stack.pop();if(!n)continue;"
    "if(n.nodeType===3){nodes.push(n);continue;}"
    "ch=n.childNodes;if(!ch)continue;for(i=ch.length-1;i>=0;i--)stack.push(ch[i]);}"
    "return{_nodes:nodes,_idx:-1,currentNode:null,nextNode:function(){this._idx++;if(this._idx>=this._nodes.length)return false;this.currentNode=this._nodes[this._idx];return true;}}}"
    "function gfopt(id,def){var el=document.getElementById(id);return el?!!el.checked:!!def}"
    "function sfopt(id,val){var el=document.getElementById(id);if(el)el.checked=!!val}"
    "function ufmsg(m){var c=document.getElementById('mdv-fc');if(c)c.innerText=m||''}"
    "function fcfg(){return{mc:gfopt('mdv-fcase',0)?1:0,ww:gfopt('mdv-fword',0)?1:0,manual:gfopt('mdv-fmanual',0)?1:0}}"
    "function fos(){var b=document.getElementById('mdv-fb');if(!b)return;b.className=(b.className.indexOf('opts')>=0)?'on':'on opts'}"
    "function inFindBar(el){var fb=document.getElementById('mdv-fb');while(el){if(el===fb)return true;el=el.parentNode;}return false}"
    "function sfs(mc,ww,manual){if(typeof mc!=='undefined')sfopt('mdv-fcase',mc);if(typeof ww!=='undefined')sfopt('mdv-fword',ww);if(typeof manual!=='undefined')sfopt('mdv-fmanual',manual)}"
    "function mdvClearFindMarks(marks){\n"
    "    for(var i=0;i<marks.length;i++){\n"
    "        var mark=marks[i],parent=mark.parentNode;\n"
    "        if(parent){parent.replaceChild(document.createTextNode(mdvTextValue(mark)),mark);parent.normalize();}\n"
    "    }\n"
    "}\n"
    "function cf(){\n"
    "    mdvClearFindMarks(mdvQSA(document,'.hl'));mdvSplitFindMarks=[];\n"
    "    fm=[];fi=-1;flq='';flmc=0;flww=0;ufmsg('');\n"
    "}\n"
    "function mdvFindPattern(text,mc,ww){\n"
    "    return ww?new RegExp('\\\\b'+text.replace(/[\\^$.*+?()[\\]{}|]/g,'\\\\$&')+'\\\\b',mc?'g':'gi'):null;\n"
    "}\n"
    "function mdvSourceMatches(line,txt,mc,re){\n"
    "    var parts=mdvRawParts(line),text='',ranges=[],j,k,m;\n"
    "    for(j=0;j<parts.length;j++){parts[j].start=text.length;text+=parts[j].text;}\n"
    "    if(re){\n"
    "        re.lastIndex=0;\n"
    "        while((m=re.exec(text))!==null)ranges.push({start:m.index,end:m.index+m[0].length,marks:[]});\n"
    "    }else{\n"
    "        var found=mc?text:text.toLowerCase(), needle=mc?txt:txt.toLowerCase();\n"
    "        for(k=0;(k=found.indexOf(needle,k))>=0;k+=needle.length)ranges.push({start:k,end:k+needle.length,marks:[]});\n"
    "    }\n"
    "    return {parts:parts,ranges:ranges};\n"
    "}\n"
    "function mdvFindMark(node,text){\n"
    "    var namespace=node.parentNode.namespaceURI;\n"
    "    var mark=namespace==='http://www.w3.org/2000/svg'?document.createElementNS(namespace,'tspan'):document.createElement('span');\n"
    "    mark.setAttribute('class','hl');mark.appendChild(document.createTextNode(text));return mark;\n"
    "}\n"
    "function mdvMarkSource(parts,ranges){\n"
    "    var k=0,j;\n"
    "    for(j=0;j<parts.length&&k<ranges.length;j++){\n"
    "        var part=parts[j],value=part.node.nodeValue,end=part.start+value.length,fragment=null,cursor=0;\n"
    "        while(k<ranges.length&&ranges[k].start<end){\n"
    "            var range=ranges[k],a=Math.max(0,range.start-part.start),b=Math.min(value.length,range.end-part.start);\n"
    "            if(!fragment)fragment=document.createDocumentFragment();\n"
    "            if(a>cursor)fragment.appendChild(document.createTextNode(value.substring(cursor,a)));\n"
    "            var mark=mdvFindMark(part.node,value.substring(a,b));\n"
    "            fragment.appendChild(mark);range.marks.push(mark);cursor=b;\n"
    "            if(range.end>end)break;k++;\n"
    "        }\n"
    "        if(fragment){\n"
    "            if(cursor<value.length)fragment.appendChild(document.createTextNode(value.substring(cursor)));\n"
    "            part.node.parentNode.replaceChild(fragment,part.node);\n"
    "        }\n"
    "    }\n"
    "}\n"
    "function mdvFindSource(txt,mc,re){\n"
    "    var lines=mdvRawLines(),i,j,match;\n"
    "    for(i=0;i<lines.length;i++){\n"
    "        match=mdvSourceMatches(lines[i],txt,mc,re);\n"
    "        mdvMarkSource(match.parts,match.ranges);\n"
    "        for(j=0;j<match.ranges.length;j++)if(match.ranges[j].marks.length)fm.push(match.ranges[j].marks);\n"
    "    }\n"
    "}\n"
    "function df(txt,mc,ww){\n"
    "    cf();if(!txt)return;mc=mc?1:0;ww=ww?1:0;\n"
    "    flq=txt;flmc=mc;flww=ww;\n"
    "    var re=mdvFindPattern(txt,mc,ww);\n"
    "    if(mdvViewMode===1){mdvFindSource(txt,mc,re);fi=fm.length?0:-1;ufh();return;}\n"
    "    var ct=document.getElementById('mdv-ct');if(!ct)return;\n"
    "    var w=document.createTreeWalker?document.createTreeWalker(ct,4,null,false):mdvTextWalker(ct),ns=[];\n"
    "while(w.nextNode()){var n=w.currentNode;"
    "var parent=n.parentNode, hidden=false;\n"
    "while(parent&&parent!==ct){\n"
    "    if(parent.tagName==='SCRIPT'||(mdvHasClass(parent,'mdv-mermaid-src')&&mdvHasClass(parent.parentNode,'ok'))){\n"
    "        hidden=true;break;\n"
    "    }\n"
    "    parent=parent.parentNode;\n"
    "}\n"
    "if(hidden)continue;\n"

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
    "    f.appendChild(mdvFindMark(nd,v.substring(s,e)));li=e;"
    "    if(m[0].length===0)re.lastIndex++;"
    "  }"
    "  if(li<v.length)f.appendChild(document.createTextNode(v.substring(li)));"
    "}else{"
    "  var nee=mc?txt:txt.toLowerCase();"
    "  var idx=0,pos,vl=mc?v:v.toLowerCase();"
    "  while((pos=vl.indexOf(nee,idx))>=0){"
    "    if(pos>idx)f.appendChild(document.createTextNode(v.substring(idx,pos)));"
    "    f.appendChild(mdvFindMark(nd,v.substring(pos,pos+txt.length)));idx=pos+txt.length;"
    "  }"
    "  if(idx<v.length)f.appendChild(document.createTextNode(v.substring(idx)));"
    "}"
    "nd.parentNode.replaceChild(f,nd);}"
    "fm=mdvQSA(ct,'.hl');fi=fm.length>0?0:-1;ufh()}"

    "function mdvRevealFind(el,pane,off,revealColumn){\n"
    "    var rect=el.getBoundingClientRect(), viewport=window.innerHeight||document.documentElement.clientHeight;\n"
    "    if(!pane){\n"
    "        if(rect.top<off||rect.bottom>viewport)mdvScrollElToTop(el,off);\n"
    "        return;\n"
    "    }\n"
    "    var bounds=pane.getBoundingClientRect();\n"
    "    var top=Math.max(off,bounds.top+pane.clientTop+8);\n"
    "    var bottom=Math.min(viewport,bounds.top+pane.clientTop+pane.clientHeight);\n"
    "    if(rect.top<top||rect.bottom>bottom)pane.scrollTop=Math.max(0,pane.scrollTop+rect.top-top);\n"
    "    if(revealColumn&&pane.scrollWidth>pane.clientWidth){\n"
    "        var left=bounds.left+pane.clientLeft, right=left+pane.clientWidth;\n"
    "        if(rect.left<left||rect.right>right){\n"
    "            pane.scrollLeft=Math.max(0,pane.scrollLeft+rect.left-left-8);\n"
    "        }\n"
    "    }\n"
    "}\n"
    "function mdvSyncFindToSource(el,off){\n"
    "    var ct=document.getElementById('mdv-ct'), anchor=el, first,last;\n"
    "    var pane=mdvRawPane(), lines=mdvRawLines(), i,j,ordinal=0, marks, match, best=null, chosen=null;\n"
    "    if(!pane||!lines.length)return;\n"
    "    while(anchor&&anchor!==ct){\n"
    "        first=anchor.getAttribute?parseInt(anchor.getAttribute('data-mdv-line'),10):NaN;\n"
    "        if(!isNaN(first))break;\n"
    "        anchor=anchor.parentNode;\n"
    "    }\n"
    "    if(!anchor||anchor===ct){\n"
    "        first=mdvLineAtY(el.getBoundingClientRect().top);last=first;\n"
    "    }else{\n"
    "        last=parseInt(anchor.getAttribute('data-mdv-line-end'),10);\n"
    "        if(isNaN(last)||last<first)last=first;\n"
    "        marks=mdvQSA(anchor,'.hl');\n"
    "        for(i=0;i<marks.length&&marks[i]!==el;i++)ordinal++;\n"
    "    }\n"
    "    if(first<0||first>=lines.length)return;\n"
    "    var re=mdvFindPattern(flq,flmc,flww);\n"
    "    // Map repeated hits within the same rendered block in document order.\n"
    "    for(i=first;i<=last&&i<lines.length&&!chosen;i++){\n"
    "        match=mdvSourceMatches(lines[i],flq,flmc,re);\n"
    "        for(j=0;j<match.ranges.length;j++){\n"
    "            best={parts:match.parts,range:match.ranges[j]};\n"
    "            if(ordinal--===0){chosen=best;break;}\n"
    "        }\n"
    "    }\n"
    "    chosen=chosen||best;\n"
    "    if(chosen){\n"
    "        mdvMarkSource(chosen.parts,[chosen.range]);\n"
    "        mdvSplitFindMarks=chosen.range.marks;\n"
    "        for(i=0;i<mdvSplitFindMarks.length;i++)mdvSplitFindMarks[i].setAttribute('class','hl hl-a');\n"
    "    }\n"
    "    if(mdvSplitFindMarks.length)mdvRevealFind(mdvSplitFindMarks[0],pane,off,true);\n"
    "    else mdvRevealFind(lines[first],pane,off,false);\n"
    "}\n"
    "function ufh(){\n"
    "    mdvClearFindMarks(mdvSplitFindMarks);mdvSplitFindMarks=[];\n"
    "    for(var i=0;i<fm.length;i++){\n"
    "        var marks=fm[i].nodeType?[fm[i]]:fm[i];\n"
    "        for(var j=0;j<marks.length;j++)marks[j].setAttribute('class',i===fi?'hl hl-a':'hl');\n"
    "    }\n"
    "    if(fi>=0&&fi<fm.length){\n"
    "        var el=fm[fi].nodeType?fm[fi]:fm[fi][0], fb=document.getElementById('mdv-fb');\n"
    "        var off=mdvHasClass(fb,'on')?fb.offsetHeight+10:20;\n"
    "        mdvRevealFind(el,mdvScrollPane(),off,mdvViewMode===1);\n"
    "        if(mdvIsSplit())mdvSyncFindToSource(el,off);\n"
    "    }\n"
    "    var c=document.getElementById('mdv-fc');\n"
    "    if(fm.length>0)c.innerText=(fi+1)+' of '+fm.length;\n"
    "    else c.innerText=document.getElementById('mdv-fi').value?'No matches':'';\n"
    "}\n"
    "function fn(){if(fm.length===0)return;fi=(fi+1)%fm.length;ufh()}"
    "function fp(){if(fm.length===0)return;fi=(fi-1+fm.length)%fm.length;ufh()}"
    "function sf(){var b=document.getElementById('mdv-fb');b.className='on';"
    "var inp=document.getElementById('mdv-fi');inp.focus();inp.select()}"
    "function hf(){document.getElementById('mdv-fb').className='';cf()}"
    "function qf(force){var inp=document.getElementById('mdv-fi'),v,cfg;if(!inp)return 0;v=inp.value||'';cfg=fcfg();"
    "if(cfg.manual&&force!==2){if(v!==flq||flmc!==cfg.mc||flww!==cfg.ww)cf();ufmsg(v?'Press Start searching':'');return 0;}"
    "if(!force&&v.length<2){cf();ufmsg(v?'Type 2+ chars':'');return 0;}"
    "if(force&&v.length<1){cf();return 0;}"
    "df(v,cfg.mc,cfg.ww);return fm.length}"
    "function fstep(back){var inp=document.getElementById('mdv-fi'),v=inp?(inp.value||''):'',cfg=fcfg();"
    "if(!v){sf();return false;}"
    "if(!fm||fm.length===0||flq!==v||flmc!==cfg.mc||flww!==cfg.ww){if(cfg.manual){ufmsg('Press Start searching');return false;}if(!qf(1))return false;return false;}"
    "if(back)fp();else fn();return false}"
    "function hkf(back){var inp=document.getElementById('mdv-fi'),v=inp?(inp.value||''):'';"
    "if(!v){sf();return false;}return fstep(back)}"
    "function f7f(){var inp=document.getElementById('mdv-fi'),b=document.getElementById('mdv-fb'),v=inp?(inp.value||''):'';"
    "if(v){if(!b||b.className.indexOf('on')<0)sf();return fstep(0);}sf();return false}"
    "function fitImages(){var imgs=mdvQSA(document,'img'),de=document.documentElement,b=document.body,"
    "vh=window.innerHeight||(de&&de.clientHeight)||(b&&b.clientHeight)||0,"
    "mh=vh>48?vh-32:vh;"
    "for(var i=0;i<imgs.length;i++){var im=imgs[i];"
    "if(!im._mdvFitHook){im._mdvFitHook=1;im.onload=function(){fitImages()};}"
    "if(mw===0){im.style.maxWidth='100%';im.style.maxHeight=mh>0&&!im.getAttribute('width')&&!im.getAttribute('height')?mh+'px':'none';}"
    "else{im.style.maxWidth='none';im.style.maxHeight='none';}}}"

    /* Help */
    "function th(){var h=document.getElementById('mdv-help');h.className=h.className==='on'?'':'on'}"
    "function mdvStopBubble(e){e=e||window.event;if(e.stopPropagation)e.stopPropagation();e.cancelBubble=true;return true}"

    "function mdvSourceZoom(){var pane=mdvRawPane();if(pane)pane.style.fontSize=(mdvRawFont*fs/zdef)+'pt';}"
    "function mdvApplyWrap(){var b=document.body,c=(b.className||'').replace(/\\bmdv-wrap\\b/g,'');"
    "b.className=c+(mdvWordWrap?' mdv-wrap':'');if(mdvWordWrap)mdvRawPane().scrollLeft=0;}"
    "function mdvToggleWrap(){mdvWordWrap=mdvWordWrap?0:1;mdvApplyWrap();up();toast(mdvWordWrap?'Source word wrap ON':'Source word wrap OFF');}"
    "function mdvSetView(mode){var b=document.body,r=mdvRawPane(),old=mdvViewMode,c,toc,query=flq;"
    "mdvInitialView=mode;if(mode===old)return;"
    "if(old!==1)mdvPreviewY=mdvScrollY();"
    "if(old!==0){mdvSourceY=r.scrollTop;mdvSourceX=r.scrollLeft;}"
    "mdvViewMode=mode;mdvRawActive=mode===1||(mode===2&&old===1);"
    "mdvRawSelStart=mdvRawSelEnd=-1;mdvRawDragging=mdvRenderDragging=0;mdvRestorePaneSelectability();"
    "c=(b.className||'').replace(/\\b(?:mdv-split|mdv-code)\\b/g,'').replace(/^\\s+|\\s+$/g,'');"
    "b.className=c+(mode===2?' mdv-split':mode===1?' mdv-code':'');"
    "if(mode===1){toc=document.getElementById('mdv-toc');if(toc)toc.className='';b.style.marginRight='0';}"
    "mdvSourceZoom();"
    "if(mode!==1){if(old===1)initMermaid();syncMermaidTypography();fitImages();}"
    "if(mode!==0){r.scrollTop=mdvSourceY;r.scrollLeft=mdvWordWrap?0:mdvSourceX;}"
    "if(mode!==1)mdvSetScrollY(mdvPreviewY);"
    "if(query){if(mode===1||old===1)df(query,flmc,flww);else ufh();}"
    "up();}"
    "function mdvSetSplit(on){mdvSetView(on?2:0)}"
    "function mdvSelectElement(el){var r,s;if(!el)return;if(document.body&&document.body.createTextRange){r=document.body.createTextRange();r.moveToElementText(el);r.select();return;}if(document.createRange&&window.getSelection){r=document.createRange();r.selectNodeContents(el);s=window.getSelection();if(s.removeAllRanges)s.removeAllRanges();s.addRange(r);}}"
    "function mdvSelectActive(){var raw=mdvUsingSource(),r=mdvRawPane(),es=mdvRawLines();if(raw){mdvRawSelStart=0;mdvRawSelEnd=es.length?es.length-1:0;}mdvSelectElement(raw?r:document.getElementById('mdv-ct'))}"
    "function mdvCopyActive(){var t;if(mdvUsingSource()){t=mdvRawSelectedText();if(t&&window.clipboardData&&window.clipboardData.setData){window.clipboardData.setData('Text',t);return;}}document.execCommand&&document.execCommand('copy')}"
    "function mdvSetSelectable(p,on){var es,i;if(!p)return;p.unselectable=on?'off':'on';p.style.msUserSelect=on?'':'none';p.style.userSelect=on?'':'none';"
    "es=p.getElementsByTagName('*');for(i=0;i<es.length;i++){es[i].unselectable=on?'off':'on';es[i].style.msUserSelect=on?'':'none';es[i].style.userSelect=on?'':'none';}}"
    "function mdvPaneShield(){var s=document.getElementById('mdv-pane-shield');if(!s){s=document.createElement('div');s.id='mdv-pane-shield';"
    "s.unselectable='on';s.onselectstart=function(){return false};s.onmousedown=function(){return false};s.onmousemove=function(){return false};"
    "s.onmouseup=function(e){if(mdvRawDragging)mdvRawDragEnd(e||window.event);else mdvEndPaneDrag();return false};document.body.appendChild(s);}return s;}"
    "function mdvHidePaneShield(){var s=document.getElementById('mdv-pane-shield');if(s)s.style.display='none';}"
    "function mdvRestorePaneSelectability(){mdvPaneRestoreTimer=null;mdvHidePaneShield();mdvSetSelectable(mdvRenderPane(),1);mdvSetSelectable(mdvRawPane(),1);}"
    "function mdvShowPaneShield(p){var s,rr;if(!p||!mdvIsSplit())return;s=mdvPaneShield();rr=p.getBoundingClientRect();"
    "s.style.position='fixed';s.style.left=rr.left+'px';s.style.top=rr.top+'px';s.style.width=Math.max(0,rr.right-rr.left)+'px';s.style.height=Math.max(0,rr.bottom-rr.top)+'px';"
    "s.style.zIndex='2147483000';s.style.backgroundColor='#fff';s.style.filter='alpha(opacity=1)';s.style.opacity='0.01';s.style.cursor='default';s.style.display='block';}"
    "function mdvEndPaneDrag(){mdvRawDragging=0;mdvRenderDragging=0;if(mdvPaneRestoreTimer)clearTimeout(mdvPaneRestoreTimer);mdvPaneRestoreTimer=setTimeout(mdvRestorePaneSelectability,30);}"
    "function mdvRenderDragStart(e){e=e||window.event;mdvRawActive=0;mdvRenderDragging=1;mdvRawDragging=0;mdvRawSelStart=-1;mdvRawSelEnd=-1;mdvLastX=e.clientX;mdvLastY=e.clientY;mdvSetSelectable(mdvRawPane(),0);mdvShowPaneShield(mdvRawPane());}"
    "function mdvRawDragStart(e){e=e||window.event;mdvRawActive=1;mdvRawDragging=1;mdvRenderDragging=0;mdvRawSelStart=mdvRawSelEnd=mdvRawPointLine(e);mdvLastX=e.clientX;mdvLastY=e.clientY;mdvSetSelectable(mdvRenderPane(),0);mdvShowPaneShield(mdvRenderPane());}"
    "function mdvRawDragMove(e){if(!mdvRawDragging)return;e=e||window.event;mdvRawSelEnd=mdvRawPointLine(e);}"
    "function mdvRawDragEnd(e){if(!mdvRawDragging){mdvEndPaneDrag();return;}e=e||window.event;mdvRawActive=1;mdvRawSelEnd=mdvRawPointLine(e);mdvLastX=e.clientX;mdvLastY=e.clientY;mdvEndPaneDrag();}"

    /* Progress */
    "function up(){var b=document.getElementById('mdv-prog'),d=mdvScrollPane()||document.body,"
    "st=mdvScrollY(),sh=d?d.scrollHeight-d.clientHeight:0;"
    "if(sh>0)b.style.width=(st/sh*100)+'%';else b.style.width='0'}"
    "window.onscroll=up;"

    "function shEscape(text){\n"
    "    return text.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');\n"
    "}\n"
    "function shSpan(text,cls){\n"
    "    var escaped=shEscape(text);\n"
    "    return cls?'<span class=\"'+cls+'\">'+escaped+'</span>':escaped;\n"
    "}\n"
    "function shMarkup(text){\n"
    "    var result=[],at=0,m;\n"
    "    var tag=/<!--[\\s\\S]*?(?:-->|$)|<\\/?[A-Za-z][\\w:-]*(?:\"[^\"]*\"|'[^']*'|[^<>\"'])*>/g;\n"
    "    var attribute=/\"[^\"]*\"|'[^']*'|[\\w:-]+(?=[ \\t\\r\\n]*=)/g;\n"
    "    while((m=tag.exec(text))!==null){\n"
    "        result.push(shEscape(text.slice(at,m.index)));\n"
    "        var token=m[0];\n"
    "        if(token.indexOf('<!--')===0)result.push(shSpan(token,'sh-cm'));\n"
    "        else{\n"
    "            var head=/^(<\\/?)([\\w:-]+)/.exec(token),rest=token.slice(head[0].length),cursor=0,a;\n"
    "            result.push(shEscape(head[1]),shSpan(head[2],'sh-tag'));\n"
    "            attribute.lastIndex=0;\n"
    "            while((a=attribute.exec(rest))!==null){\n"
    "                result.push(shEscape(rest.slice(cursor,a.index)));\n"
    "                result.push(shSpan(a[0],/^[\"']/.test(a[0])?'sh-str':'sh-attr'));\n"
    "                cursor=attribute.lastIndex;\n"
    "            }\n"
    "            result.push(shEscape(rest.slice(cursor)));\n"
    "        }\n"
    "        at=tag.lastIndex;\n"
    "    }\n"
    "    result.push(shEscape(text.slice(at)));return result.join('');\n"
    "}\n"
    "function shCode(text,lang){\n"
    "    if(lang==='html'||lang==='xml')return shMarkup(text);\n"
    "var kws='';"
    "if(lang==='js'||lang==='javascript'||lang==='typescript'||lang==='ts')"
    "kws='\\\\b(var|let|const|function|return|if|else|for|while|do|switch|case|break|continue|new|this|class|extends|import|export|from|default|try|catch|finally|throw|typeof|instanceof|async|await|yield|of|in|null|undefined|true|false)\\\\b';"
    "else if(lang==='python'||lang==='py')"
    "kws='\\\\b(def|class|return|if|elif|else|for|while|break|continue|import|from|as|try|except|finally|raise|with|yield|lambda|pass|and|or|not|in|is|None|True|False|self|print|global|nonlocal|assert|del)\\\\b';"
    "else if(lang==='csharp'||lang==='cs'||lang==='java'||lang==='rust'||lang==='go')"
    "kws='\\\\b(int|char|float|double|void|long|short|unsigned|signed|struct|enum|union|typedef|sizeof|static|extern|const|volatile|register|auto|return|if|else|for|while|do|switch|case|break|continue|goto|default|class|public|private|protected|virtual|override|new|delete|this|true|false|null|NULL|nullptr|fn|let|mut|pub|impl|use|mod|match|loop|async|await|func|package|import|var|type|interface|defer|range|select|chan)\\\\b';"
    "else if(lang==='sql')"
    "kws='\\\\b(SELECT|FROM|WHERE|INSERT|UPDATE|DELETE|CREATE|DROP|ALTER|TABLE|INDEX|JOIN|LEFT|RIGHT|INNER|OUTER|ON|AND|OR|NOT|IN|IS|NULL|AS|ORDER|BY|GROUP|HAVING|LIMIT|OFFSET|UNION|ALL|DISTINCT|INTO|VALUES|SET|BEGIN|COMMIT|ROLLBACK|EXISTS|BETWEEN|LIKE|COUNT|SUM|AVG|MAX|MIN|select|from|where|insert|update|delete|create|drop|alter|table|join|left|right|inner|outer|on|and|or|not|in|is|null|as|order|by|group|having|limit)\\\\b';"
    "else if(lang==='bash'||lang==='sh'||lang==='shell'||lang==='zsh')"
    "kws='\\\\b(if|then|else|elif|fi|for|while|do|done|case|esac|in|function|return|local|export|source|echo|exit|cd|ls|grep|sed|awk|cat|rm|mkdir|cp|mv|chmod|chown|sudo|apt|yum|pip|npm)\\\\b';"
    "else if(lang==='yaml'||lang==='yml')"
    "kws='\\\\b(true|false|null|yes|no|on|off)\\\\b';"
    "else if(lang==='css'||lang==='scss'||lang==='less')"
    "kws='\\\\b(color|background|margin|padding|border|font|display|position|width|height|top|left|right|bottom|flex|grid|none|block|inline|relative|absolute|fixed|inherit|auto|important|solid|transparent)\\\\b';"
    "else if(lang==='php')"
    "kws='\\\\b(function|return|if|else|elseif|for|foreach|while|do|switch|case|break|continue|class|public|private|protected|static|new|echo|print|null|true|false|array|isset|empty|unset|require|include|use|namespace|try|catch|finally|throw|var)\\\\b';"
    "    var keywords=kws?new RegExp(kws):null;\n"
    "    var lineComment=lang==='sql'?'--[^\\r\\n]*':'//[^\\r\\n]*';\n"
    "    var hashComment=/^(python|py|bash|sh|shell|zsh|yaml|yml|php)$/.test(lang)?'|^[ \\t]*#[^\\r\\n]*':'';\n"
    "    var token=new RegExp('(\\\\/\\\\*[\\\\s\\\\S]*?(?:\\\\*\\\\/|$)|'+lineComment+hashComment+')'+\n"
    "        '|(\"(?:[^\"\\\\\\\\\\\\r\\\\n]|\\\\\\\\.)*\"|\\\\x27(?:[^\\\\x27\\\\\\\\\\\\r\\\\n]|\\\\\\\\.)*\\\\x27|\\\\x60(?:[^\\\\x60\\\\\\\\]|\\\\\\\\.)*\\\\x60)'+\n"
    "        '|(\\\\b(?:0[xX][0-9a-fA-F]+|\\\\d+(?:\\\\.\\\\d+)?(?:[eE][+-]?\\\\d+)?)\\\\b)'+\n"
    "        '|(\\\\b[A-Za-z_][A-Za-z0-9_]*\\\\b)','gm');\n"
    "    var result=[],at=0,m;\n"
    "    while((m=token.exec(text))!==null){\n"
    "        result.push(shEscape(text.slice(at,m.index)));\n"
    "        var cls=m[1]?'sh-cm':m[2]?'sh-str':m[3]?'sh-num':keywords&&keywords.test(m[0])?'sh-kw':null;\n"
    "        if(!cls&&/^[ \\t]*\\(/.test(text.slice(token.lastIndex)))cls='sh-fn';\n"
    "        result.push(shSpan(m[0],cls));at=token.lastIndex;\n"
    "    }\n"
    "    result.push(shEscape(text.slice(at)));return result.join('');\n"
    "}\n"
    "function shAll(){\n"
    "    var blocks=mdvQSA(document,'pre code[class]');\n"
    "    for(var i=0;i<blocks.length;i++){\n"
    "        var el=blocks[i],lang=(el.className||'').replace('language-','').toLowerCase();\n"
    "        if(!lang||el.getAttribute('data-mdv-highlighted')==='1')continue;\n"
    "        el.innerHTML=shCode(mdvTextValue(el),lang);\n"
    "        el.setAttribute('data-mdv-highlighted','1');\n"
    "    }\n"
    "}\n"

    /* Expand/collapse for long blocks */
    "function initCollapse(){"
    "var blocks=mdvQSA(document,'pre,blockquote');"
    "for(var i=0;i<blocks.length;i++){"
    "var b=blocks[i];if(b.id==='mdv-raw-pane')continue;if(b.scrollHeight>420){"
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

    /* Keyboard handler (backup - primary interception is via IE subclass) */
    "function mdvSupportsDetails(){var d=document.createElement?document.createElement('details'):null;return !!(d&&typeof d.open!=='undefined')}"
    "function mdvSetDetailsState(d,open){var i,n,seen=0;if(!d)return;for(i=0;i<d.childNodes.length;i++){n=d.childNodes[i];if(n.nodeType===1&&n.tagName==='SUMMARY'){seen=1;continue;}if(!seen)continue;if(n.nodeType===1)n.style.display=open?'':'none';}if(open){if(d.setAttribute)d.setAttribute('open','open');else d.open=true;}else{if(d.removeAttribute)d.removeAttribute('open');else d.open=false;}}"
    "function initDetailsFallback(){var ds,i,d,ss,s,open;"
    "if(mdvSupportsDetails())return;"
    "ds=document.getElementsByTagName('details');"
    "for(i=0;i<ds.length;i++){d=ds[i];ss=d.getElementsByTagName('summary');if(!ss||ss.length===0)continue;s=ss[0];"
    "open=(d.getAttribute&&d.getAttribute('open')!==null)||d.open?1:0;mdvSetDetailsState(d,open);"
    "s.onclick=(function(det){return function(e){e=e||window.event;if(e.preventDefault)e.preventDefault();else e.returnValue=false;mdvSetDetailsState(det,!(det.getAttribute&&det.getAttribute('open')!==null));return false;};})(d);"
    "}}"
    );
    sb_append(sb,
    "function mmTrim(s){return s?s.replace(/^\\s+|\\s+$/g,''):''}"
    "function mmNodeToken(s){var m=/^([A-Za-z0-9_:-]+)(.*)$/.exec(mmTrim(s)),mm;if(!m)return null;"
    "var id=m[1],rest=mmTrim(m[2]),text=id,shape='rect';"
    "if(rest){"
    "if((mm=/^\\[\\((.*)\\)\\]$/.exec(rest))){text=mm[1];shape='database'}\n"
    "else if((mm=/^\\[(.*)\\]$/.exec(rest)))text=mm[1];\n"
    "else if((mm=/^\\(\\[([^\\]]*)\\]\\)$/.exec(rest))){text=mm[1];shape='round'}"
    "else if((mm=/^\\(\\((.*)\\)\\)$/.exec(rest))){text=mm[1];shape='circle'}"
    "else if((mm=/^\\((.*)\\)$/.exec(rest))){text=mm[1];shape='round'}"
    "else if((mm=/^\\{([^}]*)\\}$/.exec(rest))){text=mm[1];shape='diamond'}"
    "else return null;}"
    "text=mmTrim(text);if(text.charAt(0)==='\"'&&text.charAt(text.length-1)==='\"')text=text.slice(1,-1);\n"
    "return{id:id,text:text||id,shape:shape}}\n"
    "function mmEnsureNode(ctx,s){var p=mmNodeToken(s),n;if(!p)return null;"
    "n=ctx.map[p.id];if(!n){n={id:p.id,text:p.text,shape:p.shape};ctx.map[p.id]=n;ctx.nodes.push(n);}"
    "else{if(p.text&&p.text!==p.id)n.text=p.text;if(n.shape==='rect'&&p.shape!=='rect')n.shape=p.shape;}return n}"
    "function mmParseFlowLine(ctx,line){\n"
    "    var stack=[], quote=false, label='', left=null, next, edge=null, start=0, i=0;\n"
    "    var close={'[':']','(':')','{':'}'}, arrow=/^(-\\.->|-->|---|==>)/;\n"
    "    while(i<=line.length){\n"
    "        var ch=line.charAt(i), match=null;\n"
    "        if(!quote&&!stack.length){\n"
    "            if(i===line.length||ch===';'||(match=arrow.exec(line.substr(i)))){\n"
    "                var token=mmTrim(line.substring(start,i));\n"
    "                if(token){\n"
    "                    next=mmEnsureNode(ctx,token);if(!next)return false;\n"
    "                    if(edge)ctx.edges.push({from:left.id,to:next.id,kind:edge,label:label});\n"
    "                    left=next;\n"
    "                }else if(edge||match)return false;\n"
    "                if(!match){edge=null;left=null;start=++i;continue;}\n"
    "                edge=match[1];i+=edge.length;label='';\n"
    "                var rest=/^\\s*(?:\\|([^|]*)\\|)?\\s*/.exec(line.substr(i));\n"
    "                i+=rest[0].length;label=mmTrim(rest[1]||'');start=i;continue;\n"
    "            }\n"
    "        }\n"
    "        if(ch==='\"' && line.charAt(i-1)!=='\\\\')quote=!quote;\n"
    "        else if(!quote&&close[ch])stack.push(close[ch]);\n"
    "        else if(!quote && ch===stack[stack.length-1])stack.pop();\n"
    "        i++;\n"
    "    }\n"
    "    return !quote&&!stack.length;\n"
    "}\n"
    "function mmSvgEl(name){return document.createElementNS('http://www.w3.org/2000/svg',name)}\n"
    "function mmApplySvgTextStyle(el){var cs,fz,ff;if(!el)return;cs=window.getComputedStyle?window.getComputedStyle(document.body,null):null;"
    "ff=cs&&cs.fontFamily?cs.fontFamily:document.body.style.fontFamily;fz=cs&&cs.fontSize?cs.fontSize:document.body.style.fontSize;"
    "if(ff){el.style.fontFamily=ff;try{el.setAttribute('font-family',ff);}catch(ex){}}"
    "if(fz){el.style.fontSize=fz;try{el.setAttribute('font-size',fz);}catch(ex){}}}"
    "function mmWrapWords(text,maxChars){var words,lines,line,i,w;if(!text)return[''];if(!(maxChars>4))maxChars=16;"
    "words=mmTrim(text).split(/\\s+/);lines=[];line='';for(i=0;i<words.length;i++){w=words[i];"
    "if(!line){line=w;continue;}if((line+' '+w).length<=maxChars)line+=' '+w;else{lines.push(line);line=w;}}"
    "if(line)lines.push(line);if(lines.length===0)lines.push(text);return lines;}"
    "function mmPushText(svg,x,y,lines,cls,anchor){"
    "var step=mmBodyFontPx()*1.25;"
    "for(var i=0;i<lines.length;i++){var t=mmSvgEl('text');t.setAttribute('x',x);t.setAttribute('y',y+i*step);t.setAttribute('text-anchor',anchor||'middle');t.setAttribute('class',cls);mmApplySvgTextStyle(t);t.appendChild(document.createTextNode(lines[i]));svg.appendChild(t);}}"
    "function mmParseFlow(src){"
    "var ctx={kind:'flow',dir:'TD',nodes:[],edges:[],map:{}},lines,segs,m,i,j,seenHeader=0;"
    "if(!src||src.length>8192)return null;lines=src.replace(/\\r/g,'').split('\\n');"
    "for(i=0;i<lines.length;i++){var line=mmTrim(lines[i]);if(!line||line.substr(0,2)==='%%')continue;"
    "if(!seenHeader){m=/^(graph|flowchart)(?:\\s+(TD|TB|BT|LR|RL))?\\s*;?$/i.exec(line);\n"
    "if(!m)return null;ctx.dir=(m[2]||'TD').toUpperCase();seenHeader=1;continue;}\n"
    "if(!mmParseFlowLine(ctx,line)||ctx.nodes.length>64||ctx.edges.length>128)return null;}\n"
    "if(!seenHeader||ctx.nodes.length===0)return null;"
    "return ctx}\n"
    "\n"
    "// Lay out one forward rank at a time. Dummy vertices reserve clear lanes for\n"
    "// edges that skip ranks. DFS marks cycle edges for routing around the outside.\n"
    "function mmRenderGraph(view,ctx){\n"
    "    var nodes=ctx.nodes||ctx.classes||ctx.states, edges=ctx.edges, f=mmBodyFontPx();\n"
    "    var horizontal=/^(LR|RL)$/.test(ctx.dir), reverse=/^(RL|BT)$/.test(ctx.dir);\n"
    "    var markerRoom=ctx.kind==='er'?18:ctx.kind==='requirement'?10:0;\n"
    "    var ranks=[], order=[], ends=[], maxSpan=0, crossGap=f*2, rankGap=f*3+markerRoom*2, rails=0;\n"
    "    var i,j,k,n,e,row,span,major=0,svg=mmSvgEl('svg'), defs=mmSvgEl('defs'), labels=[];\n"
    "    function visit(node){\n"
    "        node.visit=1;\n"
    "        for(var p=0;p<node.out.length;p++){\n"
    "            var edge=node.out[p], target=edge.target;\n"
    "            if(target.visit===1)edge.back=true;\n"
    "            if(!target.visit)visit(target);\n"
    "        }\n"
    "        node.visit=2;order.push(node);\n"
    "    }\n"
    "    function add(rank,node){\n"
    "        if(!ranks[rank])ranks[rank]=[];\n"
    "        node.rank=rank;ranks[rank].push(node);\n"
    "    }\n"
    "    for(i=0;i<nodes.length;i++){\n"
    "        n=nodes[i];n.rank=0;n.out=[];n.input=[];n.visit=0;\n"
    "        n.lines=n.members?[n.title].concat(n.members):(n.text||n.label||'').split(/<br\\s*\\/?>/i);\n"
    "        span=0;for(j=0;j<n.lines.length;j++)span=Math.max(span,mmTextWidth(n.lines[j],f));\n"
    "        n.w=Math.max(f*3,span+f*1.6);n.h=f*(1.4+n.lines.length*1.25);\n"
    "        if(n.shape==='diamond'){n.w*=1.65;n.h*=1.35;}\n"
    "        if(n.shape==='circle')n.w=n.h=Math.max(n.w,n.h);\n"
    "        if(n.pseudo)n.w=n.h=f*1.15;\n"
    "        n.crossSize=horizontal?n.h:n.w;n.majorSize=horizontal?n.w:n.h;\n"
    "    }\n"
    "    for(i=0;i<edges.length;i++){\n"
    "        e=edges[i];e.source=ctx.map[e.from];e.target=ctx.map[e.to];e.ordinal=i;\n"
    "        e.source.out.push(e);e.target.input.push(e);\n"
    "        if(horizontal&&e.label)rankGap=Math.max(rankGap,mmTextWidth(e.label,f)+f*2+markerRoom*2);\n"
    "    }\n"
    "    for(i=0;i<nodes.length;i++)if(!nodes[i].visit)visit(nodes[i]);\n"
    "    for(i=order.length-1;i>=0;i--){\n"
    "        n=order[i];\n"
    "        for(j=0;j<n.out.length;j++){\n"
    "            e=n.out[j];if(!e.back)e.target.rank=Math.max(e.target.rank,n.rank+1);\n"
    "        }\n"
    "    }\n"
    "    for(i=0;i<nodes.length;i++)add(nodes[i].rank,nodes[i]);\n"
    "    for(i=0;i<edges.length;i++){\n"
    "        e=edges[i];e.chain=[e.source];\n"
    "        if(e.back){e.rail=++rails;continue;}\n"
    "        for(j=e.source.rank+1;j<e.target.rank;j++){\n"
    "            n={crossSize:f*.65,majorSize:0,previous:e.chain[e.chain.length-1]};\n"
    "            add(j,n);e.chain.push(n);\n"
    "        }\n"
    "        e.chain.push(e.target);\n"
    "    }\n"
    "    for(i=0;i<ranks.length;i++){\n"
    "        row=ranks[i];span=0;\n"
    "        for(j=0;j<row.length;j++){row[j].slot=j;}\n"
    "        if(i>0){\n"
    "            for(j=0;j<row.length;j++){\n"
    "                n=row[j];var sum=0,count=0;\n"
    "                if(n.previous){sum=n.previous.slot;count=1;}\n"
    "                else for(k=0;k<n.input.length;k++){\n"
    "                    e=n.input[k];if(!e.back){sum+=e.chain[e.chain.length-2].slot;count++;}\n"
    "                }\n"
    "                n.weight=count?sum/count:n.slot;\n"
    "            }\n"
    "            row.sort(function(a,b){return a.weight-b.weight||a.slot-b.slot;});\n"
    "        }\n"
    "        for(j=0;j<row.length;j++){row[j].slot=j;span+=row[j].crossSize;}\n"
    "        row.span=span+crossGap*(row.length-1);maxSpan=Math.max(maxSpan,row.span);\n"
    "    }\n"
    "    for(i=0;i<ranks.length;i++){\n"
    "        row=ranks[i];span=(maxSpan-row.span)/2;var depth=0;\n"
    "        for(j=0;j<row.length;j++)depth=Math.max(depth,row[j].majorSize);\n"
    "        ends[i]=[major,major+depth];\n"
    "        for(j=0;j<row.length;j++){\n"
    "            n=row[j];n.cross=span+n.crossSize/2;n.major=major+depth/2;\n"
    "            span+=n.crossSize+crossGap;\n"
    "        }\n"
    "        major+=depth+rankGap;\n"
    "    }\n"
    "    var extent=major-rankGap;\n"
    "    function xy(c,m){if(reverse)m=extent-m;return horizontal?[m,c]:[c,m];}\n"
    "    function point(c,m){return xy(c,m).join(',');}\n"
    "    function port(node,edge,out){\n"
    "        var links=out?node.out:node.input, slot=0, opposite=out?'target':'source';\n"
    "        for(var q=0;q<links.length;q++){\n"
    "            var other=links[q], d=other[opposite].cross-edge[opposite].cross;\n"
    "            if(d<0||(d===0&&other.ordinal<edge.ordinal))slot++;\n"
    "        }\n"
    "        var offset=(slot-(links.length-1)/2)*Math.min(f*.7,node.crossSize*.6/links.length);\n"
    "        var half=node.majorSize/2;\n"
    "        if(node.shape==='diamond')half*=1-Math.abs(offset)*2/node.crossSize;\n"
    "        if(node.shape==='circle'||node.pseudo)half*=Math.sqrt(1-Math.pow(offset*2/node.crossSize,2));\n"
    "        return [node.cross+offset,node.major+(out?half:-half)];\n"
    "    }\n"
    "    function curve(c1,m1,c2,m2){\n"
    "        var mid=(m1+m2)/2;\n"
    "        return ' C'+point(c1,mid)+' '+point(c2,mid)+' '+point(c2,m2);\n"
    "    }\n"
    "    function marker(name,shape,atStart,hollow){\n"
    "        var mk=mmSvgEl('marker'),path=mmSvgEl('path'),id=mmMarkerId+'-'+name;\n"
    "        mk.setAttribute('id',id);mk.setAttribute('markerUnits','userSpaceOnUse');\n"
    "        mk.setAttribute('viewBox','-1 -1 13 10');mk.setAttribute('markerWidth',13);mk.setAttribute('markerHeight',10);\n"
    "        mk.setAttribute('refX',atStart?0:10);mk.setAttribute('refY',4);mk.setAttribute('orient','auto');\n"
    "        path.setAttribute('d',shape);path.setAttribute('class',hollow?'mdv-mm-marker hollow':'mdv-mm-marker');\n"
    "        mk.appendChild(path);defs.appendChild(mk);return 'url(#'+id+')';\n"
    "    }\n"
    "    mmMarkerId++;\n"
    "    var arrow=marker('arrow','M0,0 L10,4 L0,8 z',false,false);\n"
    "    var parent=marker('parent','M10,0 L0,4 L10,8 z',true,true);\n"
    "    var child=marker('child','M0,0 L10,4 L0,8 z',false,true);\n"
    "    var aggregate=marker('aggregate','M0,4 L5,0 L10,4 L5,8 z',true,true);\n"
    "    var compose=marker('compose','M0,4 L5,0 L10,4 L5,8 z',true,false);\n"
    "    var aggregateEnd=marker('aggregate-end','M0,4 L5,0 L10,4 L5,8 z',false,true);\n"
    "    var composeEnd=marker('compose-end','M0,4 L5,0 L10,4 L5,8 z',false,false);\n"
    "    // Draw open marker strokes and mask the link under any hollow circle.\n"
    "    function lineMarker(name,shape,atStart,refX,circle){\n"
    "        var id=mmMarkerId+'-'+name;\n"
    "        var mk=mmShape(defs,'marker',{id:id,markerUnits:'userSpaceOnUse',viewBox:'-1 -1 20 12',\n"
    "            markerWidth:20,markerHeight:12,refX:atStart?0:refX,refY:5,orient:'auto'},'');\n"
    "        var group=atStart?mmShape(mk,'g',{transform:'translate('+refX+' 0) scale(-1 1)'},''):mk;\n"
    "        if(circle)mmShape(group,'circle',{cx:circle,cy:5,r:3},'mdv-mm-marker hollow');\n"
    "        mmShape(group,'path',{d:shape,fill:'none'},'mdv-mm-marker open');\n"
    "        return 'url(#'+id+')';\n"
    "    }\n"
    "    var cardsStart={},cardsEnd={}, contains, dependency;\n"
    "    if(ctx.kind==='er'){\n"
    "        var cards={'01':'M14 1 V9','11':'M7 1 V9 M14 1 V9',\n"
    "            '0n':'M10 5 L17 1 M10 5 L17 9','1n':'M7 1 V9 M10 5 L17 1 M10 5 L17 9'};\n"
    "        for(var card in cards){\n"
    "            var circle=card.charAt(0)==='0'?5:0;\n"
    "            cardsStart[card]=lineMarker('card-start-'+card,cards[card],true,17,circle);\n"
    "            cardsEnd[card]=lineMarker('card-end-'+card,cards[card],false,17,circle);\n"
    "        }\n"
    "    }else if(ctx.kind==='requirement'){\n"
    "        contains=lineMarker('contains','M2 5 H8 M5 2 V8',true,8,5);\n"
    "        dependency=lineMarker('dependency','M1 1 L9 5 L1 9',false,9,0);\n"
    "    }\n"
    "    svg.appendChild(defs);\n"
    "    for(i=0;i<edges.length;i++){\n"
    "        e=edges[i];var a=port(e.source,e,true), b=port(e.target,e,false);\n"
    "        var path=mmSvgEl('path'), d='M'+point(a[0],a[1]), lc,lm;\n"
    "        if(e.back){\n"
    "            var rail=-crossGap-e.rail*f*1.1;\n"
    "            var bottom=ends[e.source.rank][1]+rankGap*.27+markerRoom;\n"
    "            var top=ends[e.target.rank][0]-rankGap*.27-markerRoom;\n"
    "            var exit=ends[e.source.rank][1]+f*.12+markerRoom, entry=ends[e.target.rank][0]-f*.12-markerRoom;\n"
    "            d+=' L'+point(a[0],exit)+curve(a[0],exit,rail,bottom)+' L'+point(rail,top)+curve(rail,top,b[0],entry)+' L'+point(b[0],b[1]);\n"
    "            lc=rail;lm=(top+bottom)/2;\n"
    "        }else{\n"
    "            var c=a[0], length=e.chain.length;\n"
    "            for(j=1;j<length;j++){\n"
    "                var next=e.chain[j], out=ends[next.rank-1][1]+f*.18+markerRoom, into=ends[next.rank][0]-f*.18-markerRoom;\n"
    "                var nc=j===length-1?b[0]:next.cross;\n"
    "                d+=' L'+point(c,out)+curve(c,out,nc,into);\n"
    "                if(j===Math.ceil((length-1)/2)){lc=(c+nc)/2;lm=(out+into)/2;}\n"
    "                c=nc;\n"
    "            }\n"
    "            d+=' L'+point(b[0],b[1]);\n"
    "        }\n"
    "        var kind=e.kind||'-->';\n"
    "        path.setAttribute('d',d);\n"
    "        path.setAttribute('class','mdv-mm-edge'+(/-\\.->|\\.\\./.test(kind)?' dash':'')+(kind==='==>'?' bold':''));\n"
    "        if(ctx.kind==='er'){\n"
    "            path.setAttribute('marker-start',cardsStart[e.cardStart]);\n"
    "            path.setAttribute('marker-end',cardsEnd[e.cardEnd]);\n"
    "        }else if(ctx.kind==='requirement'){\n"
    "            if(kind==='contains')path.setAttribute('marker-start',contains);\n"
    "            else path.setAttribute('marker-end',dependency);\n"
    "        }else{\n"
    "            if(kind.indexOf('<|')===0)path.setAttribute('marker-start',parent);\n"
    "            else if(kind.charAt(0)==='o')path.setAttribute('marker-start',aggregate);\n"
    "            else if(kind.charAt(0)==='*')path.setAttribute('marker-start',compose);\n"
    "            if(/\\|>$/.test(kind))path.setAttribute('marker-end',child);\n"
    "            else if(/>$/.test(kind))path.setAttribute('marker-end',arrow);\n"
    "            else if(/o$/.test(kind))path.setAttribute('marker-end',aggregateEnd);\n"
    "            else if(/\\*$/.test(kind))path.setAttribute('marker-end',composeEnd);\n"
    "        }\n"
    "        svg.appendChild(path);\n"
    "        if(e.label)labels.push({point:xy(lc,lm),text:e.label});\n"
    "    }\n"
    "    for(i=0;i<labels.length;i++){\n"
    "        var label=labels[i], x=label.point[0],y=label.point[1];\n"
    "        var w=mmTextWidth(label.text,f)+f*.6;\n"
    "        mmShape(svg,'rect',{x:x-w/2,y:y-f*.65,width:w,height:f*1.3,rx:3},'mdv-mm-label');\n"
    "        mmPushText(svg,x,y+f*.34,[label.text],'mdv-mm-text');\n"
    "    }\n"
    "    for(i=0;i<nodes.length;i++){\n"
    "        n=nodes[i];var p=xy(n.cross,n.major), x=p[0],y=p[1], w=n.w,h=n.h;\n"
    "        if(n.pseudo||n.shape==='circle')mmShape(svg,'ellipse',{cx:x,cy:y,rx:w/2,ry:h/2},'mdv-mm-node');\n"
    "else if(n.shape==='database'){\n"
    "            var cap=f*.35,l=x-w/2,r=x+w/2,t=y-h/2+cap,bottom=y+h/2-cap;\n"
    "            mmShape(svg,'path',{d:'M'+l+','+t+' C'+l+','+(t-cap*1.33)+' '+r+','+(t-cap*1.33)+' '+r+','+t+\n"
    "                ' V'+bottom+' C'+r+','+(bottom+cap*1.33)+' '+l+','+(bottom+cap*1.33)+' '+l+','+bottom+' Z'},'mdv-mm-node');\n"
    "            mmShape(svg,'path',{d:'M'+l+','+t+' C'+l+','+(t+cap*1.33)+' '+r+','+(t+cap*1.33)+' '+r+','+t},'mdv-mm-edge');\n"
    "        }\n"
    "        else if(n.shape==='diamond')mmShape(svg,'polygon',{points:x+','+(y-h/2)+' '+(x+w/2)+','+y+' '+x+','+(y+h/2)+' '+(x-w/2)+','+y},'mdv-mm-node');\n"
    "        else mmShape(svg,'rect',{x:x-w/2,y:y-h/2,width:w,height:h,rx:n.shape==='round'||ctx.kind==='state'?Math.min(f,h/2):0},'mdv-mm-node');\n"
    "        if(!n.pseudo)mmPushText(svg,x,y+f*(.34-(n.lines.length-1)*.625),n.lines,'mdv-mm-text');\n"
    "        if(n.members&&n.members.length)mmShape(svg,'line',{x1:x-w/2,x2:x+w/2,y1:y-h/2+f*2.05,y2:y-h/2+f*2.05},'mdv-mm-edge');\n"
    "    }\n"
    "    view.appendChild(svg);return 1;\n"
    "}\n"
    "var mmMarkerId=0, mmMeasureCanvas;\n"
    "function mmTextWidth(text,font){\n"
    "    if(!mmMeasureCanvas){\n"
    "        var el=document.createElement('canvas');\n"
    "        mmMeasureCanvas=el.getContext&&el.getContext('2d');\n"
    "    }\n"
    "    if(!mmMeasureCanvas)return text.length*font;\n"
    "    var style=window.getComputedStyle(document.body,null);\n"
    "    mmMeasureCanvas.font=font+'px '+style.fontFamily;\n"
    "    return mmMeasureCanvas.measureText(text).width;\n"
    "}\n"
    "function mmShape(svg,name,attrs,cls){\n"
    "    var el=mmSvgEl(name),key;for(key in attrs)el.setAttribute(key,attrs[key]);\n"
    "    el.setAttribute('class',cls);svg.appendChild(el);return el;\n"
    "}\n"
    "function mmFitBounds(svg){\n"
    "    var b=svg.getBBox(), pad=Math.ceil(mmBodyFontPx()*.75);\n"
    "    if(!(b.width>0)||!(b.height>0))return;\n"
    "    svg.setAttribute('viewBox',[b.x-pad,b.y-pad,b.width+2*pad,b.height+2*pad].join(' '));\n"
    "    fitMermaidSvg(svg);\n"
    "}\n"
    "function mmSeqActor(ctx,name,label){var a;if(!name)return null;a=ctx.map[name];if(!a){a={id:name,label:label||name,index:ctx.actors.length};ctx.map[name]=a;ctx.actors.push(a);}else if(label)a.label=label;return a}"
    "function mmParseSequence(src){"
    "var ctx={kind:'sequence',actors:[],map:Object.create(null),items:[]},lines,m,i,line,a,b;"
    "if(!src||src.length>8192)return null;lines=src.replace(/\\r/g,'').split('\\n');"
    "for(i=0;i<lines.length;i++){line=mmTrim(lines[i]);if(!line||line.substr(0,2)==='%%')continue;if(i===0){if(!/^sequenceDiagram\\b/i.test(line))return null;continue;}"
    "if((m=/^(participant|actor)\\s+([A-Za-z0-9_:-]+)(?:\\s+as\\s+(.+))?$/i.exec(line))){mmSeqActor(ctx,m[2],mmTrim(m[3]||''));continue;}"
    "if((m=/^Note\\s+(right|left)\\s+of\\s+([A-Za-z0-9_:-]+)\\s*:\\s*(.+)$/i.exec(line))){a=mmSeqActor(ctx,m[2]);ctx.items.push({type:'note',side:m[1].toLowerCase(),actor:a.id,text:mmTrim(m[3])});continue;}"
    "if((m=/^([A-Za-z0-9_:-]+?)\\s*(-->>|-->|->>|->|==>>|==>)\\s*([A-Za-z0-9_:-]+)\\s*:\\s*(.+)$/i.exec(line))){a=mmSeqActor(ctx,m[1]);b=mmSeqActor(ctx,m[3]);ctx.items.push({type:'msg',from:a.id,to:b.id,kind:m[2],text:mmTrim(m[4])});continue;}return null;}"
    "if(ctx.actors.length===0||ctx.actors.length>16||ctx.items.length>96)return null;return ctx}"
    "function mmRenderSequence(view,ctx){\n"
    "    var svg=mmSvgEl('svg'),defs=mmSvgEl('defs'), mk=mmSvgEl('marker'),f=mmBodyFontPx();\n"
    "    var id='mdv-mm-seq-'+(++mmMarkerId), arrow='url(#'+id+')', x=0, i,j,a,item,step=f*1.25;\n"
    "    var head=ctx.title?f*2.35:0,y=head+f*4.7, width;\n"
    "    for(i=0;i<ctx.actors.length;i++){\n"
    "        a=ctx.actors[i];\n"
    "        a.w=Math.max(f*5,mmTextWidth(a.label,f)+f*1.6,a.caption?mmTextWidth(a.caption,f)+f*1.6:0);\n"
    "        a.left=a.right=a.w/2;\n"
    "    }\n"
    "    // Allocate vertical space from the wrapped text before drawing lifelines.\n"
    "    for(i=0;i<ctx.items.length;i++){\n"
    "        item=ctx.items[i];\n"
    "        if(item.type==='groupEnd'){item.start.endY=y-f*.8;y+=f*.55;continue;}\n"
    "        item.lines=mmWrapWords(item.text,item.type==='note'?22:38);item.y=y;\n"
    "        if(item.type==='note'){\n"
    "            a=ctx.map[item.actor];item.w=0;\n"
    "            for(j=0;j<item.lines.length;j++)item.w=Math.max(item.w,mmTextWidth(item.lines[j],f));\n"
    "            item.w+=f*1.3;a[item.side]=Math.max(a[item.side],item.w+f);\n"
    "        }\n"
    "        y+=item.lines.length*step+f*2.5;\n"
    "    }\n"
    "    mk.setAttribute('id',id);mk.setAttribute('markerWidth','10');mk.setAttribute('markerHeight','10');\n"
    "    mk.setAttribute('refX','9');mk.setAttribute('refY','3');mk.setAttribute('orient','auto');\n"
    "    mmShape(mk,'path',{d:'M0,0 L0,6 L9,3 z'},'mdv-mm-marker');defs.appendChild(mk);svg.appendChild(defs);\n"
    "    for(i=0;i<ctx.actors.length;i++){\n"
    "        a=ctx.actors[i];a.cx=x+a.left;x=a.cx+a.right+f*2;\n"
    "        mmShape(svg,'rect',{x:a.cx-a.w/2,y:head,width:a.w,height:f*2.5,rx:4},'mdv-mm-node');\n"
    "        mmPushText(svg,a.cx,head+f*(a.caption?1.02:1.59),[a.label],'mdv-mm-text');\n"
    "        if(a.caption)mmPushText(svg,a.cx,head+f*2.1,[a.caption],'mdv-mm-text');\n"
    "        mmShape(svg,'line',{x1:a.cx,x2:a.cx,y1:head+f*2.5,y2:y-f},'mdv-mm-life');\n"
    "    }\n"
    "    width=x;\n"
    "    if(ctx.title)mmPushText(svg,width/2,f*1.28,[ctx.title],'mdv-mm-text mdv-mm-chart-heading');\n"
    "    for(i=0;i<ctx.items.length;i++){\n"
    "        item=ctx.items[i];\n"
    "        if(item.type==='group'){\n"
    "            var inset=Math.min(item.depth*f*.38,width*.25);\n"
    "            mmShape(svg,'rect',{x:-f+inset,y:item.y-f,width:width+f-inset*2,height:item.endY-item.y+f},\n"
    "                'mdv-mm-edge dash');\n"
    "            mmPushText(svg,inset,item.y,item.lines,'mdv-mm-text mdv-mm-chart-heading','start');\n"
    "        }else if(item.type==='msg'){\n"
    "            var from=ctx.map[item.from],to=ctx.map[item.to], at=item.y+step*(item.lines.length-1)+f*.55;\n"
    "            var d='M'+from.cx+','+at;\n"
    "            d+=from===to?' h'+f*2+' v'+f+' H'+from.cx:' H'+to.cx;\n"
    "            mmShape(svg,'path',{d:d},'mdv-mm-edge'+(item.kind.indexOf('--')===0?' dash':'')+\n"
    "                (item.kind.indexOf('==')===0?' bold':'')).setAttribute('marker-end',arrow);\n"
    "            mmPushText(svg,from===to?from.cx+f:(from.cx+to.cx)/2,item.y,item.lines,'mdv-mm-text');\n"
    "        }else if(item.type==='note'){\n"
    "            a=ctx.map[item.actor];\n"
    "            var w=item.w;\n"
    "            x=a.cx+(item.side==='right'?f:-f-w);\n"
    "            mmShape(svg,'rect',{x:x,y:item.y-f*1.15,width:w,height:item.lines.length*step+f*.8,rx:3},'mdv-mm-note');\n"
    "            mmPushText(svg,x+f*.65,item.y,item.lines,'mdv-mm-note-text','start');\n"
    "        }\n"
    "    }\n"
    "    view.appendChild(svg);return 1;\n"
    "}\n"
    "function mmClassGet(ctx,name){var c=ctx.map[name];if(!c){c={id:name,title:name,members:[]};ctx.map[name]=c;ctx.classes.push(c);}return c}"
    "function mmParseClass(src){"
    "var ctx={kind:'class',classes:[],edges:[],map:Object.create(null)},lines,i,line,m,inClass=null;"
    "if(!src||src.length>8192)return null;lines=src.replace(/\\r/g,'').split('\\n');"
    "for(i=0;i<lines.length;i++){line=mmTrim(lines[i]);if(!line||line.substr(0,2)==='%%')continue;if(i===0){if(!/^classDiagram\\b/i.test(line))return null;continue;}"
    "if((m=/^direction\\s+(TD|TB|BT|LR|RL)$/i.exec(line))){ctx.dir=m[1].toUpperCase();continue;}\n"
    "if(inClass&&line==='}'){inClass=null;continue;}if(inClass){inClass.members.push(line);continue;}"
    "if((m=/^class\\s+([A-Za-z0-9_:-]+)\\s*\\{$/.exec(line))){inClass=mmClassGet(ctx,m[1]);continue;}"
    "if((m=/^class\\s+([A-Za-z0-9_:-]+)$/.exec(line))){mmClassGet(ctx,m[1]);continue;}"
    "if((m=/^([A-Za-z0-9_:-]+)\\s*:\\s*(.+)$/.exec(line))){mmClassGet(ctx,m[1]).members.push(mmTrim(m[2]));continue;}"
    "if((m=/^([A-Za-z0-9_:-]+)\\s+([.<|o*\\->]+)\\s+([A-Za-z0-9_:-]+)(?:\\s*:\\s*(.+))?$/.exec(line))){mmClassGet(ctx,m[1]);mmClassGet(ctx,m[3]);ctx.edges.push({from:m[1],to:m[3],kind:m[2],label:mmTrim(m[4]||'')});continue;}return null;}"
    "if(inClass||ctx.classes.length===0||ctx.classes.length>24||ctx.edges.length>64)return null;return ctx}"
    "function mmStateNode(ctx,name,label){var n,id=name;if(name==='[*]'){id='[*]#'+(++ctx.autoId);n={id:id,label:label||'',pseudo:1};ctx.map[id]=n;ctx.states.push(n);return n;}n=ctx.map[id];if(!n){n={id:id,label:label||id,pseudo:0};ctx.map[id]=n;ctx.states.push(n);}else if(label)n.label=label;return n}"
    "function mmParseState(src){"
    "var ctx={kind:'state',states:[],edges:[],map:Object.create(null),autoId:0},lines,i,line,m,a,b;"
    "if(!src||src.length>8192)return null;lines=src.replace(/\\r/g,'').split('\\n');"
    "for(i=0;i<lines.length;i++){line=mmTrim(lines[i]);if(!line||line.substr(0,2)==='%%')continue;if(i===0){if(!/^stateDiagram(?:-v2)?\\b/i.test(line))return null;continue;}"
    "if((m=/^direction\\s+(TD|TB|BT|LR|RL)$/i.exec(line))){ctx.dir=m[1].toUpperCase();continue;}\n"
    "if((m=/^state\\s+\"([^\"]+)\"\\s+as\\s+([A-Za-z0-9_:-]+)$/i.exec(line))){mmStateNode(ctx,m[2],m[1]);continue;}"
    "if((m=/^(\\[\\*\\]|[A-Za-z0-9_:-]+)\\s*--?>\\s*(\\[\\*\\]|[A-Za-z0-9_:-]+)(?:\\s*:\\s*(.+))?$/i.exec(line))){a=mmStateNode(ctx,m[1]);b=mmStateNode(ctx,m[2]);ctx.edges.push({from:a.id,to:b.id,label:mmTrim(m[3]||'')});continue;}"
    "if((m=/^([A-Za-z0-9_:-]+)$/.exec(line))){mmStateNode(ctx,m[1]);continue;}return null;}"
    "if(ctx.states.length===0||ctx.states.length>24||ctx.edges.length>64)return null;return ctx}"
    "function mmUnquote(text){\n"
    "    return text.charAt(0)==='\"'&&text.slice(-1)==='\"'?text.slice(1,-1):text;\n"
    "}\n"
    "var mmGraphName='(?:\"[^\"]+\"|[^\\\\s\"{}\\\\[\\\\]<>:|;]+)';\n"
    "function mmParseER(src){\n"
    "    var ctx={kind:'er',nodes:[],edges:[],map:Object.create(null)}, entity=null;\n"
    "    var words='(?:(?:one|zero)\\\\s+or\\\\s+(?:one|zero|more|many)|many\\\\([01]\\\\)|[01]\\\\+|only\\\\s+one|1)';\n"
    "    // Entity identifiers and aliases each have their own capture.\n"
    "    var name='('+mmGraphName+')(?:\\\\s*\\\\[\\\\s*('+mmGraphName+')\\\\s*\\\\])?';\n"
    "    var rel=new RegExp('^'+name+'\\\\s*([|}][o|]|'+words+')\\\\s*(--|\\\\.\\\\.|(?:optionally\\\\s+)?to)\\\\s*([o|][|{]|'+words+')\\\\s*'+name+'\\\\s*:\\\\s*(\"[^\"]+\"|[^\"]+)$','i');\n"
    "    var declaration=new RegExp('^'+name+'\\\\s*(?:\\\\{\\\\s*(.*))?$');\n"
    "    var lines=src.split(/\\r?\\n/), i, line, m, closed, field, a, b, left, right;\n"
    "    function node(id,alias){\n"
    "        id=mmUnquote(id);\n"
    "        var n=ctx.map[id];\n"
    "        if(!n){n={id:id,title:id,members:[]};ctx.map[id]=n;ctx.nodes.push(n);}\n"
    "        if(alias)n.title=mmUnquote(alias);\n"
    "        return n;\n"
    "    }\n"
    "    function cardinality(value,start){\n"
    "        value=value.toLowerCase().replace(/\\s+/g,' ');\n"
    "        if(start&&/^[|}][o|]$/.test(value))return (value.charAt(1)==='o'?'0':'1')+(value.charAt(0)==='}'?'n':'1');\n"
    "        if(!start&&/^[o|][|{]$/.test(value))return (value.charAt(0)==='o'?'0':'1')+(value.charAt(1)==='{'?'n':'1');\n"
    "        if(/^(one or zero|zero or one)$/.test(value))return '01';\n"
    "        if(/^(one or (more|many)|many\\(1\\)|1\\+)$/.test(value))return '1n';\n"
    "        if(/^(zero or (more|many)|many\\(0\\)|0\\+)$/.test(value))return '0n';\n"
    "        if(/^(only one|1)$/.test(value))return '11';\n"
    "        return null;\n"
    "    }\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        if(!entity){\n"
    "            if((m=/^direction\\s+(TB|BT|LR|RL)$/i.exec(line))){ctx.dir=m[1].toUpperCase();continue;}\n"
    "            if((m=rel.exec(line))){\n"
    "                left=cardinality(m[3],true);right=cardinality(m[5],false);\n"
    "                if(!left||!right)return null;\n"
    "                a=node(m[1],m[2]);b=node(m[6],m[7]);\n"
    "                ctx.edges.push({from:a.id,to:b.id,kind:/^\\.\\.|^optionally/i.test(m[4])?'..':'--',\n"
    "                    label:mmUnquote(mmTrim(m[8])),cardStart:left,cardEnd:right});\n"
    "                continue;\n"
    "            }\n"
    "            m=declaration.exec(line);\n"
    "            if(!m)return null;\n"
    "            a=node(m[1],m[2]);\n"
    "            if(typeof m[3]==='undefined')continue;\n"
    "            entity=a;line=m[3];\n"
    "        }\n"
    "        closed=/\\}\\s*$/.test(line);\n"
    "        if(closed)line=mmTrim(line.replace(/\\}\\s*$/,''));\n"
    "        if(line){\n"
    "            m=/^([A-Za-z_\\u0080-\\uffff][\\w\\u0080-\\uffff()[\\]-]*\\??)\\s+(\\*?[\\w\\u0080-\\uffff()[\\]-]+)(?:\\s+((?:PK|FK|UK)(?:\\s*,\\s*(?:PK|FK|UK))*))?(?:\\s+\"([^\"]*)\")?$/.exec(line);\n"
    "            if(!m)return null;\n"
    "            field=m[1]+' '+m[2];\n"
    "            if(m[3])field+='  ['+m[3].replace(/\\s*,\\s*/g,', ')+']';\n"
    "            if(m[4])field+='  '+m[4];\n"
    "            entity.members.push(field);\n"
    "        }\n"
    "        if(closed)entity=null;\n"
    "    }\n"
    "    return !entity&&ctx.nodes.length&&ctx.nodes.length<=64&&ctx.edges.length<=128?ctx:null;\n"
    "}\n"
    "function mmParseRequirement(src){\n"
    "    var ctx={kind:'requirement',nodes:[],edges:[],map:Object.create(null)}, node=null;\n"
    "    var types={requirement:'Requirement',functionalrequirement:'Functional Requirement',\n"
    "        interfacerequirement:'Interface Requirement',performancerequirement:'Performance Requirement',\n"
    "        physicalrequirement:'Physical Requirement',designconstraint:'Design Constraint',element:'Element'};\n"
    "    var fields={id:'ID',text:'Text',risk:'Risk',verifymethod:'Verification',type:'Type',docref:'Document reference'};\n"
    "    var names='('+mmGraphName+')', relationships='(contains|copies|derives|satisfies|verifies|refines|traces)';\n"
    "    var declaration=new RegExp('^(\\\\w+)\\\\s+'+names+'\\\\s*\\\\{\\\\s*(.*)$');\n"
    "    var forward=new RegExp('^'+names+'\\\\s*-\\\\s*'+relationships+'\\\\s*->\\\\s*'+names+'$','i');\n"
    "    var backward=new RegExp('^'+names+'\\\\s*<-\\\\s*'+relationships+'\\\\s*-\\\\s*'+names+'$','i');\n"
    "    var lines=src.split(/\\r?\\n/), i, line, m, kind, id, key, value, closed, reversed;\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        if(!node){\n"
    "            if((m=/^direction\\s+(TB|BT|LR|RL)$/i.exec(line))){ctx.dir=m[1].toUpperCase();continue;}\n"
    "            m=forward.exec(line);reversed=!m;\n"
    "            if(m||(m=backward.exec(line))){\n"
    "                kind=m[2].toLowerCase();\n"
    "                ctx.edges.push({from:mmUnquote(m[reversed?3:1]),to:mmUnquote(m[reversed?1:3]),\n"
    "                    label:'<<'+kind+'>>',kind:kind==='contains'?'contains':'-.->'});\n"
    "                continue;\n"
    "            }\n"
    "            m=declaration.exec(line);\n"
    "            if(!m)return null;\n"
    "            kind=m[1].toLowerCase();id=mmUnquote(m[2]);\n"
    "            if(!Object.prototype.hasOwnProperty.call(types,kind)||ctx.map[id])return null;\n"
    "            node={id:id,title:id,members:['<<'+types[kind]+'>>'],element:kind==='element'};\n"
    "            ctx.map[id]=node;ctx.nodes.push(node);line=m[3];\n"
    "        }\n"
    "        closed=/\\}\\s*$/.test(line);\n"
    "        if(closed)line=mmTrim(line.replace(/\\}\\s*$/,''));\n"
    "        if(line){\n"
    "            m=/^(\\w+)\\s*:\\s*(\"[^\"]*\"|[^\"{}]+)$/i.exec(line);\n"
    "            if(!m)return null;\n"
    "            key=m[1].toLowerCase();value=mmUnquote(mmTrim(m[2]));\n"
    "            if(!(node.element?/^(type|docref)$/:/^(id|text|risk|verifymethod)$/).test(key))return null;\n"
    "            if(key==='risk'&&!/^(low|medium|high)$/i.test(value))return null;\n"
    "            if(key==='verifymethod'&&!/^(analysis|inspection|test|demonstration)$/i.test(value))return null;\n"
    "            node.members=node.members.concat(mmWrapWords(fields[key]+': '+value,48));\n"
    "        }\n"
    "        if(closed)node=null;\n"
    "    }\n"
    "    if(node||!ctx.nodes.length||ctx.nodes.length>64||ctx.edges.length>128)return null;\n"
    "    for(i=0;i<ctx.edges.length;i++){\n"
    "        if(!ctx.map[ctx.edges[i].from]||!ctx.map[ctx.edges[i].to])return null;\n"
    "    }\n"
    "    return ctx;\n"
    "}\n"
    );
    sb_append(sb,
    "var mmDay=86400000;\n"
    "var mmDurationUnits={ms:1,s:1000,m:60000,h:3600000,d:mmDay,w:7*mmDay};\n"
    "var mmDateFormats={\n"
    "    'YYYY-MM-DD':/^(\\d{4})-(\\d{2})-(\\d{2})$/,\n"
    "    'YYYY-MM-DD HH:mm':/^(\\d{4})-(\\d{2})-(\\d{2}) (\\d{2}):(\\d{2})$/,\n"
    "    'YYYY-MM-DD HH:mm:ss':/^(\\d{4})-(\\d{2})-(\\d{2}) (\\d{2}):(\\d{2}):(\\d{2})$/,\n"
    "    'HH:mm':/^()()()(\\d{2}):(\\d{2})$/,\n"
    "    'HH:mm:ss':/^()()()(\\d{2}):(\\d{2}):(\\d{2})$/\n"
    "};\n"
    "var mmMonths='January February March April May June July August September October November December'.split(' ');\n"
    "var mmWeekdays='Sunday Monday Tuesday Wednesday Thursday Friday Saturday'.split(' ');\n"
    "function mmDate(text,format){\n"
    "    if(format==='X'||format==='x'){\n"
    "        return /^-?\\d+(?:\\.\\d+)?$/.test(text)?+new Date(+text*(format==='X'?1000:1)):NaN;\n"
    "    }\n"
    "    var m=mmDateFormats[format].exec(text);\n"
    "    if(!m)return NaN;\n"
    "    var date=new Date(0), year=m[1]?+m[1]:1970, month=m[2]?m[2]-1:0, day=m[3]?+m[3]:1;\n"
    "    var hour=+(m[4]||0), minute=+(m[5]||0), second=+(m[6]||0);\n"
    "    if(hour>23||minute>59||second>59)return NaN;\n"
    "    date.setUTCFullYear(year,month,day);\n"
    "    date.setUTCHours(hour,minute,second,0);\n"
    "    return date.getUTCFullYear()===year&&date.getUTCMonth()===month&&date.getUTCDate()===day?+date:NaN;\n"
    "}\n"
    "function mmDateLabel(time,format){\n"
    "    var date=new Date(time), month=mmMonths[date.getUTCMonth()], weekday=mmWeekdays[date.getUTCDay()];\n"
    "    function pad(n,size){return ('000'+n).slice(-size);}\n"
    "    var codes={Y:date.getUTCFullYear(),y:pad(date.getUTCFullYear()%100,2),\n"
    "        m:pad(date.getUTCMonth()+1,2),d:pad(date.getUTCDate(),2),e:date.getUTCDate(),\n"
    "        H:pad(date.getUTCHours(),2),I:pad(date.getUTCHours()%12||12,2),M:pad(date.getUTCMinutes(),2),\n"
    "        S:pad(date.getUTCSeconds(),2),L:pad(date.getUTCMilliseconds(),3),p:date.getUTCHours()<12?'AM':'PM',\n"
    "        b:month.slice(0,3),B:month,a:weekday.slice(0,3),A:weekday,s:Math.floor(time/1000),'%':'%'};\n"
    "    return format.replace(/%./g,function(token){return codes[token.charAt(1)];});\n"
    "}\n"
    "function mmGanttDuration(start,m){\n"
    "    var amount=+m[1], unit=m[2];\n"
    "    if(unit!=='M'&&unit!=='y')return +new Date(start+amount*mmDurationUnits[unit]);\n"
    "    var date=new Date(start), day=date.getUTCDate(), months=amount*(unit==='y'?12:1);\n"
    "    if(months!==Math.floor(months))return NaN;\n"
    "    date.setUTCMonth(date.getUTCMonth()+months+1,0);\n"
    "    date.setUTCDate(Math.min(day,date.getUTCDate()));\n"
    "    return +date;\n"
    "}\n"
    "function mmGanttExcluded(ctx,day){\n"
    "    if(ctx.includes[day])return false;\n"
    "    var weekday=((day+4)%7+7)%7;\n"
    "    return ctx.excludedDates[day]||ctx.excludedDays[weekday]||\n"
    "        (ctx.weekends&&(weekday===ctx.weekend||weekday===(ctx.weekend+1)%7));\n"
    "}\n"
    "function mmResolveGantt(ctx,ids){\n"
    "    function reference(text,until){\n"
    "        var list=text.split(/\\s+/), time=until?Infinity:-Infinity;\n"
    "        for(var i=0;i<list.length;i++){\n"
    "            var task=ids[list[i]], value=resolve(task,!until);\n"
    "            time=until?Math.min(value,time):Math.max(value,time);\n"
    "        }\n"
    "        return time;\n"
    "    }\n"
    "    function resolve(task,end){\n"
    "        if(!task)return NaN;\n"
    "        var field=end?'end':'start', state=field+'State', value, m;\n"
    "        if(task[state]===1)return NaN;\n"
    "        if(task[state]===2)return task[field];\n"
    "        task[state]=1;\n"
    "        if(!end){\n"
    "            if(!task.startSpec)value=resolve(task.previous,true);\n"
    "            else if((m=/^after\\s+(.+)$/.exec(task.startSpec)))value=reference(m[1],false);\n"
    "            else value=mmDate(task.startSpec,ctx.format);\n"
    "        }else{\n"
    "            var start=resolve(task,false);\n"
    "            if(!isFinite(start))return NaN;\n"
    "            if((m=/^until\\s+(.+)$/.exec(task.endSpec)))value=reference(m[1],true);\n"
    "            else if((m=/^(\\d+(?:\\.\\d+)?)(ms|[smhdwMy])$/.exec(task.endSpec))){\n"
    "                value=mmGanttDuration(start,m);\n"
    "                // A duration extends by one day for each excluded date crossed.\n"
    "                if(ctx.hasExclusions){\n"
    "                    var count=0;\n"
    "                    for(var day=Math.floor(start/mmDay)+1;day*mmDay<=value;day++){\n"
    "                        if(++count>36600)return NaN;\n"
    "                        if(mmGanttExcluded(ctx,day))value+=mmDay;\n"
    "                    }\n"
    "                }\n"
    "            }else{\n"
    "                value=mmDate(task.endSpec,ctx.format);\n"
    "                if(ctx.inclusive)value+=mmDay;\n"
    "            }\n"
    "            if(value<start||!isFinite(+new Date(value)))return NaN;\n"
    "        }\n"
    "        task[field]=value;task[state]=2;\n"
    "        return value;\n"
    "    }\n"
    "    for(var i=0;i<ctx.tasks.length;i++){\n"
    "        if(!isFinite(resolve(ctx.tasks[i],true)))return false;\n"
    "    }\n"
    "    return true;\n"
    "}\n"
    "function mmParseGantt(src){\n"
    "    var ctx={kind:'gantt',title:'',format:'YYYY-MM-DD',axis:'',tasks:[],weekend:6,\n"
    "        excludedDays:[],excludedDates:Object.create(null),includes:Object.create(null)};\n"
    "    var ids=Object.create(null), previous=null, section='', lines=src.split(/\\r?\\n/);\n"
    "    var i,j,line,m,task,parts,key,value,day;\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        if(line==='topAxis'){ctx.topAxis=true;continue;}\n"
    "        if(line==='inclusiveEndDates'){ctx.inclusive=true;continue;}\n"
    "        if((m=/^(title|section|dateFormat|axisFormat|tickInterval|excludes|includes|weekend|todayMarker)\\s+(.+)$/.exec(line))){\n"
    "            key=m[1];value=m[2];\n"
    "            if(key==='title')ctx.title=value;\n"
    "            else if(key==='section')section=value;\n"
    "            else if(key==='dateFormat'){\n"
    "                if(!Object.prototype.hasOwnProperty.call(mmDateFormats,value)&&value!=='X'&&value!=='x')return null;\n"
    "                ctx.format=value;\n"
    "            }else if(key==='axisFormat'){\n"
    "                if(/%(?![YymdeHIMSLpbBaAs%])/.test(value))return null;\n"
    "                ctx.axis=value;\n"
    "            }else if(key==='tickInterval'){\n"
    "                m=/^([1-9]\\d*)(millisecond|second|minute|hour|day|week)s?$/.exec(value);\n"
    "                if(!m)return null;\n"
    "                ctx.tick=+m[1]*mmDurationUnits[m[2]==='millisecond'?'ms':m[2].charAt(0)];\n"
    "                if(!isFinite(ctx.tick))return null;\n"
    "            }else if(key==='todayMarker'){\n"
    "                if(value!=='off')return null;\n"
    "                ctx.noToday=true;\n"
    "            }else if(key==='weekend'){\n"
    "                if(value!=='friday'&&value!=='saturday')return null;\n"
    "                ctx.weekend=value==='friday'?5:6;\n"
    "            }else{\n"
    "                parts=value.toLowerCase().split(/[\\s,]+/);\n"
    "                for(j=0;j<parts.length;j++){\n"
    "                    value=parts[j];day=mmDate(value,'YYYY-MM-DD')/mmDay;\n"
    "                    if(key==='includes'){if(!isFinite(day))return null;ctx.includes[day]=true;}\n"
    "                    else if(value==='weekends')ctx.weekends=true;\n"
    "                    else if(isFinite(day))ctx.excludedDates[day]=true;\n"
    "                    else{\n"
    "                        day=mmWeekdays.indexOf(value.charAt(0).toUpperCase()+value.slice(1));\n"
    "                        if(day<0)return null;\n"
    "                        ctx.excludedDays[day]=true;\n"
    "                    }\n"
    "                }\n"
    "                if(key==='excludes')ctx.hasExclusions=true;\n"
    "            }\n"
    "            continue;\n"
    "        }\n"
    "        m=/^([^:]+):(.+)$/.exec(line);\n"
    "        if(!m||/^(?:click\\s|%%\\{)/.test(line)||ctx.tasks.length>=128)return null;\n"
    "        parts=m[2].split(',').map(mmTrim);\n"
    "        task={label:mmTrim(m[1]),section:section,previous:previous,tags:Object.create(null)};\n"
    "        while(parts.length&&/^(active|done|crit|milestone|vert)$/.test(parts[0]))task.tags[parts.shift()]=true;\n"
    "        if(!task.label||parts.length<1||parts.length>3||parts.indexOf('')>=0)return null;\n"
    "        if(parts.length===3){\n"
    "            key=parts.shift();\n"
    "            if(!/^[\\w-]+$/.test(key)||ids[key])return null;\n"
    "            ids[key]=task;\n"
    "        }\n"
    "        task.startSpec=parts.length===2?parts[0]:'';\n"
    "        task.endSpec=parts[parts.length-1];\n"
    "        ctx.tasks.push(task);previous=task;\n"
    "    }\n"
    "    return ctx.tasks.length&&mmResolveGantt(ctx,ids)?ctx:null;\n"
    "}\n"
    "function mmRenderGantt(view,ctx){\n"
    "    var svg=mmSvgEl('svg'), f=mmBodyFontPx(), tasks=ctx.tasks, rows=[], ticks=[], tickWidth=0;\n"
    "    var low=Infinity, high=-Infinity, labelWidth=f*5, y=ctx.title?f*3:0, section='';\n"
    "    var i,j,task,lines,label,range,interval,first,last,plotWidth,t,bottom,top;\n"
    "    function line(x1,y1,x2,y2,cls){\n"
    "        mmShape(svg,'line',{x1:x1,y1:y1,x2:x2,y2:y2},'mdv-mm-edge'+(cls?' '+cls:''));\n"
    "    }\n"
    "    for(i=0;i<tasks.length;i++){\n"
    "        task=tasks[i];\n"
    "        low=Math.min(low,task.start);high=Math.max(high,task.end);\n"
    "        if(task.section!==section){\n"
    "            section=task.section;\n"
    "            if(section){\n"
    "                lines=mmWrapWords(section,30);\n"
    "                rows.push({lines:lines,heading:true});\n"
    "            }\n"
    "        }\n"
    "        label=[task.label];\n"
    "        if(task.tags.crit)label.push('critical');\n"
    "        if(task.tags.done)label.push('done');\n"
    "        if(task.tags.active)label.push('active');\n"
    "        lines=mmWrapWords(label[0]+(label.length>1?' ('+label.slice(1).join(', ')+')':''),30);\n"
    "        if(!task.tags.vert)rows.push({lines:lines,task:task});\n"
    "    }\n"
    "    for(i=0;i<rows.length;i++)for(j=0;j<rows[i].lines.length;j++)labelWidth=Math.max(labelWidth,mmTextWidth(rows[i].lines[j],f));\n"
    "    for(i=0;i<tasks.length;i++)if(tasks[i].tags.vert)labelWidth=Math.max(labelWidth,mmTextWidth(tasks[i].label,f));\n"
    "    labelWidth+=f*1.5;\n"
    "    range=high-low;\n"
    "    if(ctx.tick)interval=ctx.tick*Math.max(1,Math.ceil(range/ctx.tick/19));\n"
    "    else{\n"
    "        var steps=[1,10,100,1000,5000,15000,60000,300000,900000,3600000,10800000,21600000,\n"
    "            mmDay,2*mmDay,7*mmDay,14*mmDay,30*mmDay,90*mmDay,365*mmDay];\n"
    "        for(i=0;i+1<steps.length&&steps[i]<range/6;i++){}\n"
    "        interval=steps[i]*Math.max(1,Math.ceil(range/steps[i]/6));\n"
    "    }\n"
    "    first=Math.floor(low/interval)*interval;last=Math.max(first+interval,Math.ceil(high/interval)*interval);\n"
    "    var format=ctx.axis;\n"
    "    if(!format){\n"
    "        if(/^[Xx]$/.test(ctx.format))format=interval<1000?'%s.%L':'%s';\n"
    "        else if(interval>=mmDay)format='%Y-%m-%d';\n"
    "        else{\n"
    "            format=interval<1000?'%H:%M:%S.%L':interval<60000?'%H:%M:%S':'%H:%M';\n"
    "            if(ctx.format.indexOf('YYYY')===0&&Math.floor(first/mmDay)!==Math.floor(last/mmDay))format='%m-%d '+format;\n"
    "        }\n"
    "    }\n"
    "    for(t=first;t<=last;t+=interval){\n"
    "        if(!isFinite(+new Date(t)))return 0;\n"
    "        label=mmDateLabel(t,format);\n"
    "        tickWidth=Math.max(tickWidth,mmTextWidth(label,f));\n"
    "        ticks.push({time:t,label:label});\n"
    "    }\n"
    "    plotWidth=Math.max(f*24,(ticks.length-1)*(tickWidth+f*1.1));\n"
    "    function x(time){return labelWidth+(time-first)*plotWidth/(last-first);}\n"
    "    y+=ctx.topAxis?f*2:0;\n"
    "    top=y;\n"
    "    for(i=0;i<rows.length;i++){\n"
    "        rows[i].y=y;rows[i].h=f*(rows[i].lines.length*1.25+.8);y+=rows[i].h;\n"
    "    }\n"
    "    bottom=Math.max(y,top+f*2);\n"
    "    if(ctx.hasExclusions&&last-first<=366*mmDay){\n"
    "        for(t=Math.floor(first/mmDay);t*mmDay<last;t++){\n"
    "            if(mmGanttExcluded(ctx,t))mmShape(svg,'rect',{x:x(Math.max(first,t*mmDay)),y:top,\n"
    "                width:x(Math.min(last,(t+1)*mmDay))-x(Math.max(first,t*mmDay)),height:bottom-top},\n"
    "                'mdv-mm-node mdv-mm-excluded');\n"
    "        }\n"
    "    }\n"
    "    for(i=0;i<ticks.length;i++){\n"
    "        t=x(ticks[i].time);\n"
    "        line(t,top,t,bottom,'mdv-mm-grid');\n"
    "        mmPushText(svg,t,bottom+f*1.35,[ticks[i].label],'mdv-mm-text');\n"
    "        if(ctx.topAxis)mmPushText(svg,t,top-f*.55,[ticks[i].label],'mdv-mm-text');\n"
    "    }\n"
    "    line(labelWidth,top,labelWidth+plotWidth,top);\n"
    "    line(labelWidth,bottom,labelWidth+plotWidth,bottom);\n"
    "    for(i=0;i<rows.length;i++){\n"
    "        var row=rows[i], center=row.y+row.h/2;\n"
    "        mmPushText(svg,row.heading?0:f*.55,center+f*(.34-(row.lines.length-1)*.625),row.lines,\n"
    "            'mdv-mm-text'+(row.heading?' mdv-mm-chart-heading':''),'start');\n"
    "        if(row.heading){line(0,row.y+row.h,labelWidth+plotWidth,row.y+row.h,'mdv-mm-grid');continue;}\n"
    "        task=row.task;\n"
    "        var cls='mdv-mm-node'+(task.tags.done?' mdv-mm-task-done':'')+(task.tags.active?' mdv-mm-task-active':'');\n"
    "        if(task.tags.milestone){\n"
    "            t=x((task.start+task.end)/2);\n"
    "            var r=f*.5;\n"
    "            mmShape(svg,'polygon',{points:[t+','+(center-r),(t+r)+','+center,t+','+(center+r),(t-r)+','+center].join(' ')},cls);\n"
    "        }else{\n"
    "            mmShape(svg,'rect',{x:x(task.start),y:center-f*.5,width:Math.max(1,x(task.end)-x(task.start)),height:f,rx:3},cls);\n"
    "        }\n"
    "    }\n"
    "    var now=new Date(), today=Math.floor(+now/mmDay)*mmDay;\n"
    "    if(!ctx.noToday&&ctx.format.indexOf('YYYY')===0&&today>=low&&today<=high)line(x(today),top,x(today),bottom,'dash');\n"
    "    var markerY=bottom+f*2.75;\n"
    "    for(i=0;i<tasks.length;i++){\n"
    "        task=tasks[i];if(!task.tags.vert)continue;\n"
    "        t=x((task.start+task.end)/2);\n"
    "        line(t,top,t,markerY-f*1.1,'dash');\n"
    "        mmPushText(svg,t,markerY,[task.label],'mdv-mm-text');\n"
    "        markerY+=f*1.35;\n"
    "    }\n"
    "    if(ctx.title)mmPushText(svg,(labelWidth+plotWidth)/2,f*1.1,[ctx.title],'mdv-mm-text mdv-mm-chart-heading');\n"
    "    view.appendChild(svg);return 1;\n"
    "}\n"
    "function mmParseTimeline(src){\n"
    "    var lines=src.split(/\\r?\\n/), header=/^timeline(?:\\s+(LR|TD))?$/i.exec(mmTrim(lines[0]));\n"
    "    if(!header)return null;\n"
    "    var ctx={kind:'timeline',dir:(header[1]||'LR').toUpperCase(),title:'',groups:[]};\n"
    "    var group=null, period=null, count=0, i, j, line, m, parts, label;\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        if(/^%%\\{/.test(line))return null;\n"
    "        if((m=/^title\\s+(.+)$/.exec(line))){ctx.title=m[1];continue;}\n"
    "        if((m=/^section\\s+(.+)$/.exec(line))){\n"
    "            group={title:m[1],periods:[]};ctx.groups.push(group);period=null;continue;\n"
    "        }\n"
    "        parts=line.split(':');label=mmTrim(parts.shift());\n"
    "        if(label){\n"
    "            if(!group){group={title:'',periods:[]};ctx.groups.push(group);}\n"
    "            period={label:label,events:[]};group.periods.push(period);count++;\n"
    "        }else if(!period||!parts.length)return null;\n"
    "        for(j=0;j<parts.length;j++){\n"
    "            label=mmTrim(parts[j]);if(!label)return null;\n"
    "            period.events.push(label);count++;\n"
    "        }\n"
    "        if(count>128)return null;\n"
    "    }\n"
    "    ctx.groups=ctx.groups.filter(function(group){return group.periods.length>0;});\n"
    "    return ctx.groups.length?ctx:null;\n"
    "}\n"
    "function mmRenderTimeline(view,ctx){\n"
    "    var svg=mmSvgEl('svg'), f=mmBodyFontPx(), horizontal=ctx.dir==='LR', groups=ctx.groups;\n"
    "    var i,j,k,g,p,box,sectionH=0,periodH=0,periodW=0,eventW=0;\n"
    "    function measure(text,limit){\n"
    "        var chunks=text.split(/<br\\s*\\/?>/i), lines=[], width=0, n;\n"
    "        for(n=0;n<chunks.length;n++)lines=lines.concat(mmWrapWords(chunks[n],limit));\n"
    "        for(n=0;n<lines.length;n++)width=Math.max(width,mmTextWidth(lines[n],f));\n"
    "        return {lines:lines,w:width+f*1.6,h:f*(lines.length*1.25+.75)};\n"
    "    }\n"
    "    function card(box,x,y,w,h,heading){\n"
    "        mmShape(svg,'rect',{x:x,y:y,width:w,height:h,rx:4},'mdv-mm-node');\n"
    "        mmPushText(svg,x+w/2,y+h/2+f*(.34-(box.lines.length-1)*.625),box.lines,\n"
    "            'mdv-mm-text'+(heading?' mdv-mm-chart-heading':''));\n"
    "    }\n"
    "    function line(x1,y1,x2,y2){\n"
    "        mmShape(svg,'line',{x1:x1,y1:y1,x2:x2,y2:y2},'mdv-mm-edge');\n"
    "    }\n"
    "    function dot(x,y){\n"
    "        mmShape(svg,'circle',{cx:x,cy:y,r:f*.23},'mdv-mm-marker');\n"
    "    }\n"
    "    for(i=0;i<groups.length;i++){\n"
    "        g=groups[i];g.heading=g.title?measure(g.title,44):null;\n"
    "        if(g.heading)sectionH=Math.max(sectionH,g.heading.h);\n"
    "        g.width=0;\n"
    "        for(j=0;j<g.periods.length;j++){\n"
    "            p=g.periods[j];p.box=measure(p.label,24);p.width=Math.max(f*7,p.box.w);\n"
    "            periodH=Math.max(periodH,p.box.h);periodW=Math.max(periodW,p.width);\n"
    "            p.cards=[];p.eventHeight=0;\n"
    "            for(k=0;k<p.events.length;k++){\n"
    "                box=measure(p.events[k],28);p.cards.push(box);\n"
    "                p.width=Math.max(p.width,box.w);eventW=Math.max(eventW,box.w);\n"
    "                p.eventHeight+=box.h+(k?f*.45:0);\n"
    "            }\n"
    "            g.width+=p.width+(j?f*.65:0);\n"
    "        }\n"
    "        g.width=Math.max(g.width,g.heading?g.heading.w:0);\n"
    "    }\n"
    "    var x=0,y=0,total=0,title=ctx.title?measure(ctx.title,60):null;\n"
    "    if(title)y=title.h+f*.45;\n"
    "    if(horizontal){\n"
    "        for(i=0;i<groups.length;i++)total+=groups[i].width+(i?f*1.25:0);\n"
    "        if(title)mmPushText(svg,total/2,f*1.2,title.lines,'mdv-mm-text mdv-mm-chart-heading');\n"
    "        var periodY=y+(sectionH?sectionH+f*.65:0), axisY=periodY+periodH+f*.65;\n"
    "        line(0,axisY,total+f*.5,axisY);\n"
    "        mmShape(svg,'polygon',{points:(total+f*.5)+','+(axisY-f*.27)+' '+(total+f)+','+axisY+\n"
    "            ' '+(total+f*.5)+','+(axisY+f*.27)},'mdv-mm-marker');\n"
    "        for(i=0;i<groups.length;i++){\n"
    "            g=groups[i];\n"
    "            if(g.heading)card(g.heading,x,y,g.width,sectionH,true);\n"
    "            var contentWidth=0;\n"
    "            for(j=0;j<g.periods.length;j++)contentWidth+=g.periods[j].width+(j?f*.65:0);\n"
    "            var px=x+(g.width-contentWidth)/2;\n"
    "            for(j=0;j<g.periods.length;j++){\n"
    "                p=g.periods[j];\n"
    "                var middle=px+p.width/2, eventY=axisY+f*.7;\n"
    "                line(middle,periodY+periodH,middle,p.cards.length?eventY+p.eventHeight:axisY);\n"
    "                dot(middle,axisY);card(p.box,px,periodY,p.width,periodH,true);\n"
    "                for(k=0;k<p.cards.length;k++){\n"
    "                    box=p.cards[k];card(box,px,eventY,p.width,box.h,false);eventY+=box.h+f*.45;\n"
    "                }\n"
    "                px+=p.width+f*.65;\n"
    "            }\n"
    "            x+=g.width+f*1.25;\n"
    "        }\n"
    "    }else{\n"
    "        var axisX=periodW+f, eventX=axisX+f;\n"
    "        total=eventW?eventX+eventW:axisX+f;\n"
    "        for(i=0;i<groups.length;i++)if(groups[i].heading)total=Math.max(total,groups[i].heading.w);\n"
    "        if(title)mmPushText(svg,total/2,f*1.2,title.lines,'mdv-mm-text mdv-mm-chart-heading');\n"
    "        for(i=0;i<groups.length;i++){\n"
    "            g=groups[i];\n"
    "            if(g.heading){card(g.heading,0,y,total,g.heading.h,true);y+=g.heading.h+f*.65;}\n"
    "            for(j=0;j<g.periods.length;j++){\n"
    "                p=g.periods[j];\n"
    "                var rowHeight=Math.max(p.box.h,p.eventHeight), ym=y+rowHeight/2;\n"
    "                line(axisX,y,axisX,y+rowHeight+f*.65);\n"
    "                line(periodW,ym,p.cards.length?eventX:axisX,ym);dot(axisX,ym);\n"
    "                card(p.box,0,ym-p.box.h/2,periodW,p.box.h,true);\n"
    "                var ey=ym-p.eventHeight/2;\n"
    "                if(p.cards.length>1)line(eventX,ey+p.cards[0].h/2,eventX,ey+p.eventHeight-p.cards[p.cards.length-1].h/2);\n"
    "                for(k=0;k<p.cards.length;k++){\n"
    "                    box=p.cards[k];card(box,eventX,ey,eventW,box.h,false);ey+=box.h+f*.45;\n"
    "                }\n"
    "                y+=rowHeight+f*.65;\n"
    "            }\n"
    "        }\n"
    "        mmShape(svg,'polygon',{points:(axisX-f*.27)+','+y+' '+(axisX+f*.27)+','+y+' '+axisX+','+(y+f*.5)},'mdv-mm-marker');\n"
    "    }\n"
    "    view.appendChild(svg);return 1;\n"
    "}\n"
    );
    sb_append(sb,
    "function mmAccent(index){\n"
    "    return ' mdv-mm-accent mdv-mm-accent-'+index%4;\n"
    "}\n"
    "function mmGitWord(word){\n"
    "    return mmUnquote(word).replace(/\\\\([\"\\\\])/g,'$1');\n"
    "}\n"
    "function mmParseGitGraph(src){\n"
    "    var lines=src.split(/\\r?\\n/), header=/^gitGraph(?:\\s+(LR|TB|BT))?\\s*:?$/i.exec(mmTrim(lines[0]));\n"
    "    if(!header)return null;\n"
    "    var main={name:'main',head:null,index:0,order:0};\n"
    "    var ctx={kind:'gitgraph',direction:(header[1]||'LR').toUpperCase(),branches:[main],commits:[]};\n"
    "    var branches=Object.create(null), ids=Object.create(null), current=main;\n"
    "    branches.main=main;\n"
    "    var word='(\"(?:\\\\\\\\.|[^\"\\\\\\\\])*\"|[^\\\\s\"]+)';\n"
    "    var branchName=new RegExp('^'+word+'(?:\\\\s+(.*))?$');\n"
    "    var attribute=new RegExp('^(id|tag|type|parent)\\\\s*:\\\\s*'+word+'(?:\\\\s+(.*))?$','i');\n"
    "    var i,line,m,command,args,name,branch,other,attrs,key,value,picked,parents,commit;\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        m=/^(commit|branch|checkout|switch|merge|cherry-pick)(?:\\s+(.*))?$/i.exec(line);\n"
    "        if(!m)return null;\n"
    "        command=m[1].toLowerCase();args=m[2]||'';other=null;\n"
    "        if(/^(branch|checkout|switch|merge)$/.test(command)){\n"
    "            m=branchName.exec(args);if(!m)return null;\n"
    "            name=mmGitWord(m[1]);args=m[2]||'';branch=branches[name];\n"
    "            if(!name)return null;\n"
    "            if(command==='branch'){\n"
    "                if(branch||ctx.branches.length>=24)return null;\n"
    "                m=/^order\\s*:\\s*(\\d+)$/.exec(args);\n"
    "                if(args&&!m)return null;\n"
    "                value=m?+m[1]:0;if(!isFinite(value))return null;\n"
    "                branch={name:name,head:current.head,index:ctx.branches.length,order:value};\n"
    "                branches[name]=branch;ctx.branches.push(branch);current=branch;\n"
    "                continue;\n"
    "            }\n"
    "            if(!branch)return null;\n"
    "            if(command==='checkout'||command==='switch'){\n"
    "                if(args)return null;\n"
    "                current=branch;continue;\n"
    "            }\n"
    "            if(current===branch||!current.head||!branch.head||current.head===branch.head)return null;\n"
    "            other=branch.head;\n"
    "        }\n"
    "        attrs=Object.create(null);attrs.tags=[];\n"
    "        while(args){\n"
    "            m=attribute.exec(args);if(!m)return null;\n"
    "            key=m[1].toLowerCase();value=mmGitWord(m[2]);\n"
    "            if(key==='tag')attrs.tags.push(value);\n"
    "            else{if(!value||attrs[key])return null;attrs[key]=value;}\n"
    "            args=m[3]||'';\n"
    "        }\n"
    "        if(attrs.type&&!/^(NORMAL|REVERSE|HIGHLIGHT)$/.test(attrs.type))return null;\n"
    "        if(ctx.commits.length>=128)return null;\n"
    "        picked=null;parents=current.head?[current.head]:[];\n"
    "        if(command==='cherry-pick'){\n"
    "            picked=ids[attrs.id];\n"
    "            if(!picked||picked.branch===current||!current.head||attrs.type)return null;\n"
    "            if(picked.parents.length===2&&!attrs.parent)return null;\n"
    "            if(attrs.parent&&(!ids[attrs.parent]||picked.parents.indexOf(ids[attrs.parent])<0))return null;\n"
    "        }else{\n"
    "            if(attrs.parent||attrs.id&&ids[attrs.id])return null;\n"
    "            if(other)parents.push(other);\n"
    "        }\n"
    "        commit={id:picked?'':attrs.id||'',branch:current,parents:parents,picked:picked,\n"
    "            type:attrs.type||(picked?'PICK':other?'MERGE':'NORMAL'),tags:attrs.tags,seq:ctx.commits.length};\n"
    "        if(commit.id)ids[commit.id]=commit;\n"
    "        ctx.commits.push(commit);current.head=commit;\n"
    "    }\n"
    "    if(!ctx.commits.length)return null;\n"
    "    ctx.branches.sort(function(a,b){return a.order-b.order||a.index-b.index;});\n"
    "    return ctx;\n"
    "}\n"
    "function mmRenderGitGraph(view,ctx){\n"
    "    var svg=mmSvgEl('svg'), f=mmBodyFontPx(), horizontal=ctx.direction==='LR', reverse=ctx.direction==='BT';\n"
    "    var branches=ctx.branches, commits=ctx.commits, i,j,b,c,end=0,cross=0,branchWidth=0;\n"
    "    for(i=0;i<branches.length;i++){\n"
    "        b=branches[i];b.labelWidth=mmTextWidth(b.name,f);b.width=b.labelWidth;\n"
    "        branchWidth=Math.max(branchWidth,b.width);\n"
    "    }\n"
    "    for(i=0;i<commits.length;i++){\n"
    "        c=commits[i];\n"
    "        c.lines=[c.picked?'Cherry-pick '+(c.picked.id||'#'+(c.picked.seq+1)):c.id||'#'+(i+1)];\n"
    "        if(c.tags.length)c.lines.push(c.tags.join(', '));\n"
    "        c.labelWidth=0;\n"
    "        for(j=0;j<c.lines.length;j++)c.labelWidth=Math.max(c.labelWidth,mmTextWidth(c.lines[j],f));\n"
    "        c.branch.width=Math.max(c.branch.width,c.labelWidth+f*1.3);\n"
    "        c.major=i?commits[i-1].major+(horizontal?(c.labelWidth+commits[i-1].labelWidth)/2+f*2.5:f*3.5):\n"
    "            horizontal?branchWidth+f*2.5+c.labelWidth/2:f*2.7;\n"
    "    }\n"
    "    end=commits[commits.length-1].major+f*1.35;\n"
    "    for(i=0;i<branches.length;i++){\n"
    "        b=branches[i];b.cross=cross;\n"
    "        cross+=horizontal?f*4.25:b.width+f*1.8;\n"
    "    }\n"
    "    function coord(major,minor){\n"
    "        return horizontal?[major,minor]:[minor,reverse?end-major:major];\n"
    "    }\n"
    "    function point(major,minor){return coord(major,minor).join(',');}\n"
    "    function rail(from,to,isPick,secondary){\n"
    "        var a=from.branch.cross,b=to.branch.cross, start=from.major, finish=to.major;\n"
    "        var d='M'+point(start,a);\n"
    "        if(a!==b){\n"
    "            // Forks leave near the parent. Cross-branch merges and picks enter near the destination.\n"
    "            var slot=secondary||isPick?to.seq-1:from.seq, prev=commits[slot], next=commits[slot+1];\n"
    "            var left=prev.major+(horizontal?prev.labelWidth/2+f*.35:f*1.35);\n"
    "            var right=next.major-(horizontal?next.labelWidth/2+f*.35:f*1.35), middle=(left+right)/2;\n"
    "            d+=' L'+point(left,a)+' C'+point(middle,a)+' '+point(middle,b)+' '+point(right,b);\n"
    "        }\n"
    "        d+=' L'+point(finish,b);\n"
    "        mmShape(svg,'path',{d:d},'mdv-mm-edge'+mmAccent((isPick?from:to).branch.index)+(isPick?' dash':''));\n"
    "    }\n"
    "    for(i=0;i<branches.length;i++){\n"
    "        b=branches[i];\n"
    "        var from=horizontal?branchWidth+f: f*1.75;\n"
    "        mmShape(svg,'path',{d:'M'+point(from,b.cross)+' L'+point(end,b.cross)},'mdv-mm-edge mdv-mm-grid'+mmAccent(b.index));\n"
    "        var label=coord(horizontal?branchWidth: f*.7,b.cross);\n"
    "        mmPushText(svg,label[0]+(horizontal?0:b.labelWidth/2),label[1]+f*.34,[b.name],\n"
    "            'mdv-mm-text mdv-mm-chart-heading',horizontal?'end':'middle');\n"
    "    }\n"
    "    for(i=0;i<commits.length;i++){\n"
    "        c=commits[i];\n"
    "        for(j=0;j<c.parents.length;j++)rail(c.parents[j],c,false,j>0);\n"
    "        if(c.picked)rail(c.picked,c,true,false);\n"
    "    }\n"
    "    for(i=0;i<commits.length;i++){\n"
    "        c=commits[i];\n"
    "        var pos=coord(c.major,c.branch.cross), x=pos[0],y=pos[1], r=f*.42;\n"
    "        var cls='mdv-mm-node'+mmAccent(c.branch.index);\n"
    "        if(c.type==='HIGHLIGHT')mmShape(svg,'rect',{x:x-r,y:y-r,width:2*r,height:2*r,rx:1},cls);\n"
    "        else if(c.type==='PICK')mmShape(svg,'polygon',{points:[x+','+(y-r),(x+r)+','+y,x+','+(y+r),(x-r)+','+y].join(' ')},cls);\n"
    "        else{\n"
    "            mmShape(svg,'circle',{cx:x,cy:y,r:r},cls);\n"
    "            if(c.type==='MERGE')mmShape(svg,'circle',{cx:x,cy:y,r:r*.5},cls);\n"
    "            if(c.type==='REVERSE')mmShape(svg,'path',{d:'M'+(x-r*.55)+','+(y-r*.55)+' L'+(x+r*.55)+','+(y+r*.55)+\n"
    "                ' M'+(x+r*.55)+','+(y-r*.55)+' L'+(x-r*.55)+','+(y+r*.55)},'mdv-mm-edge'+mmAccent(c.branch.index));\n"
    "        }\n"
    "        mmPushText(svg,x+(horizontal?0:f*.78),y+(horizontal?f*1.6:f*.34-(c.lines.length-1)*f*.625),\n"
    "            c.lines,'mdv-mm-text',horizontal?'middle':'start');\n"
    "    }\n"
    "    view.appendChild(svg);return 1;\n"
    "}\n"
    "var mmMindShapes=[\n"
    "    ['circle','((','))'],['bang','))','(('],['hexagon','{{','}}'],\n"
    "    ['rect','[',']'],['round','(',')'],['cloud',')','(']\n"
    "];\n"
    "function mmMindNode(text){\n"
    "    var token=/^([^\\s()[\\]{}]*)\\s*([()[\\]{}].*)$/.exec(text), shape='round', m,i;\n"
    "    if(token){\n"
    "        for(i=0;i<mmMindShapes.length;i++){\n"
    "            m=mmMindShapes[i];\n"
    "            if(token[2].indexOf(m[1])===0&&token[2].slice(-m[2].length)===m[2]&&token[2].length>=m[1].length+m[2].length){\n"
    "                shape=m[0];text=mmTrim(token[2].slice(m[1].length,-m[2].length));\n"
    "                if(!text)text=token[1];\n"
    "                break;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    text=mmUnquote(text);\n"
    "    return text?{label:text,shape:shape,children:[]}:null;\n"
    "}\n"
    "function mmParseMindmap(src){\n"
    "    var lines=src.split(/\\r?\\n/), nodes=[], stack=[], i,j,line,indent,node;\n"
    "    if(!/^mindmap$/i.test(mmTrim(lines[0])))return null;\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        if(/^(?:::icon\\([^()]+\\)|:::[\\w-]+(?:\\s+[\\w-]+)*)$/.test(line)){\n"
    "            if(!nodes.length)return null;\n"
    "            continue;\n"
    "        }\n"
    "        if(nodes.length>=128||/^%%\\{|^::/.test(line))return null;\n"
    "        indent=0;\n"
    "        for(j=0;j<lines[i].length;j++){\n"
    "            if(lines[i].charAt(j)===' ')indent++;\n"
    "            else if(lines[i].charAt(j)==='\\t')indent+=4-indent%4;\n"
    "            else break;\n"
    "        }\n"
    "        while(stack.length&&stack[stack.length-1].indent>=indent)stack.pop();\n"
    "        if(nodes.length&&!stack.length)return null;\n"
    "        node=mmMindNode(line);if(!node)return null;\n"
    "        node.indent=indent;node.depth=stack.length;\n"
    "        if(stack.length){\n"
    "            node.parent=stack[stack.length-1];node.parent.children.push(node);\n"
    "        }\n"
    "        stack.push(node);nodes.push(node);\n"
    "    }\n"
    "    return nodes.length?{kind:'mindmap',nodes:nodes}:null;\n"
    "}\n"
    "function mmRenderMindmap(view,ctx){\n"
    "    var svg=mmSvgEl('svg'), f=mmBodyFontPx(), nodes=ctx.nodes, root=nodes[0], gap=f*.85;\n"
    "    var sides=[[],[]], spans=[0,0], columns=[[],[]], i,j,n;\n"
    "    for(i=nodes.length-1;i>=0;i--){\n"
    "        n=nodes[i];n.lines=[];\n"
    "        var parts=n.label.split(/<br\\s*\\/?>/i), width=0;\n"
    "        for(j=0;j<parts.length;j++)n.lines=n.lines.concat(mmWrapWords(parts[j],24));\n"
    "        for(j=0;j<n.lines.length;j++)width=Math.max(width,mmTextWidth(n.lines[j],f));\n"
    "        n.w=Math.max(f*3,width+f*1.6);n.h=f*(n.lines.length*1.25+.75);\n"
    "        if(n.shape==='circle')n.w=n.h=Math.sqrt(n.w*n.w+n.h*n.h);\n"
    "        else if(n.shape==='hexagon')n.w+=f*1.6;\n"
    "        else if(n.shape==='cloud'||n.shape==='bang'){n.w*=1.35;n.h*=1.55;}\n"
    "        n.span=0;\n"
    "        for(j=0;j<n.children.length;j++)n.span+=n.children[j].span+(j?gap:0);\n"
    "        n.span=Math.max(n.span,n.h);\n"
    "    }\n"
    "    root.x=root.y=0;\n"
    "    for(i=0;i<root.children.length;i++){\n"
    "        n=root.children[i];j=spans[0]<=spans[1]?0:1;\n"
    "        n.side=j;n.accent=i;\n"
    "        spans[j]+=n.span+(sides[j].length?gap:0);sides[j].push(n);\n"
    "    }\n"
    "    for(i=1;i<nodes.length;i++){\n"
    "        n=nodes[i];\n"
    "        if(n.parent!==root){n.side=n.parent.side;n.accent=n.parent.accent;}\n"
    "        var widths=columns[n.side];\n"
    "        widths[n.depth]=Math.max(widths[n.depth]||0,n.w);\n"
    "    }\n"
    "    function place(node,top,positions,sign){\n"
    "        node.y=top+node.span/2;\n"
    "        node.x=positions[node.depth]*sign;\n"
    "        var h=0,i;\n"
    "        for(i=0;i<node.children.length;i++)h+=node.children[i].span+(i?gap:0);\n"
    "        top=node.y-h/2;\n"
    "        for(i=0;i<node.children.length;i++){\n"
    "            var child=node.children[i];place(child,top,positions,sign);top+=child.span+gap;\n"
    "        }\n"
    "    }\n"
    "    for(i=0;i<sides.length;i++){\n"
    "        var positions=[], edge=root.w/2, top=-spans[i]/2;\n"
    "        for(j=1;j<columns[i].length;j++){\n"
    "            positions[j]=edge+f*2.5+columns[i][j]/2;edge=positions[j]+columns[i][j]/2;\n"
    "        }\n"
    "        for(j=0;j<sides[i].length;j++){\n"
    "            n=sides[i][j];place(n,top,positions,i===0?1:-1);top+=n.span+gap;\n"
    "        }\n"
    "    }\n"
    "    for(i=1;i<nodes.length;i++){\n"
    "        n=nodes[i];\n"
    "        var parent=n.parent, sign=n.side===0?1:-1, a=parent.x+sign*parent.w/2, b=n.x-sign*n.w/2;\n"
    "        var mid=(a+b)/2;\n"
    "        // The line starts inside the outline so irregular shapes meet the connector cleanly.\n"
    "        var d='M'+parent.x+','+parent.y+' L'+a+','+parent.y+\n"
    "            ' C'+mid+','+parent.y+' '+mid+','+n.y+' '+b+','+n.y+' L'+n.x+','+n.y;\n"
    "        mmShape(svg,'path',{d:d},'mdv-mm-edge'+mmAccent(n.accent));\n"
    "    }\n"
    "    for(i=0;i<nodes.length;i++){\n"
    "        n=nodes[i];\n"
    "        var x=n.x,y=n.y,w=n.w,h=n.h, left=x-w/2,top=y-h/2, right=x+w/2,bottom=y+h/2;\n"
    "        var cls='mdv-mm-node'+(i?mmAccent(n.accent):'');\n"
    "        if(n.shape==='circle')mmShape(svg,'circle',{cx:x,cy:y,r:w/2},cls);\n"
    "        else if(n.shape==='hexagon'){\n"
    "            mmShape(svg,'polygon',{points:[(left+f*.8)+','+top,(right-f*.8)+','+top,right+','+y,\n"
    "                (right-f*.8)+','+bottom,(left+f*.8)+','+bottom,left+','+y].join(' ')},cls);\n"
    "        }else if(n.shape==='bang'){\n"
    "            var points=[];\n"
    "            for(j=0;j<24;j++){\n"
    "                var angle=j*Math.PI/12, scale=j%2?.82:1;\n"
    "                points.push((x+Math.cos(angle)*w/2*scale)+','+(y+Math.sin(angle)*h/2*scale));\n"
    "            }\n"
    "            mmShape(svg,'polygon',{points:points.join(' ')},cls);\n"
    "        }else if(n.shape==='cloud'){\n"
    "            var cloud='M'+(left+w*.16)+','+(top+h*.82)+\n"
    "                ' C'+left+','+(top+h*.82)+' '+left+','+(top+h*.36)+' '+(left+w*.16)+','+(top+h*.34)+\n"
    "                ' C'+(left+w*.12)+','+top+' '+(left+w*.41)+','+top+' '+(left+w*.48)+','+(top+h*.13)+\n"
    "                ' C'+(left+w*.66)+','+(top-h*.04)+' '+(left+w*.9)+','+top+' '+(left+w*.88)+','+(top+h*.32)+\n"
    "                ' C'+right+','+(top+h*.33)+' '+(right+w*.04)+','+(top+h*.77)+' '+(left+w*.84)+','+(top+h*.82)+\n"
    "                ' C'+(left+w*.74)+','+bottom+' '+(left+w*.57)+','+bottom+' '+(left+w*.49)+','+(top+h*.87)+\n"
    "                ' C'+(left+w*.36)+','+(bottom+h*.04)+' '+(left+w*.21)+','+bottom+' '+(left+w*.16)+','+(top+h*.82)+' Z';\n"
    "            mmShape(svg,'path',{d:cloud},cls);\n"
    "        }else mmShape(svg,'rect',{x:left,y:top,width:w,height:h,rx:n.shape==='round'?f*.5:0},cls);\n"
    "        mmPushText(svg,x,y+f*(.34-(n.lines.length-1)*.625),n.lines,'mdv-mm-text'+(i?'':' mdv-mm-chart-heading'));\n"
    "    }\n"
    "    view.appendChild(svg);return 1;\n"
    "}\n"
    );
    sb_append(sb,
    "var mmDataNumber='[+-]?(?:\\\\d+(?:\\\\.\\\\d*)?|\\\\.\\\\d+)(?:[eE][+-]?\\\\d+)?';\n"
    "var mmDataWord='\"(?:\\\\\\\\.|[^\"\\\\\\\\])*\"';\n"
    "var mmDataId='(?:'+mmDataWord+'|[\\\\w\\\\u0080-\\\\uffff-]+)';\n"
    "var mmDataKey='('+mmDataId+')(?:\\\\s*\\\\[\\\\s*('+mmDataWord+')\\\\s*\\\\])?';\n"
    "function mmDataNumeric(text){\n"
    "    return new RegExp('^'+mmDataNumber+'$').test(text)&&isFinite(+text)?+text:NaN;\n"
    "}\n"
    "function mmDataValue(value){return String(+value.toPrecision(8));}\n"
    "function mmDataTip(element,label){\n"
    "    var tip=mmSvgEl('title');\n"
    "    tip.appendChild(document.createTextNode(label));element.appendChild(tip);\n"
    "}\n"
    "function mmParseRadar(lines,title){\n"
    "    var ctx={kind:'radar',title:title,axes:[],curves:[],min:0,max:null,legend:true,grid:'circle',ticks:5};\n"
    "    var axes=Object.create(null), curves=Object.create(null);\n"
    "    var axisPattern=new RegExp('^'+mmDataKey+'\\\\s*(,|$)\\\\s*(.*)$');\n"
    "    var curvePattern=new RegExp('^'+mmDataKey+'\\\\s*\\\\{([^{}]+)\\\\}\\\\s*(,|$)\\\\s*(.*)$');\n"
    "    var i,j,line,m,args,id,label,list,values,number,key,index,largest=-Infinity;\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        m=/^(title|axis|curve|min|max|ticks|graticule|showLegend)\\s+(.+)$/i.exec(line);\n"
    "        if(!m)return null;\n"
    "        key=m[1].toLowerCase();args=m[2];\n"
    "        if(key==='axis'||key==='curve'){\n"
    "            while(args){\n"
    "                m=(key==='axis'?axisPattern:curvePattern).exec(args);\n"
    "                if(!m)return null;\n"
    "                id=mmGitWord(m[1]);label=m[2]?mmGitWord(m[2]):id;\n"
    "                if(!id)return null;\n"
    "                if(key==='axis'){\n"
    "                    if(axes[id]||ctx.axes.length>=32)return null;\n"
    "                    axes[id]=true;ctx.axes.push({id:id,label:label});\n"
    "                    if(m[3]&&!m[4])return null;\n"
    "                    args=m[4];\n"
    "                }else{\n"
    "                    if(curves[id]||ctx.curves.length>=16)return null;\n"
    "                    curves[id]=true;ctx.curves.push({label:label,raw:m[3]});\n"
    "                    if(m[4]&&!m[5])return null;\n"
    "                    args=m[5];\n"
    "                }\n"
    "            }\n"
    "        }else if(key==='title')ctx.title=mmGitWord(args);\n"
    "        else if(key==='showlegend'){\n"
    "            if(!/^(true|false)$/i.test(args))return null;\n"
    "            ctx.legend=/^true$/i.test(args);\n"
    "        }else if(key==='graticule'){\n"
    "            if(!/^(polygon|circle)$/i.test(args))return null;\n"
    "            ctx.grid=args.toLowerCase();\n"
    "        }else{\n"
    "            number=mmDataNumeric(args);\n"
    "            if(!isFinite(number)||(key==='ticks'&&(number<1||number>20||number!==Math.floor(number))))return null;\n"
    "            ctx[key]=number;\n"
    "        }\n"
    "    }\n"
    "    if(ctx.axes.length<3||!ctx.curves.length)return null;\n"
    "    var keyed=new RegExp('^('+mmDataId+')\\\\s*:\\\\s*('+mmDataNumber+')$');\n"
    "    for(i=0;i<ctx.axes.length;i++)axes[ctx.axes[i].id]=i;\n"
    "    for(i=0;i<ctx.curves.length;i++){\n"
    "        list=ctx.curves[i].raw.split(/,(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)/);values=[];\n"
    "        if(list.length!==ctx.axes.length)return null;\n"
    "        var byId=list[0].indexOf(':')>=0;\n"
    "        for(j=0;j<list.length;j++){\n"
    "            args=mmTrim(list[j]);index=j;\n"
    "            if(byId){\n"
    "                m=keyed.exec(args);\n"
    "                if(!m)return null;\n"
    "                index=axes[mmGitWord(m[1])];args=m[2];\n"
    "                if(typeof index!=='number'||typeof values[index]!=='undefined')return null;\n"
    "            }\n"
    "            number=mmDataNumeric(args);\n"
    "            if(!isFinite(number)||number<ctx.min||(ctx.max!==null&&number>ctx.max))return null;\n"
    "            values[index]=number;largest=Math.max(number,largest);\n"
    "        }\n"
    "        ctx.curves[i].values=values;\n"
    "    }\n"
    "    if(ctx.max===null)ctx.max=largest===ctx.min?ctx.min+1:largest;\n"
    "    return ctx.max>ctx.min&&isFinite(ctx.max-ctx.min)?ctx:null;\n"
    "}\n"
    "function mmRenderRadar(view,ctx){\n"
    "    var svg=mmSvgEl('svg'), f=mmBodyFontPx(), axes=ctx.axes, curves=ctx.curves, tips=[];\n"
    "    var radius=f*Math.max(9,axes.length*.57), labelR=radius+f*1.05, i,j,p,angle,points,value;\n"
    "    var bottom=labelR+f*2, top=-labelR-f*2, width=radius*2, range=ctx.max-ctx.min;\n"
    "    function point(a,r){return [Math.cos(a)*r,Math.sin(a)*r];}\n"
    "    for(i=0;i<axes.length;i++)tips.push(2*Math.PI*i/axes.length-Math.PI/2);\n"
    "    for(i=1;i<=ctx.ticks;i++){\n"
    "        var r=radius*i/ctx.ticks;\n"
    "        if(ctx.grid==='circle')mmShape(svg,'circle',{cx:0,cy:0,r:r},'mdv-mm-edge mdv-mm-grid');\n"
    "        else{\n"
    "            points=[];\n"
    "            for(j=0;j<axes.length;j++)points.push(point(tips[j],r).join(','));\n"
    "            mmShape(svg,'polygon',{points:points.join(' ')},'mdv-mm-edge mdv-mm-grid');\n"
    "        }\n"
    "    }\n"
    "    for(i=0;i<axes.length;i++){\n"
    "        angle=tips[i];p=point(angle,radius);\n"
    "        mmShape(svg,'line',{x1:0,y1:0,x2:p[0],y2:p[1]},'mdv-mm-edge mdv-mm-grid');\n"
    "        p=point(angle,labelR);\n"
    "        var lines=mmWrapWords(axes[i].label,19);\n"
    "        var anchor=Math.abs(p[0])<f*.2?'middle':p[0]<0?'end':'start';\n"
    "        var baseline=p[1]+f*(.34-(lines.length-1)*.625);\n"
    "        if(p[1]<0)baseline-=f*.4;\n"
    "        top=Math.min(top,baseline-f*1.3);\n"
    "        bottom=Math.max(bottom,baseline+f*(lines.length*1.25+.4));\n"
    "        mmPushText(svg,p[0],baseline,lines,'mdv-mm-text',anchor);\n"
    "    }\n"
    "    for(i=0;i<curves.length;i++){\n"
    "        points=[];\n"
    "        for(j=0;j<axes.length;j++)points.push(point(tips[j],radius*((curves[i].values[j]-ctx.min)/range)));\n"
    "        mmShape(svg,'polygon',{points:points.map(function(p){return p.join(',');}).join(' ')},\n"
    "            'mdv-mm-edge mdv-mm-data-fill'+mmAccent(i));\n"
    "        for(j=0;j<points.length;j++){\n"
    "            p=points[j];\n"
    "            mmDataTip(mmShape(svg,'circle',{cx:p[0],cy:p[1],r:f*.19},'mdv-mm-radar-point'+mmAccent(i)),\n"
    "                curves[i].label+' - '+axes[j].label+': '+mmDataValue(curves[i].values[j]));\n"
    "        }\n"
    "    }\n"
    "    // Keep the tick labels in their own column where they cannot cover the curves.\n"
    "    var scaleX=labelR;\n"
    "    for(i=0;i<axes.length;i++){\n"
    "        p=point(tips[i],labelR);\n"
    "        if(p[0]>=0.2*f){\n"
    "            var wrapped=mmWrapWords(axes[i].label,19);\n"
    "            for(j=0;j<wrapped.length;j++)scaleX=Math.max(scaleX,p[0]+mmTextWidth(wrapped[j],f));\n"
    "        }\n"
    "    }\n"
    "    scaleX+=f*.85;\n"
    "    var tickStep=Math.ceil(f*1.25*ctx.ticks/radius);\n"
    "    for(i=0;i<=ctx.ticks;i++){\n"
    "        if(i!==ctx.ticks&&(i%tickStep!==0||i>ctx.ticks-tickStep))continue;\n"
    "        value=ctx.min+range*(i/ctx.ticks);\n"
    "        var tickY=-radius*i/ctx.ticks+f*.34;\n"
    "        mmPushText(svg,scaleX,tickY,[mmDataValue(value)],'mdv-mm-text','start');\n"
    "    }\n"
    "    if(ctx.legend){\n"
    "        var x=-width/2, y=bottom;\n"
    "        for(i=0;i<curves.length;i++){\n"
    "            var entry=mmTextWidth(curves[i].label,f)+f*2.65;\n"
    "            if(x>-width/2&&x+entry>width/2){x=-width/2;y+=f*1.55;}\n"
    "            mmShape(svg,'line',{x1:x,y1:y-f*.34,x2:x+f,y2:y-f*.34},'mdv-mm-edge bold'+mmAccent(i));\n"
    "            mmPushText(svg,x+f*1.4,y,[curves[i].label],'mdv-mm-text','start');x+=entry;\n"
    "        }\n"
    "    }\n"
    "    if(ctx.title)mmPushText(svg,0,top-f*.8,[ctx.title],'mdv-mm-text mdv-mm-chart-heading');\n"
    "    view.appendChild(svg);return 1;\n"
    "}\n"
    "function mmParseTreemap(lines,title){\n"
    "    var ctx={kind:'treemap',title:title,root:{children:[]}}, nodes=[], stack=[];\n"
    "    var declaration=new RegExp('^('+mmDataWord+')(?:\\\\s*:\\\\s*('+mmDataNumber+'))?$');\n"
    "    var i,j,line,m,indent,node;\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        if((m=/^title\\s+(.+)$/i.exec(line))){ctx.title=mmGitWord(m[1]);continue;}\n"
    "        m=declaration.exec(line);\n"
    "        if(!m||nodes.length>=128)return null;\n"
    "        indent=0;\n"
    "        for(j=0;j<lines[i].length;j++){\n"
    "            if(lines[i].charAt(j)===' ')indent++;\n"
    "            else if(lines[i].charAt(j)==='\\t')indent+=4-indent%4;\n"
    "            else break;\n"
    "        }\n"
    "        while(stack.length&&stack[stack.length-1].indent>=indent)stack.pop();\n"
    "        var parent=stack.length?stack[stack.length-1]:ctx.root;\n"
    "        if(parent.leaf)return null;\n"
    "        node={label:mmGitWord(m[1]),leaf:typeof m[2]!=='undefined',value:m[2]?+m[2]:0,indent:indent,children:[]};\n"
    "        if(!node.label||!isFinite(node.value)||node.value<0)return null;\n"
    "        parent.children.push(node);nodes.push(node);stack.push(node);\n"
    "    }\n"
    "    for(i=nodes.length-1;i>=0;i--){\n"
    "        node=nodes[i];\n"
    "        if(!node.leaf&&!node.children.length)return null;\n"
    "        for(j=0;j<node.children.length;j++)node.value+=node.children[j].value;\n"
    "        if(!isFinite(node.value))return null;\n"
    "    }\n"
    "    ctx.root.value=0;\n"
    "    for(i=0;i<ctx.root.children.length;i++)ctx.root.value+=ctx.root.children[i].value;\n"
    "    return ctx.root.value>0&&isFinite(ctx.root.value)?ctx:null;\n"
    "}\n"
    "function mmTreemapTiles(children,x,y,w,h,total,paint){\n"
    "    // Group nearby large tiles into strips along the short side to avoid narrow slivers.\n"
    "    var items=children.map(function(child,i){return {child:child,i:i};}).filter(function(item){return item.child.value>0;});\n"
    "    items.sort(function(a,b){return b.child.value-a.child.value||a.i-b.i;});\n"
    "    var start=0,end,area,next,min,max,side,strip,cursor,span,remaining=total;\n"
    "    function aspect(sum,small,big,length){\n"
    "        var square=sum*sum, edge=length*length;\n"
    "        return Math.max(edge*big/square,square/(edge*small));\n"
    "    }\n"
    "    while(start<items.length&&w>0&&h>0){\n"
    "        var horizontal=w>=h;\n"
    "        side=horizontal?h:w;var surface=w*h;\n"
    "        area=items[start].child.value/remaining*surface;\n"
    "        max=min=area;end=start+1;\n"
    "        while(end<items.length){\n"
    "            next=items[end].child.value/remaining*surface;\n"
    "            if(aspect(area+next,next,max,side)>aspect(area,min,max,side))break;\n"
    "            area+=next;min=next;end++;\n"
    "        }\n"
    "        strip=end===items.length?(horizontal?w:h):Math.min(horizontal?w:h,area/side);\n"
    "        cursor=0;\n"
    "        for(var j=start;j<end;j++){\n"
    "            var item=items[j];\n"
    "            span=j===end-1?Math.max(0,side-cursor):Math.min(side-cursor,item.child.value/remaining*surface/area*side);\n"
    "            if(horizontal)paint(item.child,item.i,x,y+cursor,strip,span);\n"
    "            else paint(item.child,item.i,x+cursor,y,span,strip);\n"
    "            cursor+=span;\n"
    "        }\n"
    "        if(horizontal){x+=strip;w-=strip;}\n"
    "        else{y+=strip;h-=strip;}\n"
    "        start=end;\n"
    "        remaining=0;\n"
    "        for(var k=start;k<items.length;k++)remaining+=items[k].child.value;\n"
    "    }\n"
    "}\n"
    "function mmRenderTreemap(view,ctx){\n"
    "    var svg=mmSvgEl('svg'), f=mmBodyFontPx(), width=f*41,height=f*25;\n"
    "    var y=ctx.title?f*2.45:0;\n"
    "    function draw(node,index,x,y,w,h,accent){\n"
    "        if(w<.1||h<.1)return;\n"
    "        var gap=Math.min(f*.18,w*.045,h*.045);\n"
    "        var group=node.children.length>0;\n"
    "        var el=mmShape(svg,'rect',{x:x+gap,y:y+gap,width:w-2*gap,height:h-2*gap,rx:Math.min(gap,2)},\n"
    "            'mdv-mm-data-fill'+(group?' mdv-mm-tree-group':'')+mmAccent(accent));\n"
    "        mmDataTip(el,node.label+': '+mmDataValue(node.value));\n"
    "        var room=w-2*gap-f*.66, label=node.label;\n"
    "        var labelFits=h>f*1.6&&room>=mmTextWidth(label,f);\n"
    "        var header=labelFits&&(!group||h>=f*4)?f*1.55:0;\n"
    "        if(header)mmPushText(svg,x+gap+f*.33,y+gap+f*1.15,[label],\n"
    "            'mdv-mm-text'+(group?' mdv-mm-chart-heading':''),'start');\n"
    "        if(group){\n"
    "            var inset=gap+Math.min(f*.24,w*.06,h*.06);\n"
    "            mmTreemapTiles(node.children,x+inset,y+inset+header,w-2*inset,h-2*inset-header,node.value,\n"
    "                function(child,i,cx,cy,cw,ch){draw(child,i,cx,cy,cw,ch,accent);});\n"
    "        }else{\n"
    "            label=mmDataValue(node.value);\n"
    "            if(h>f*2.9&&header&&room>=mmTextWidth(label,f))\n"
    "                mmPushText(svg,x+gap+f*.33,y+gap+f*2.4,[label],'mdv-mm-text','start');\n"
    "        }\n"
    "    }\n"
    "    mmTreemapTiles(ctx.root.children,0,y,width,height,ctx.root.value,\n"
    "        function(node,i,x,y,w,h){draw(node,i,x,y,w,h,i);});\n"
    "    if(ctx.title)mmPushText(svg,width/2,f*1.25,[ctx.title],'mdv-mm-text mdv-mm-chart-heading');\n"
    "    view.appendChild(svg);return 1;\n"
    "}\n"
    "function mmParseVenn(lines,title){\n"
    "    var ctx={kind:'venn',title:title,sets:[],regions:[]}, ids=Object.create(null);\n"
    "    var seen=Object.create(null), current=null, parentIndent=0;\n"
    "    var setPattern=new RegExp('^'+mmDataKey+'(?:\\\\s*:\\\\s*('+mmDataNumber+'))?$');\n"
    "    var unionPattern=new RegExp('^('+mmDataId+'(?:\\\\s*,\\\\s*'+mmDataId+')+)'+\n"
    "        '(?:\\\\s*\\\\[\\\\s*('+mmDataWord+')\\\\s*\\\\])?(?:\\\\s*:\\\\s*('+mmDataNumber+'))?$');\n"
    "    var idPattern=new RegExp(mmDataId,'g');\n"
    "    var i,j,line,m,statement,id,label,size,region,list,key,indent;\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        m=/^(title|set|union|text)\\s+(.+)$/i.exec(line);\n"
    "        if(!m)return null;\n"
    "        statement=m[1].toLowerCase();line=m[2];indent=lines[i].search(/\\S/);\n"
    "        if(statement==='title'){ctx.title=mmGitWord(line);continue;}\n"
    "        m=(statement==='union'?unionPattern:setPattern).exec(line);\n"
    "        if(!m)return null;\n"
    "        label=m[2]?mmGitWord(m[2]):'';\n"
    "        size=m[3]?+m[3]:null;\n"
    "        if(size!==null&&(!isFinite(size)||size<0))return null;\n"
    "        if(statement==='text'){\n"
    "            if(!current||indent<=parentIndent||size!==null||current.text.length>=24)return null;\n"
    "            current.text.push(label||mmGitWord(m[1]));continue;\n"
    "        }\n"
    "        if(statement==='set'){\n"
    "            id=mmGitWord(m[1]);\n"
    "            if(!id||ids[id]||ctx.sets.length>=6||size===0)return null;\n"
    "            region={id:id,label:label||id,size:size===null?10:size,text:[],members:[ctx.sets.length]};\n"
    "            ids[id]=region;ctx.sets.push(region);\n"
    "        }else{\n"
    "            list=m[1].match(idPattern);\n"
    "            region={label:label,size:size,text:[],members:[]};\n"
    "            for(j=0;j<list.length;j++){\n"
    "                id=mmGitWord(list[j]);var set=ids[id];\n"
    "                if(!set||region.members.indexOf(set.members[0])>=0||\n"
    "                    (size!==null&&size>set.size))return null;\n"
    "                region.members.push(set.members[0]);\n"
    "            }\n"
    "            region.members.sort(function(a,b){return a-b;});\n"
    "            key=region.members.join(',');\n"
    "            if(seen[key])return null;\n"
    "            seen[key]=true;\n"
    "        }\n"
    "        if(ctx.regions.length>=64)return null;\n"
    "        ctx.regions.push(region);current=region;parentIndent=indent;\n"
    "    }\n"
    "    return ctx.sets.length?ctx:null;\n"
    "}\n"
    "function mmVennDistance(a,b,area){\n"
    "    if(area<=0)return a+b;\n"
    "    var low=Math.abs(a-b),high=a+b,mid,aa,bb,overlap;\n"
    "    for(var i=0;i<32;i++){\n"
    "        mid=(low+high)/2;\n"
    "        aa=Math.acos(Math.max(-1,Math.min(1,(mid*mid+a*a-b*b)/(2*mid*a))));\n"
    "        bb=Math.acos(Math.max(-1,Math.min(1,(mid*mid+b*b-a*a)/(2*mid*b))));\n"
    "        overlap=a*a*(aa-Math.sin(2*aa)/2)+b*b*(bb-Math.sin(2*bb)/2);\n"
    "        if(overlap>area)low=mid;\n"
    "        else high=mid;\n"
    "    }\n"
    "    return (low+high)/2;\n"
    "}\n"
    "function mmVennLayout(ctx){\n"
    "    var sets=ctx.sets, regions=ctx.regions, n=sets.length, max=0, pairs=[], centers=[];\n"
    "    var i,j,k,u,area,a,b,v,step;\n"
    "    for(i=0;i<n;i++)max=Math.max(max,sets[i].size);\n"
    "    for(i=0;i<n;i++){\n"
    "        pairs[i]=[];\n"
    "        centers.push({r:Math.sqrt(sets[i].size/max/Math.PI),\n"
    "            x:n===1?0:Math.cos(Math.PI+2*Math.PI*i/n),y:n===1?0:Math.sin(Math.PI+2*Math.PI*i/n)});\n"
    "        if(centers[i].r<1e-7)return null;\n"
    "    }\n"
    "    for(i=0;i<regions.length;i++){\n"
    "        u=regions[i];\n"
    "        if(u.members.length===2)pairs[u.members[0]][u.members[1]]=u.size===null?\n"
    "            Math.min(sets[u.members[0]].size,sets[u.members[1]].size)/4:u.size;\n"
    "    }\n"
    "    for(i=0;i<regions.length;i++){\n"
    "        u=regions[i];\n"
    "        if(u.members.length<3||u.size===0)continue;\n"
    "        for(j=0;j<u.members.length;j++)for(k=j+1;k<u.members.length;k++){\n"
    "            a=u.members[j];b=u.members[k];\n"
    "            if(typeof pairs[a][b]==='undefined')pairs[a][b]=Math.max(u.size||0,Math.min(sets[a].size,sets[b].size)/4);\n"
    "            else if(pairs[a][b]<=0||(u.size!==null&&pairs[a][b]<u.size))return null;\n"
    "        }\n"
    "    }\n"
    "    var links=[];\n"
    "    for(i=0;i<n;i++)for(j=i+1;j<n;j++){\n"
    "        area=(pairs[i][j]||0)/max;a=centers[i];b=centers[j];\n"
    "        links.push({a:a,b:b,overlap:area>0,distance:mmVennDistance(a.r,b.r,area)+(area?0:.13)});\n"
    "    }\n"
    "    // Relax circle centers against distances inferred from pairwise overlap areas.\n"
    "    for(step=0;step<250;step++){\n"
    "        for(i=0;i<links.length;i++){\n"
    "            var link=links[i];a=link.a;b=link.b;\n"
    "            var dx=b.x-a.x,dy=b.y-a.y,d=Math.sqrt(dx*dx+dy*dy);\n"
    "            if(!d){dx=1;dy=0;}\n"
    "            v=(d-link.distance)*(link.overlap?.24:.42);\n"
    "            if(!link.overlap&&v>=0)continue;\n"
    "            dx*=v/(d||1);dy*=v/(d||1);\n"
    "            a.x+=dx;a.y+=dy;b.x-=dx;b.y-=dy;\n"
    "        }\n"
    "    }\n"
    "    return centers;\n"
    "}\n"
    "function mmVennPoint(circles,members,exclusive){\n"
    "    var left=-Infinity,right=Infinity,top=-Infinity,bottom=Infinity,i,j,best=null,bestMargin=0;\n"
    "    for(i=0;i<members.length;i++){\n"
    "        var c=circles[members[i]];\n"
    "        left=Math.max(left,c.x-c.r);right=Math.min(right,c.x+c.r);\n"
    "        top=Math.max(top,c.y-c.r);bottom=Math.min(bottom,c.y+c.r);\n"
    "    }\n"
    "    if(right<=left||bottom<=top)return null;\n"
    "    for(i=0;i<=28;i++)for(j=0;j<=28;j++){\n"
    "        var x=left+(right-left)*i/28,y=top+(bottom-top)*j/28,margin=Infinity;\n"
    "        for(var k=0;k<circles.length;k++){\n"
    "            c=circles[k];var dx=x-c.x,dy=y-c.y;\n"
    "            var distance=c.r-Math.sqrt(dx*dx+dy*dy);\n"
    "            if(members.indexOf(k)>=0)margin=Math.min(margin,distance);\n"
    "            else if(exclusive)margin=Math.min(margin,-distance);\n"
    "        }\n"
    "        if(margin>bestMargin){bestMargin=margin;best={x:x,y:y,margin:margin};}\n"
    "    }\n"
    "    return best;\n"
    "}\n"
    "function mmRenderVenn(view,ctx){\n"
    "    var circles=mmVennLayout(ctx);\n"
    "    if(!circles)return 0;\n"
    "    var svg=mmSvgEl('svg'), f=mmBodyFontPx(), i,j,c,region,lines,pt,scale;\n"
    "    var minX=Infinity,minY=Infinity,maxX=-Infinity,maxY=-Infinity,legend=[];\n"
    "    for(i=0;i<circles.length;i++){\n"
    "        c=circles[i];\n"
    "        minX=Math.min(minX,c.x-c.r);maxX=Math.max(maxX,c.x+c.r);\n"
    "        minY=Math.min(minY,c.y-c.r);maxY=Math.max(maxY,c.y+c.r);\n"
    "    }\n"
    "    scale=f*Math.min(39/(maxX-minX),24/(maxY-minY));\n"
    "    var w=(maxX-minX)*scale,h=(maxY-minY)*scale, y=ctx.title?f*2.65:0;\n"
    "    for(i=0;i<circles.length;i++){\n"
    "        c=circles[i];c.x=(c.x-minX)*scale;c.y=(c.y-minY)*scale+y;c.r*=scale;\n"
    "        mmDataTip(mmShape(svg,'circle',{cx:c.x,cy:c.y,r:c.r},'mdv-mm-data-fill'+mmAccent(i)),\n"
    "            ctx.sets[i].label+': '+mmDataValue(ctx.sets[i].size));\n"
    "    }\n"
    "    for(i=0;i<ctx.regions.length;i++){\n"
    "        region=ctx.regions[i];\n"
    "        lines=region.label?mmWrapWords(region.label,18):[];\n"
    "        for(j=0;j<region.text.length;j++)lines=lines.concat(mmWrapWords(region.text[j],18));\n"
    "        if(!lines.length)continue;\n"
    "        pt=mmVennPoint(circles,region.members,true);\n"
    "        var width=0,height;\n"
    "        for(j=0;j<lines.length;j++)width=Math.max(width,mmTextWidth(lines[j],f));\n"
    "        height=f*(lines.length*1.25+.35);\n"
    "        if(pt&&pt.margin>=Math.sqrt(width*width+height*height)/2){\n"
    "            mmPushText(svg,pt.x,pt.y+f*(.34-(lines.length-1)*.625),lines,'mdv-mm-text');\n"
    "        }else{\n"
    "            var names=[];\n"
    "            for(j=0;j<region.members.length;j++)names.push(ctx.sets[region.members[j]].label);\n"
    "            var label=region.members.length>1?names.join(' \\u2229 ')+(region.label?' - '+region.label:''):region.label;\n"
    "            if(region.text.length)label+=': '+region.text.join(', ');\n"
    "            legend.push({text:label,members:region.members});\n"
    "        }\n"
    "    }\n"
    "    for(i=0;i<legend.length;i++){\n"
    "        var entry=legend[i];\n"
    "        if(entry.members.length===1)\n"
    "            mmShape(svg,'line',{x1:0,y1:y+h+f*(1.1+i*1.55),x2:f,y2:y+h+f*(1.1+i*1.55)},\n"
    "                'mdv-mm-edge bold'+mmAccent(entry.members[0]));\n"
    "        mmPushText(svg,f*1.4,y+h+f*(1.45+i*1.55),[entry.text],'mdv-mm-text','start');\n"
    "    }\n"
    "    if(ctx.title)mmPushText(svg,w/2,f*1.3,[ctx.title],'mdv-mm-text mdv-mm-chart-heading');\n"
    "    view.appendChild(svg);return 1;\n"
    "}\n"
    );
    sb_append(sb,
    "function mmParsePacket(lines,title,options){\n"
    "    var ctx={kind:'packet',title:title,fields:[],bits:32,showBits:true,descending:false};\n"
    "    var i,line,m,start,end,next=0;\n"
    "    if(options.bitsPerRow){\n"
    "        ctx.bits=+options.bitsPerRow;\n"
    "        if(!/^\\d+$/.test(options.bitsPerRow)||ctx.bits<1||ctx.bits>64)return null;\n"
    "    }\n"
    "    if(options.showBits){\n"
    "        if(!/^(true|false)$/.test(options.showBits))return null;\n"
    "        ctx.showBits=options.showBits==='true';\n"
    "    }\n"
    "    if(options.bitOrder){\n"
    "        if(!/^(ascending|descending)$/.test(options.bitOrder))return null;\n"
    "        ctx.descending=options.bitOrder==='descending';\n"
    "    }\n"
    "    var field=new RegExp('^(\\\\+\\\\d+|\\\\d+)(?:\\\\s*-\\\\s*(\\\\d+))?\\\\s*:\\\\s*('+mmDataWord+')\\\\s*(?:%%.*)?$');\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        if((m=/^title\\s+(.+)$/i.exec(line))){\n"
    "            ctx.title=mmGitWord(mmTrim(m[1].replace(/\\s+%%.*$/,'')));continue;\n"
    "        }\n"
    "        m=field.exec(line);\n"
    "        if(!m||ctx.fields.length>=128)return null;\n"
    "        if(m[1].charAt(0)==='+'){\n"
    "            if(m[2]||+m[1]<=0)return null;\n"
    "            start=next;end=start+(+m[1])-1;\n"
    "        }else{\n"
    "            start=+m[1];end=typeof m[2]==='undefined'?start:+m[2];\n"
    "        }\n"
    "        if(start<next||end<start||end>=ctx.bits*128)return null;\n"
    "        ctx.fields.push({start:start,end:end,label:mmGitWord(m[3])});\n"
    "        next=end+1;\n"
    "    }\n"
    "    return ctx.fields.length?ctx:null;\n"
    "}\n"
    "function mmRenderPacket(view,ctx){\n"
    "    var svg=mmSvgEl('svg'),f=mmBodyFontPx(),bits=ctx.bits,fields=ctx.fields;\n"
    "    var cell=Math.max(f*1.28,mmTextWidth(String(fields[fields.length-1].end),f)+f*.35);\n"
    "    var w=bits*cell, top=ctx.title?f*2.25:0, rowH=f*(ctx.showBits?5.55:4.05), boxH=f*3.85;\n"
    "    var i,field,n,row,last,x,y,width,lo,hi,label,rotate,room,el;\n"
    "    if(ctx.title)mmPushText(svg,w/2,f*1.2,[ctx.title],'mdv-mm-text mdv-mm-chart-heading');\n"
    "    for(i=0;i<fields.length;i++){\n"
    "        field=fields[i];n=field.start;\n"
    "        while(n<=field.end){\n"
    "            row=Math.floor(n/bits);last=Math.min(field.end,(row+1)*bits-1);\n"
    "            width=(last-n+1)*cell;\n"
    "            x=ctx.descending?(bits-1-last%bits)*cell:n%bits*cell;\n"
    "            y=top+row*rowH+(ctx.showBits?f*1.45:0);\n"
    "            el=mmShape(svg,'rect',{x:x,y:y,width:width,height:boxH},'mdv-mm-node');\n"
    "            mmDataTip(el,field.label+' ('+(field.start===field.end?field.start:field.start+'-'+field.end)+')');\n"
    "            if(ctx.showBits){\n"
    "                lo=ctx.descending?last:n;hi=ctx.descending?n:last;\n"
    "                mmPushText(svg,x+cell/2,y-f*.42,[String(lo)],'mdv-mm-text');\n"
    "                if(hi!==lo)mmPushText(svg,x+width-cell/2,y-f*.42,[String(hi)],'mdv-mm-text');\n"
    "            }\n"
    "            label=field.label;\n"
    "            rotate=width<boxH&&mmTextWidth(label,f)>width-f*.35;\n"
    "            room=(rotate?boxH:width)-f*.42;\n"
    "            if(mmTextWidth(label,f)>room){\n"
    "                var cut=label.length;\n"
    "                do{cut--;}while(cut>0&&mmTextWidth(label.slice(0,cut)+'\\u2026',f)>room);\n"
    "                label=label.slice(0,cut)+'\\u2026';\n"
    "            }\n"
    "            var cx=x+width/2, cy=y+boxH/2;\n"
    "            if(rotate){\n"
    "                var group=mmShape(svg,'g',{transform:'rotate(-90 '+cx+' '+cy+')'},'');\n"
    "                mmPushText(group,cx,cy+f*.34,[label],'mdv-mm-text');\n"
    "            }else mmPushText(svg,cx,cy+f*.34,[label],'mdv-mm-text');\n"
    "            n=last+1;\n"
    "        }\n"
    "    }\n"
    "    view.appendChild(svg);return 1;\n"
    "}\n"
    "function mmParseIshikawa(lines,title){\n"
    "    var root=null,stack=[],count=0,i,j,indent,node,line;\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        if(/^%%\\{/.test(line)||++count>96)return null;\n"
    "        node={label:mmGitWord(line),children:[],depth:0};\n"
    "        if(!node.label)return null;\n"
    "        if(!root){root=node;continue;}\n"
    "        indent=0;\n"
    "        for(j=0;j<lines[i].length;j++){\n"
    "            if(lines[i].charAt(j)===' ')indent++;\n"
    "            else if(lines[i].charAt(j)==='\\t')indent+=4-indent%4;\n"
    "            else break;\n"
    "        }\n"
    "        while(stack.length&&stack[stack.length-1].indent>=indent)stack.pop();\n"
    "        var parent=stack.length?stack[stack.length-1]:root;\n"
    "        node.depth=stack.length;node.indent=indent;\n"
    "        parent.children.push(node);stack.push(node);\n"
    "    }\n"
    "    return root?{kind:'ishikawa',root:root,title:title}:null;\n"
    "}\n"
    "function mmRenderIshikawa(view,ctx){\n"
    "    var svg=mmSvgEl('svg'),f=mmBodyFontPx(),root=ctx.root,groups=[],i,j;\n"
    "    var maxY=f*2,x=0, step=f*1.25, effect=mmWrapWords(root.label,22);\n"
    "    function measure(lines){\n"
    "        var width=0;\n"
    "        for(var i=0;i<lines.length;i++)width=Math.max(width,mmTextWidth(lines[i],f));\n"
    "        return width;\n"
    "    }\n"
    "    function line(x1,y1,x2,y2,cls){\n"
    "        mmShape(svg,'line',{x1:x1,y1:y1,x2:x2,y2:y2},'mdv-mm-edge'+cls);\n"
    "    }\n"
    "    function collect(node,parent,g){\n"
    "        var rows=mmWrapWords(node.label,24),d=node.depth;\n"
    "        var item={rows:rows,depth:d,parent:parent,y:g.h+(rows.length*step)/2};\n"
    "        g.width=Math.max(g.width,measure(rows)+f*(d*1.15+6.4));\n"
    "        g.h+=rows.length*step+f*.65;\n"
    "        g.items.push(item);\n"
    "        for(var i=0;i<node.children.length;i++)collect(node.children[i],item,g);\n"
    "    }\n"
    "    for(i=0;i<root.children.length;i++){\n"
    "        var g={items:[],width:f*10,h:f*1.1};\n"
    "        collect(root.children[i],null,g);groups.push(g);maxY=Math.max(maxY,g.h);\n"
    "    }\n"
    "    for(i=0;i<groups.length;i+=2){\n"
    "        var col=Math.max(groups[i].width,groups[i+1]?groups[i+1].width:0);\n"
    "        for(j=i;j<Math.min(i+2,groups.length);j++){\n"
    "            var g=groups[j],sign=j%2===0?-1:1, end=x+col, cls=mmAccent(j);\n"
    "            line(end,0,end-f*3.5,sign*g.h,cls+' bold');\n"
    "            for(var k=0;k<g.items.length;k++){\n"
    "                var it=g.items[k],boneX=end-f*3.5*(it.y/g.h);\n"
    "                it.x=boneX-f*(1.25+1.3*it.depth);\n"
    "                var y=sign*it.y;\n"
    "                if(!it.parent)line(it.x,y,boneX,y,cls);\n"
    "                else{\n"
    "                    var parentY=sign*it.parent.y;\n"
    "                    var elbow=it.parent.x+f*.45;\n"
    "                    line(it.x,y,elbow,y,cls);\n"
    "                    line(elbow,y,elbow,parentY,cls);\n"
    "                }\n"
    "                mmPushText(svg,it.x-f*.33,y+f*(.34-(it.rows.length-1)*.625),it.rows,\n"
    "                    'mdv-mm-text'+(k===0?' mdv-mm-chart-heading':''),'end');\n"
    "            }\n"
    "        }\n"
    "        x+=col+f*.65;\n"
    "    }\n"
    "    var spine=Math.max(x+f*1.25,f*4), width=measure(effect)+f*2.05,h=effect.length*step+f*1.05;\n"
    "    line(0,0,spine,0,' bold');\n"
    "    mmShape(svg,'path',{d:'M'+(spine-f*.7)+',0 L'+spine+','+(-h/2)+' H'+(spine+width)+' V'+(h/2)+' H'+\n"
    "        spine+' Z'},'mdv-mm-node');\n"
    "    mmPushText(svg,spine+width/2,f*(.34-(effect.length-1)*.625),effect,'mdv-mm-text mdv-mm-chart-heading');\n"
    "    if(ctx.title)mmPushText(svg,(spine+width)/2,-Math.max(maxY,h/2)-f,[ctx.title],'mdv-mm-text mdv-mm-chart-heading');\n"
    "    view.appendChild(svg);return 1;\n"
    "}\n"
    );
    sb_append(sb,
    "function mmParseZenUml(lines,title){\n"
    "    var ctx={kind:'zenuml',title:title,actors:[],map:Object.create(null),items:[]};\n"
    "    var scopes=[],comments=[],reply=false,i,line,m,frame,actor,from,to,label,assign,opens;\n"
    "    var word='[\\\\w\\\\u0080-\\\\uffff$][\\\\w\\\\u0080-\\\\uffff$-]*';\n"
    "    var async=new RegExp('^('+word+')\\\\s*->\\\\s*('+word+')\\\\s*:\\\\s*(.+)$');\n"
    "    var declaration=new RegExp('^(?:@([A-Za-z][\\\\w-]*)\\\\s+)?('+word+')(?:\\\\s+as\\\\s+(.+))?$','i');\n"
    "    var call=new RegExp('^(?:('+word+')\\\\s*->\\\\s*)?(?:(new)\\\\s+('+word+')|('+word+')\\\\.('+word+'))(\\\\(.*\\\\))?$');\n"
    "    function caller(){\n"
    "        for(var n=scopes.length-1;n>=0;n--)if(scopes[n].to)return scopes[n].to;\n"
    "        if(!ctx.map['@caller']){\n"
    "            var a=mmSeqActor(ctx,'@caller','Caller');\n"
    "            ctx.actors.pop();ctx.actors.unshift(a);\n"
    "        }\n"
    "        return '@caller';\n"
    "    }\n"
    "    function message(a,b,text,kind){\n"
    "        mmSeqActor(ctx,a);mmSeqActor(ctx,b);\n"
    "        if(comments.length){\n"
    "            ctx.items.push({type:'note',side:'right',actor:a,text:comments.join(' ')});\n"
    "            comments=[];\n"
    "        }\n"
    "        ctx.items.push({type:'msg',from:a,to:b,text:text,kind:kind});\n"
    "    }\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        if(/^\\/\\//.test(line)){comments.push(mmTrim(line.slice(2)));continue;}\n"
    "        while(line.charAt(0)==='}'){\n"
    "            frame=scopes.pop();\n"
    "            if(!frame||reply)return null;\n"
    "            if(frame.group)ctx.items.push({type:'groupEnd',start:frame.group,text:''});\n"
    "            else if(frame.assign&&!frame.returned)message(frame.to,frame.from,frame.assign,'-->>');\n"
    "            line=mmTrim(line.slice(1));\n"
    "        }\n"
    "        if(!line)continue;\n"
    "        if((m=/^title\\s+(.+)$/i.exec(line))){ctx.title=mmGitWord(m[1]);continue;}\n"
    "        if(/^@(return|reply)$/i.test(line)){if(reply)return null;reply=true;continue;}\n"
    "        if((m=async.exec(line))){\n"
    "            message(m[1],m[2],m[3],reply?'-->>':'->>');\n"
    "            reply=false;continue;\n"
    "        }\n"
    "        if(reply)return null;\n"
    "        opens=/\\{\\s*$/.test(line);\n"
    "        if(opens){\n"
    "            line=mmTrim(line.slice(0,-1));\n"
    "            if((m=/^(if|else(?:\\s+if)?|opt|par|while|for(?:each)?|loop|try|catch|finally)\\b(?:\\s*(.+))?$/i.exec(line))){\n"
    "                if(comments.length)comments=[];\n"
    "                var item={type:'group',text:m[1]+(m[2]?' '+m[2]:''),depth:scopes.length};\n"
    "                ctx.items.push(item);scopes.push({group:item});continue;\n"
    "            }\n"
    "        }\n"
    "        if(!opens&&(m=/^return(?:\\s+(.+))?$/.exec(line))){\n"
    "            frame=null;\n"
    "            for(var j=scopes.length-1;j>=0;j--)if(scopes[j].to){frame=scopes[j];break;}\n"
    "            if(!frame)return null;\n"
    "            frame.returned=true;message(frame.to,frame.from,m[1]||'return','-->>');continue;\n"
    "        }\n"
    "        assign='';\n"
    "        m=/^((?:[\\w$<>.,\\[\\]]+\\s+)?[\\w$]+)\\s*=\\s*(.+)$/.exec(line);\n"
    "        if(m){assign=m[1];line=m[2];}\n"
    "        if((m=call.exec(line))){\n"
    "            from=m[1]||caller();to=m[3]||m[4];\n"
    "            label=m[2]?'new '+to+(m[6]||''):m[5]+(m[6]||'');\n"
    "            message(from,to,label,m[2]?'->>':'==>>');\n"
    "            if(opens)scopes.push({from:from,to:to,assign:assign,returned:false});\n"
    "            else if(assign)message(to,from,assign,'-->>');\n"
    "            continue;\n"
    "        }\n"
    "        if(!opens&&!assign&&(m=declaration.exec(line))){\n"
    "            actor=mmSeqActor(ctx,m[2],m[3]?mmGitWord(m[3]):'');\n"
    "            if(m[1])actor.caption=m[1];\n"
    "            comments=[];continue;\n"
    "        }\n"
    "        return null;\n"
    "    }\n"
    "    return !reply&&!scopes.length&&ctx.actors.length&&ctx.actors.length<=16&&ctx.items.length<=128?ctx:null;\n"
    "}\n"
    "var mmEventTypes={ui:[0,'UI'],pcr:[0,'Processor'],processor:[0,'Processor'],cmd:[1,'Command'],command:[1,'Command'],\n"
    "    rmo:[1,'Read model'],readmodel:[1,'Read model'],evt:[2,'Event'],event:[2,'Event']};\n"
    "var mmEventLaneNames=['UI / Automation','Commands / Read models','Events'];\n"
    "function mmEventPayload(text){\n"
    "    if(text.charAt(0)!=='{')return null;\n"
    "    var depth=0,quote='',i,c;\n"
    "    for(i=0;i<text.length;i++){\n"
    "        c=text.charAt(i);\n"
    "        if(quote){\n"
    "            if(c==='\\\\')i++;\n"
    "            else if(c===quote)quote='';\n"
    "        }else if(c==='\"'||c===\"'\"||c==='\\x60')quote=c;\n"
    "        else if(c==='{')depth++;\n"
    "        else if(c==='}'&&--depth===0)return {value:text.slice(1,i),rest:mmTrim(text.slice(i+1))};\n"
    "    }\n"
    "    return null;\n"
    "}\n"
    "function mmParseEventModel(lines,title){\n"
    "    var ctx={kind:'eventmodeling',title:title,frames:[],lanes:[],edges:[]};\n"
    "    var ids=Object.create(null),laneMap=Object.create(null),data=Object.create(null);\n"
    "    var i,j,line,m,id,key,frame,type,namespace,lane,rest,payload;\n"
    "    var name='[\\\\w\\\\u0080-\\\\uffff.-]+';\n"
    "    var framePattern=new RegExp('^(tf|timeframe|rf|resetframe)\\\\s+(\\\\d{1,9})\\\\s+(\\\\w+)\\\\s+('+name+')(?:\\\\s*(.*))?$','i');\n"
    "    var dataPattern=new RegExp('^data\\\\s+('+name+')\\\\s*(?:\\\\x60\\\\w+\\\\x60\\\\s*)?(\\\\{.*)$','i');\n"
    "    var reference=new RegExp('^\\\\[\\\\[('+name+')\\\\]\\\\]\\\\s*');\n"
    "    for(i=1;i<lines.length;i++){\n"
    "        line=mmTrim(lines[i]);\n"
    "        if(!line||/^%%(?!\\{)/.test(line))continue;\n"
    "        if((m=/^title\\s+(.+)$/i.exec(line))){ctx.title=mmGitWord(m[1]);continue;}\n"
    "        if((m=dataPattern.exec(line))){\n"
    "            key=m[1];\n"
    "            if(Object.prototype.hasOwnProperty.call(data,key))return null;\n"
    "            rest=m[2];payload=mmEventPayload(rest);\n"
    "            while(!payload&&i+1<lines.length){rest+='\\n'+lines[++i];payload=mmEventPayload(rest);}\n"
    "            if(!payload||payload.rest)return null;\n"
    "            data[key]=payload.value;continue;\n"
    "        }\n"
    "        m=framePattern.exec(line);\n"
    "        if(!m||ctx.frames.length>=64)return null;\n"
    "        id=String(+m[2]);key=m[3].toLowerCase();\n"
    "        if(ids[id]||!Object.prototype.hasOwnProperty.call(mmEventTypes,key))return null;\n"
    "        type=mmEventTypes[key];\n"
    "        var dot=m[4].indexOf('.');\n"
    "        namespace=dot>0?m[4].slice(0,dot):'';\n"
    "        var laneKey=type[0]+':'+namespace;\n"
    "        lane=laneMap[laneKey];\n"
    "        if(!lane){\n"
    "            if(ctx.lanes.length>=24)return null;\n"
    "            lane={name:(namespace?namespace+' / ':'')+mmEventLaneNames[type[0]],group:type[0],order:ctx.lanes.length};\n"
    "            laneMap[laneKey]=lane;ctx.lanes.push(lane);\n"
    "        }\n"
    "        frame={id:id,number:m[2],name:m[4],type:type[1],lane:lane,reset:/^r/i.test(m[1]),\n"
    "            sources:[],index:ctx.frames.length,data:'',ref:'',inline:false};\n"
    "        rest=mmTrim(m[5]||'');\n"
    "        while(rest){\n"
    "            if((m=/^\\x60(?:json|jsobj|figma|salt|uri|md|html|text)\\x60\\s*(?=\\{)/i.exec(rest)))rest=rest.slice(m[0].length);\n"
    "            if(rest.charAt(0)==='{'){\n"
    "                payload=mmEventPayload(rest);\n"
    "                if(!payload||frame.inline||frame.ref)return null;\n"
    "                frame.data=payload.value;frame.inline=true;rest=payload.rest;\n"
    "            }else if((m=reference.exec(rest))){\n"
    "                if(frame.ref||frame.inline)return null;\n"
    "                frame.ref=m[1];rest=rest.slice(m[0].length);\n"
    "            }else if((m=/^->>\\s*(\\d{1,9})(?=\\s|->>|$)\\s*/.exec(rest))){\n"
    "                key=String(+m[1]);\n"
    "                if(key===id||frame.sources.indexOf(key)>=0)return null;\n"
    "                frame.sources.push(key);rest=rest.slice(m[0].length);\n"
    "            }else return null;\n"
    "        }\n"
    "        ids[id]=frame;ctx.frames.push(frame);\n"
    "    }\n"
    "    if(!ctx.frames.length)return null;\n"
    "    for(i=0;i<ctx.frames.length;i++){\n"
    "        frame=ctx.frames[i];\n"
    "        if(frame.ref){\n"
    "            if(!Object.prototype.hasOwnProperty.call(data,frame.ref))return null;\n"
    "            frame.data=data[frame.ref];\n"
    "        }\n"
    "        if(frame.sources.length){\n"
    "            for(j=0;j<frame.sources.length;j++){\n"
    "                var source=ids[frame.sources[j]];\n"
    "                if(!source||ctx.edges.length>=128)return null;\n"
    "                ctx.edges.push({from:source,to:frame,explicit:true});\n"
    "            }\n"
    "        }else if(!frame.reset){\n"
    "            for(j=i-1;j>=0;j--){\n"
    "                if(ctx.frames[j].lane!==frame.lane){ctx.edges.push({from:ctx.frames[j],to:frame});break;}\n"
    "            }\n"
    "        }\n"
    "        if(ctx.edges.length>128)return null;\n"
    "    }\n"
    "    ctx.lanes.sort(function(a,b){\n"
    "        return a.group-b.group||a.order-b.order;\n"
    "    });\n"
    "    return ctx;\n"
    "}\n"
    "function mmRenderEventModel(view,ctx){\n"
    "    var svg=mmSvgEl('svg'),f=mmBodyFontPx(),frames=ctx.frames,lanes=ctx.lanes,edges=ctx.edges;\n"
    "    var gutter=f*2.8,labelW=f*9,x,y,i,j,lane,node,lines,railCount=0;\n"
    "    for(i=0;i<lanes.length;i++){\n"
    "        lane=lanes[i];lane.lines=mmWrapWords(lane.name,22);\n"
    "        lane.h=f*Math.max(4,lane.lines.length*1.25+1.2);\n"
    "        for(j=0;j<lane.lines.length;j++)labelW=Math.max(labelW,mmTextWidth(lane.lines[j],f)+f*1.3);\n"
    "    }\n"
    "    for(i=0;i<edges.length;i++){\n"
    "        if(Math.abs(edges[i].from.index-edges[i].to.index)>1)edges[i].rail=++railCount;\n"
    "    }\n"
    "    x=labelW+gutter;\n"
    "    for(i=0;i<frames.length;i++){\n"
    "        node=frames[i];\n"
    "        node.lines=mmWrapWords(node.name,23);\n"
    "        node.details=[];\n"
    "        lines=mmTrim(node.data).split(/\\r?\\n/);\n"
    "        for(j=0;j<lines.length;j++){\n"
    "            if(mmTrim(lines[j]))node.details=node.details.concat(mmWrapWords(mmTrim(lines[j]),24));\n"
    "        }\n"
    "        if(node.details.length>5)node.details=node.details.slice(0,4).concat(['\\u2026']);\n"
    "        lines=node.lines.concat(node.details,[node.number+' '+node.type]);\n"
    "        node.h=f*(lines.length*1.25+.85);node.w=f*6;\n"
    "        for(j=0;j<lines.length;j++)node.w=Math.max(node.w,mmTextWidth(lines[j],f)+f*1.45);\n"
    "        node.x=x;x+=node.w+gutter;node.lane.h=Math.max(node.lane.h,node.h+f*1.15);\n"
    "    }\n"
    "    var top=(ctx.title?f*2.4:0)+railCount*f*.58, bottom=top;\n"
    "    for(i=0;i<lanes.length;i++){\n"
    "        lane=lanes[i];lane.y=bottom;bottom+=lane.h;\n"
    "        mmShape(svg,'rect',{x:0,y:lane.y,width:x,height:lane.h},'mdv-mm-edge mdv-mm-grid');\n"
    "        mmPushText(svg,f*.65,lane.y+lane.h/2+f*(.34-(lane.lines.length-1)*.625),lane.lines,\n"
    "            'mdv-mm-text mdv-mm-chart-heading','start');\n"
    "    }\n"
    "    for(i=0;i<frames.length;i++){\n"
    "        node=frames[i];node.cy=node.lane.y+node.lane.h/2;\n"
    "    }\n"
    "    var defs=mmSvgEl('defs'), markerId='mdv-mm-event-'+(++mmMarkerId);\n"
    "    var marker=mmShape(defs,'marker',{id:markerId,markerWidth:10,markerHeight:10,refX:9,refY:3,orient:'auto'},'');\n"
    "    mmShape(marker,'path',{d:'M0,0 L0,6 L9,3 z'},'mdv-mm-marker');svg.appendChild(defs);\n"
    "    for(i=0;i<edges.length;i++){\n"
    "        var edge=edges[i],a=edge.from,b=edge.to,forward=a.index<b.index;\n"
    "        var ax=forward?a.x+a.w:a.x,bx=forward?b.x:b.x+b.w;\n"
    "        var ma=ax+(forward?1:-1)*gutter/2, mb=bx+(forward?-1:1)*gutter/2;\n"
    "        var d='M'+ax+','+a.cy+' H'+ma;\n"
    "        if(edge.rail){\n"
    "            y=top-f*.4-edge.rail*f*.55;\n"
    "            d+=' V'+y+' H'+mb;\n"
    "        }\n"
    "        d+=' V'+b.cy+' H'+bx;\n"
    "        mmShape(svg,'path',{d:d,'marker-end':'url(#'+markerId+')'},'mdv-mm-edge'+(edge.explicit?' dash':''));\n"
    "    }\n"
    "    for(i=0;i<frames.length;i++){\n"
    "        node=frames[i];y=node.cy-node.h/2;\n"
    "        var color=node.type==='Read model'?1:node.lane.group===2?3:node.lane.group===1?0:2;\n"
    "        var rect=mmShape(svg,'rect',{x:node.x,y:y,width:node.w,height:node.h,rx:4},'mdv-mm-data-fill'+mmAccent(color));\n"
    "        mmDataTip(rect,node.number+' '+node.type+' '+node.name+(node.data?'\\n'+mmTrim(node.data):''));\n"
    "        mmPushText(svg,node.x+node.w/2,y+f*1.23,[node.number+' '+node.type],'mdv-mm-text');\n"
    "        mmPushText(svg,node.x+node.w/2,y+f*2.48,node.lines,'mdv-mm-text mdv-mm-chart-heading');\n"
    "        if(node.details.length)\n"
    "            mmPushText(svg,node.x+f*.72,y+f*(2.48+1.25*node.lines.length),node.details,'mdv-mm-text','start');\n"
    "    }\n"
    "    if(ctx.title)mmPushText(svg,x/2,f*1.15,[ctx.title],'mdv-mm-text mdv-mm-chart-heading');\n"
    "    view.appendChild(svg);return 1;\n"
    "}\n"
    "function mmParseDataChart(src){\n"
    "    var lines=src.split(/\\r?\\n/), title='', header=mmTrim(lines[0]), options=Object.create(null);\n"
    "    var config=false, packetIndent=0;\n"
    "    if(header==='---'){\n"
    "        var end=1,m,line;\n"
    "        while(end<lines.length&&mmTrim(lines[end])!=='---'){\n"
    "            line=lines[end];\n"
    "            if(mmTrim(line)){\n"
    "                if((m=/^title\\s*:\\s*(.+)$/.exec(line))&&!title){\n"
    "                    title=mmGitWord(mmTrim(m[1]));\n"
    "                    if(title.charAt(0)===\"'\"&&title.slice(-1)===\"'\")title=title.slice(1,-1);\n"
    "                }else if(/^config\\s*:\\s*$/.test(line)&&!config)config=true;\n"
    "                else if(config&&!packetIndent&&(m=/^( +)packet\\s*:\\s*$/.exec(line)))packetIndent=m[1].length;\n"
    "                else if(packetIndent&&(m=/^( +)(bitsPerRow|showBits|bitOrder)\\s*:\\s*(\\w+)\\s*$/.exec(line))&&\n"
    "                    m[1].length>packetIndent&&!options[m[2]])options[m[2]]=m[3];\n"
    "                else return null;\n"
    "            }\n"
    "            end++;\n"
    "        }\n"
    "        lines=lines.slice(end+1);\n"
    "        while(lines.length&&!mmTrim(lines[0]))lines.shift();\n"
    "        header=mmTrim(lines[0]);\n"
    "    }\n"
    "    if(/^packet(?:-beta)?$/i.test(header))return mmParsePacket(lines,title,options);\n"
    "    if(config)return null;\n"
    "    if(/^radar-beta$/i.test(header))return mmParseRadar(lines,title);\n"
    "    if(/^treemap-beta$/i.test(header))return mmParseTreemap(lines,title);\n"
    "    if(/^venn-beta$/i.test(header))return mmParseVenn(lines,title);\n"
    "    if(/^ishikawa-beta$/i.test(header))return mmParseIshikawa(lines,title);\n"
    "    if(/^zenuml$/i.test(header))return mmParseZenUml(lines,title);\n"
    "    if(/^eventmodeling$/i.test(header))return mmParseEventModel(lines,title);\n"
    "    return null;\n"
    "}\n"
    "function mmParse(src){"
    "var first;if(!src||src.length>8192)return null;\n"
    "src=src.replace(/^(?:[ \\t]*(?:%%[^\\r\\n]*)?\\r?\\n)+/,'');src=mmTrim(src);first=src.split('\\n')[0];\n"
    "if(/^(?:packet(?:-beta)?|(?:radar|treemap|venn|ishikawa)-beta|zenuml|eventmodeling|---)\\s*$/i.test(first))return mmParseDataChart(src);\n"
    "if(/^gantt\\s*$/i.test(first))return mmParseGantt(src);\n"
    "if(/^gitGraph\\b/i.test(first))return mmParseGitGraph(src);\n"
    "if(/^mindmap\\b/i.test(first))return mmParseMindmap(src);\n"
    "if(/^timeline\\b/i.test(first))return mmParseTimeline(src);\n"
    "if(/^sequenceDiagram\\b/i.test(first))return mmParseSequence(src);"
    "if(/^classDiagram\\b/i.test(first))return mmParseClass(src);"
    "if(/^stateDiagram(?:-v2)?\\b/i.test(first))return mmParseState(src);"
    "if(/^(graph|flowchart)\\b/i.test(first))return mmParseFlow(src);"
    "if(/^erDiagram\\s*$/i.test(first))return mmParseER(src);\n"
    "if(/^requirementDiagram\\s*$/i.test(first))return mmParseRequirement(src);\n"
    "return null}"
    "function mmRender(block){"
    "var srcEl=mdvQS(block,'.mdv-mermaid-src'),view=mdvQS(block,'.mdv-mermaid-view'),ctx,ok=0;"
    "if(!srcEl||!view)return;srcEl=mdvQS(srcEl,'.ln-code')||srcEl;ctx=mmParse(srcEl.textContent||srcEl.innerText||'');if(!ctx)return;view.innerHTML='';"
    "if(ctx.kind==='sequence'||ctx.kind==='zenuml')ok=mmRenderSequence(view,ctx);"
    "else if(ctx.kind==='gantt')ok=mmRenderGantt(view,ctx);"
    "else if(ctx.kind==='timeline')ok=mmRenderTimeline(view,ctx);"
    "else if(ctx.kind==='gitgraph')ok=mmRenderGitGraph(view,ctx);"
    "else if(ctx.kind==='mindmap')ok=mmRenderMindmap(view,ctx);"
    "else if(ctx.kind==='radar')ok=mmRenderRadar(view,ctx);"
    "else if(ctx.kind==='treemap')ok=mmRenderTreemap(view,ctx);"
    "else if(ctx.kind==='venn')ok=mmRenderVenn(view,ctx);"
    "else if(ctx.kind==='packet')ok=mmRenderPacket(view,ctx);"
    "else if(ctx.kind==='ishikawa')ok=mmRenderIshikawa(view,ctx);"
    "else if(ctx.kind==='eventmodeling')ok=mmRenderEventModel(view,ctx);"
    "else ok=mmRenderGraph(view,ctx);"
    "if(ok){block.className=block.className.replace(/\\bok\\b/g,'');block.className+=(block.className?' ':'')+'ok';mmFitBounds(view.firstChild);}}"
    "function initMermaid(){var blocks=mdvQSA(document,'.mdv-mermaid[data-mdv-mermaid]');if(!document.createElementNS||!window.getComputedStyle)return;for(var i=0;i<blocks.length;i++){try{mmRender(blocks[i]);}catch(ex){blocks[i].className=blocks[i].className.replace(/\\bok\\b/g,'');}}}"
#if _WIN32_WINNT < 0x0600
    "function fitMermaidSvg(svg){return;}"
    "function syncMermaidTypography(){return;}"
    "function initMermaid(){return;}"
#endif
    "function pd(e){if(e.preventDefault)e.preventDefault();else e.returnValue=false}"
    "document.onkeydown=function(e){"
    "e=e||window.event;var c=e.ctrlKey||e.metaKey,k=e.keyCode;"
    "var inp=document.getElementById('mdv-fi'),ae=document.activeElement,inFind=inFindBar(ae);"
    "if(inFind){"
    "if(k===27){pd(e);hf();return false}"
    "if(k===13){pd(e);return fstep(e.shiftKey?1:0)}"
    "if(k===118){pd(e);return f7f()}"
    "if(k===114){pd(e);return hkf(e.shiftKey?1:0)}"
    "return true}"
    "if(c&&(k===187||k===107)){pd(e);zi();return false}"
    "if(c&&(k===189||k===109)){pd(e);zo();return false}"
    "if(c&&(k===48||k===96)){pd(e);zr();return false}"
    "if(c&&k===84){pd(e);ttoc();return false}"
    "if(c&&k===70){pd(e);sf();return false}"
    "if(c&&k===80){pd(e);window.print();return false}"
    "if(c&&k===71){pd(e);mdvSetScrollY(0);return false}"
    "if(c&&k===76){pd(e);tl();return false}"
    "if(c&&(k===49||k===97)){pd(e);mdvSetView(0);return false}"
    "if(c&&(k===50||k===98)){pd(e);mdvSetView(1);return false}"
    "if(c&&k===87){pd(e);mdvToggleWrap();return false}"
    "if(c&&k===77){pd(e);mdvSetSplit(!mdvIsSplit());return false}"
    "if(c&&k===89){pd(e);mdvSyncHere();return false}"
    "if(k===112||(c&&k===191)){pd(e);th();return false}"
    "if(k===118){pd(e);return f7f()}"
    "if(k===114){pd(e);return hkf(e.shiftKey?1:0)}"
    "if(k===27){hf();document.getElementById('mdv-toc').className='';document.body.style.marginRight='0';"
    "document.getElementById('mdv-help').className='';return false}"
    "};"

    /* Init */
    "window.onresize=function(){syncMermaidTypography();fitImages()};"
    "window.onload=function(){"
    "var inp=document.getElementById('mdv-fi');if(inp){"
    "var trig=function(){var s=inp;if(s._db)clearTimeout(s._db);"
    "s._db=setTimeout(function(){qf(0)},250)};"
    "inp.oninput=trig;"
    "inp.onpaste=trig;"
    "inp.oncut=trig;"
    "sfs(0,0,0);"
    "}"
    "var fops=['mdv-fword','mdv-fcase','mdv-fmanual'];"
    "for(var fi0=0;fi0<fops.length;fi0++){var fel=document.getElementById(fops[fi0]);if(fel)fel.onclick=function(){qf(0)};}"
    "var hp=document.getElementById('mdv-help');if(hp){hp.onmousewheel=mdvStopBubble;hp.onwheel=mdvStopBubble;}"
    "var hc=document.getElementById('mdv-help-close');if(hc)hc.title='Close help';"
    "var rp=mdvRenderPane(),rawp=mdvRawPane();"
    "if(rp){rp.onscroll=up;rp.onmousedown=mdvRenderDragStart;rp.onselectstart=function(){return !mdvRawDragging};rp.oncontextmenu=rp.onmousedown;}"
    "if(rawp){rawp.onmousedown=mdvRawDragStart;rawp.onmousemove=mdvRawDragMove;rawp.onmouseup=mdvRawDragEnd;rawp.onselectstart=function(){return !mdvRenderDragging};rawp.oncontextmenu=mdvRawDragStart;rawp.onscroll=up;}"
    "document.onmouseup=function(e){if(mdvRawDragging)mdvRawDragEnd(e||window.event);else if(mdvRenderDragging)mdvEndPaneDrag();return true};"
    "afw();fitImages();initMermaid();shAll();initCollapse();initDetailsFallback();initLinkTooltips();"
    "if(ln){ln=0;tl();}"
    "if(document.documentMode&&document.documentMode<10)document.body.className+=' mdv-legacy-wrap';"
    "mdvApplyWrap();mdvSetView(mdvInitialView);"
    "usv();"
    "ucc();"
    "up()};"
    "</script>");
}

/* --- HTML UI elements --- */

static const char* get_ui(void) {
    return
    "<div id=\"mdv-prog\"></div>"
    "<div id=\"mdv-fb\">"
    "<div id=\"mdv-fb-row\">"
    "<input id=\"mdv-fi\" type=\"text\" placeholder=\"Find in document...\" autocomplete=\"off\" aria-label=\"Find in document\" title=\"Find in document\">"
    "<span id=\"mdv-fc\"></span>"
    "<button class=\"fb\" onclick=\"fp()\" title=\"Previous match\" aria-label=\"Previous match\">&laquo;</button>"
    "<button class=\"fb\" onclick=\"fn()\" title=\"Next match\" aria-label=\"Next match\">&raquo;</button>"
    "<button class=\"fb primary\" onclick=\"qf(2)\" title=\"Start search using the current options\">Start searching</button>"
    "<button class=\"fb tgl\" onclick=\"fos()\" title=\"Show or hide search options\" aria-label=\"Show or hide search options\">&#9662;</button>"
    "<button class=\"fb close\" onclick=\"hf()\" title=\"Close search bar\" aria-label=\"Close search bar\">&times;</button>"
    "</div>"
    "<div id=\"mdv-fo\">"
    "<label class=\"mdv-fopt\" title=\"Match whole words only\"><input id=\"mdv-fword\" type=\"checkbox\"> Whole word only search</label>"
    "<label class=\"mdv-fopt\" title=\"Match letter case\"><input id=\"mdv-fcase\" type=\"checkbox\"> Case sensitive search</label>"
    "<label class=\"mdv-fopt\" title=\"Only search after Start searching is pressed\"><input id=\"mdv-fmanual\" type=\"checkbox\"> Disable search as you type</label>"
    "</div>"
    "</div>"
    "<div id=\"mdv-char\"></div>"
    "<div id=\"mdv-raw-char\"></div>"
    "<div id=\"mdv-status\"></div>"
    "<div id=\"mdv-toc\"><div id=\"mdv-toc-t\">Table of Contents</div></div>"
    "<div id=\"mdv-toast\"></div>"
    "<div id=\"mdv-help\">"
    "<button id=\"mdv-help-close\" onclick=\"th()\" aria-label=\"Close\">&times;</button>"
    "<h3>MDView Keyboard Shortcuts</h3>"

    "<div class=\"hrow\"><span>Copy to Clipboard</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">C</span></span></div>"
    "<div class=\"hrow\"><span>Zoom in</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">+</span></span></div>"
    "<div class=\"hrow\"><span>Zoom out</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">&minus;</span></span></div>"
    "<div class=\"hrow\"><span>Zoom with mouse wheel</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">Wheel</span></span></div>"
    "<div class=\"hrow\"><span>Reset zoom</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">0</span></span></div>"
    "<div class=\"help-sep\"></div>"

    "<div class=\"hrow\"><span>Preview</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">1</span></span></div>"
    "<div class=\"hrow\"><span>Code</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">2</span></span></div>"
    "<div class=\"hrow\"><span>Wrap source lines</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">W</span></span></div>"
    "<div class=\"hrow\"><span>Split view</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">M</span></span></div>"
    "<div class=\"hrow\"><span>Toggle dark / light</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">D</span></span></div>"
    "<div class=\"hrow\"><span>Line numbers</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">L</span></span></div>"
    "<div class=\"help-sep\"></div>"

    "<div class=\"hrow\"><span>Table of Contents</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">T</span></span></div>"
    "<div class=\"hrow\"><span>Print</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">P</span></span></div>"
    "<div class=\"hrow\"><span>Go to top</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">G</span></span></div>"
    "<div class=\"help-sep\"></div>"

    "<div class=\"hrow\"><span>Find in page</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">F</span></span></div>"
    "<div class=\"hrow\"><span>Select all in active view</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">A</span></span></div>"
    "<div class=\"hrow\"><span>Sync split panes here</span><span class=\"hkeys\"><span class=\"kc\">Ctrl</span><span class=\"kc-plus\">+</span><span class=\"kc\">Y</span></span></div>"
    "<div class=\"hrow\"><span>Continue Find forward</span><span class=\"hkeys\"><span class=\"kc\">F3</span></span></div>"
    "<div class=\"hrow\"><span>Continue Find backward</span><span class=\"hkeys\"><span class=\"kc\">Shift</span><span class=\"kc-plus\">+</span><span class=\"kc\">F3</span></span></div>"
    "<div class=\"hrow\"><span>Open find / next result</span><span class=\"hkeys\"><span class=\"kc\">F7</span></span></div>"
    "<div class=\"help-sep\"></div>"

    "<div class=\"hrow\"><span>Close find / TOC / help</span><span class=\"hkeys\"><span class=\"kc\">Esc</span></span></div>"
    "<div class=\"hrow\"><span>This help</span><span class=\"hkeys\"><span class=\"kc\">F1</span></span></div>"
    "<div class=\"help-sep\"></div>"

    "<div class=\"help-foot\">MDView v" MDVIEW_VERSION " &middot; Settings auto-saved &middot; Press Esc to close</div>"
    "</div>";
}

/* --- Lister search integration (Ctrl+F / F3 / Shift+F3) --- */

/* WLX SDK search flags (ListSearchText searchParameter) */
#ifndef LCS_FINDFIRST
    #define LCS_FINDFIRST   1
    #define LCS_MATCHCASE   2
    #define LCS_WHOLEWORDS  4
    #define LCS_BACKWARDS   8
#endif

#ifndef LC_COPY
    #define LC_COPY        1
    #define LC_NEWPARAMS   2
    #define LC_SELECT_ALL  3
    #define LC_SETPERCENT  4
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
        L"if(typeof sfs==='function'){sfs(%d,%d,typeof gfopt==='function'?gfopt('mdv-fmanual',0):0);}"
        L"if(typeof df==='function'){df('%s',%d,%d);}"
        L"})();",
        esc, matchCase ? 1 : 0, wholeWords ? 1 : 0,
        esc, matchCase ? 1 : 0, wholeWords ? 1 : 0);

    exec_js(d->pBrowser, js);
}

static void js_find_step(MDViewData* d, int backwards) {
    if (!d || !d->pBrowser) return;
    exec_js(d->pBrowser, backwards ? L"fp()" : L"fn()" );
}

/* --- File Reading --- */

static FILE* open_utf8_path_rb(const char* fn) {
    FILE* f = NULL;
    wchar_t* wfn = NULL;
    int wl;

    if (!fn) return NULL;

    wl = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fn, -1, NULL, 0);
    if (wl > 0) {
        wfn = (wchar_t*)calloc((size_t)wl, sizeof(wchar_t));
        if (wfn) {
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fn, -1, wfn, wl) > 0)
                f = _wfopen(wfn, L"rb");
            free(wfn);
            if (f) return f;
        }
    }

    return fopen(fn, "rb");
}

#include "mdview_encoding.inc"

static char* read_file_utf8(const char* fn) {
    FILE* f=open_utf8_path_rb(fn); if(!f)return NULL;
    if (fseek(f,0,SEEK_END) != 0) { fclose(f); return NULL; }
    long sz=ftell(f);
    if (sz < 0 || sz > MDVIEW_MAX_FILE_SIZE) { fclose(f); return NULL; }
    if (fseek(f,0,SEEK_SET) != 0) { fclose(f); return NULL; }
    char* buf=(char*)malloc((size_t)sz+1); if(!buf){fclose(f);return NULL;}
    size_t read = fread(buf,1,(size_t)sz,f);
    fclose(f);
    if (read != (size_t)sz) { free(buf); return NULL; }
    buf[sz]='\0';
    return decode_markdown_text(buf,read);
}

/* --- WebBrowser Control --- */

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

/* --- Window Procedure --- */

static BOOL CALLBACK FindIEServerProc(HWND hwnd, LPARAM lParam) {
    wchar_t cls[64]; GetClassNameW(hwnd, cls, 64);
    if (wcscmp(cls, L"Internet Explorer_Server") == 0) { *(HWND*)lParam = hwnd; return FALSE; }
    return TRUE;
}

/* Read current JS state and save to INI */
static void save_current_settings(IWebBrowser2* pB) {
    if (!pB || !g_iniPath[0]) return;
    /* Read values from JS globals via execScript + document.title hack */
    exec_js(pB, L"document.title=''+fs+','+mw+','+ln+','+mdvWordWrap");
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
        int rfs,rmw,rln,wrap;
        if (sscanf_s(title,"%d,%d,%d,%d",&rfs,&rmw,&rln,&wrap)!=4) return;
        g_settings.wordWrap=wrap!=0;
        save_setting_int("WordWrap",g_settings.wordWrap);
        save_setting_int("FontSize", rfs);
        save_setting_int("MaxWidth", rmw);
        save_setting_int("LineNumbers", rln);
        save_setting_int("RawFontSize", g_settings.rawFontSize);
        save_setting_str("RawFontName", g_settings.rawFontName);
    }
}

static int load_file_into_existing_view(MDViewData* d, const char* fileUtf8) {
    char* md;
    char* body;
    char* rawHtml;
    char* full;
    StrBuf cssBuf;
    StrBuf jsBuf;
    const char* ui;
    size_t fl;
    int dark;

    if (!d || !d->pBrowser || !fileUtf8) return LISTPLUGIN_ERROR;

    md = read_file_utf8(fileUtf8);
    if (!md) return LISTPLUGIN_ERROR;
    body = md_to_html(md, fileUtf8);
    if (!body) {
        free(md);
        return LISTPLUGIN_ERROR;
    }
    rawHtml = md_to_raw_html(md);
    if (!rawHtml) {
        free(md);
        free(body);
        return LISTPLUGIN_ERROR;
    }

    sb_init(&cssBuf);
    sb_init(&jsBuf);
    if (!cssBuf.data || !jsBuf.data) {
        free(md);
        free(body);
        free(rawHtml);
        if (cssBuf.data) free(cssBuf.data);
        if (jsBuf.data) free(jsBuf.data);
        return LISTPLUGIN_ERROR;
    }

    dark = resolve_color_theme(d->listerFlags);
    build_css(&cssBuf);
    MDViewHtmlState state;
    int wrap=g_settings.wordWrap;
    if (get_html_state(d,&state)) { d->viewMode=(ViewMode)state.viewMode; wrap=state.wordWrap; }
    build_js(&jsBuf,d->viewMode,wrap);
    ui = get_ui();

    fl = strlen(body) + strlen(rawHtml) + cssBuf.len + jsBuf.len + strlen(ui) + 1536;
    full = (char*)malloc(fl);
    if (!full) {
        free(md);
        free(body);
        free(rawHtml);
        free(cssBuf.data);
        free(jsBuf.data);
        return LISTPLUGIN_ERROR;
    }

    snprintf(full, fl,
        "<!DOCTYPE html><html%s><head>"
        "<meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">"
        "<meta charset=\"utf-8\"><style>%s</style></head><body%s>"
        "%s<div id=\"mdv-layout\"><div id=\"mdv-render-pane\"><div id=\"mdv-ct\">%s</div></div><pre id=\"mdv-raw-pane\">%s</pre></div>%s</body></html>",
        dark ? " class=\"mdv-dark\"" : " class=\"mdv-light\"", cssBuf.data, dark ? " class=\"mdv-dark dark\"" : " class=\"mdv-light\"", ui, body, rawHtml, jsBuf.data);

    d->darkTheme=dark;
    d->paletteRevision=g_paletteRevision;
    navigate_to_html(d->pBrowser, full);

    free(full);
    free(body);
    free(rawHtml);
    free(cssBuf.data);
    free(jsBuf.data);
    if (d->mdUtf8) free(d->mdUtf8);
    d->mdUtf8 = md;
    d->rawCharCount = utf8_char_count(md);
    if (d->currentFile) free(d->currentFile);
    d->currentFile = mdview_strdup(fileUtf8);

    return LISTPLUGIN_OK;
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
        }
        return 0;
    case WM_TIMER:
        if (d && wP == MDVIEW_SYNC_TIMER_ID && d->pBrowser) {
            handle_pending_link_command(d);
            DWORD now=GetTickCount();
            if (now-d->themeCheck>=1000) {
                d->themeCheck=now;
                refresh_color_theme(d);
            }
            return 0;
        }
        break;
    case WM_DESTROY:
        if(d){
            KillTimer(hwnd, MDVIEW_SYNC_TIMER_ID);
            /* Save settings to INI before closing */
            save_current_settings(d->pBrowser);
            if(d->hwndIEServer && d->origIEProc){
        mdview_set_window_ptr(d->hwndIEServer, GWLP_WNDPROC, (LONG_PTR)d->origIEProc);
                RemovePropW(d->hwndIEServer, L"MDViewData");
            }
            if (d->mdUtf8) { free(d->mdUtf8); d->mdUtf8 = NULL; }
            if (d->currentFile) { free(d->currentFile); d->currentFile = NULL; }
            if(d->pBrowser) IWebBrowser2_Release(d->pBrowser);
            if(d->pOleObj){ IOleObject_Close(d->pOleObj,OLECLOSE_NOSAVE); IOleObject_Release(d->pOleObj); }
    free(d); mdview_set_window_ptr(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wP,lP);
}

/* --- DLL Entry --- */

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

/* --- TC Lister Plugin Exports --- */

__declspec(dllexport) HWND __stdcall ListLoad(HWND pw, char* file, int flags) {
    ensure_ie11_emulation();
    load_settings();

    if(!g_classRegistered){
        WNDCLASSEXW wc={0}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=ContainerWndProc;
        wc.hInstance=g_hInstance; wc.lpszClassName=CLASS_NAME;
        RegisterClassExW(&wc); g_classRegistered=1;
    }

    char* md=read_file_utf8(file); if(!md)return NULL;
    char* body=md_to_html(md, file);
    char* rawHtml = NULL;
    if(!body){
        free(md);
        return NULL;
    }
    rawHtml = md_to_raw_html(md);
    if(!rawHtml){
        free(body);
        free(md);
        return NULL;
    }

    /* Determine theme: saved preference, or auto-detect */
    int dark = resolve_color_theme(flags);

    /* Build CSS and JS dynamically with current settings */
    StrBuf cssBuf; sb_init(&cssBuf); build_css(&cssBuf);
    StrBuf jsBuf;  sb_init(&jsBuf);  build_js(&jsBuf,VIEW_PREVIEW,g_settings.wordWrap);
    const char* ui = get_ui();

    size_t fl=strlen(body)+strlen(rawHtml)+cssBuf.len+jsBuf.len+strlen(ui)+1536;
    char* full=(char*)malloc(fl);
    if (!full) {
        free(body);
        free(rawHtml);
        free(cssBuf.data);
        free(jsBuf.data);
        free(md);
        return NULL;
    }
    snprintf(full,fl,
        "<!DOCTYPE html><html%s><head>"
        "<meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">"
        "<meta charset=\"utf-8\"><style>%s</style></head><body%s>"
        "%s<div id=\"mdv-layout\"><div id=\"mdv-render-pane\"><div id=\"mdv-ct\">%s</div></div><pre id=\"mdv-raw-pane\">%s</pre></div>%s</body></html>",
        dark?" class=\"mdv-dark\"":" class=\"mdv-light\"", cssBuf.data, dark?" class=\"mdv-dark dark\"":" class=\"mdv-light\"", ui, body, rawHtml, jsBuf.data);
    free(body); free(rawHtml); free(cssBuf.data); free(jsBuf.data);

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
    data->darkTheme=dark;
    data->listerFlags=flags;
    data->paletteRevision=g_paletteRevision;
    data->rawCharCount = utf8_char_count(md);
    data->mdUtf8 = md;
    data->currentFile = mdview_strdup(file);
    mdview_set_window_ptr(hwnd, GWLP_USERDATA, (LONG_PTR)data);

    SiteImpl* site=NULL;
    HRESULT hr=create_browser(hwnd,&data->pBrowser,&data->pOleObj,&site);
    if(FAILED(hr)){
        if (data->mdUtf8) free(data->mdUtf8);
        if (data->currentFile) free(data->currentFile);
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
    SetTimer(hwnd, MDVIEW_SYNC_TIMER_ID, MDVIEW_SYNC_TIMER_MS, NULL);

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

__declspec(dllexport) int __stdcall ListLoadNext(HWND pw, HWND lw, char* file, int flags) {
    MDViewData* d = (MDViewData*)mdview_get_window_ptr(lw, GWLP_USERDATA);
    (void)pw;
    int previousFlags, result;
    if (!d || !file) return LISTPLUGIN_ERROR;
    previousFlags=d->listerFlags;
    d->listerFlags=flags;
    result=load_file_into_existing_view(d,file);
    if (result!=LISTPLUGIN_OK) d->listerFlags=previousFlags;
    return result;
}

__declspec(dllexport) int __stdcall ListLoadNextW(HWND pw, HWND lw, WCHAR* file, int flags) {
    int len;
    char* u;
    int rc;
    if (!file) return LISTPLUGIN_ERROR;
    len = WideCharToMultiByte(CP_UTF8, 0, file, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return LISTPLUGIN_ERROR;
    u = (char*)malloc((size_t)len);
    if (!u) return LISTPLUGIN_ERROR;
    if (WideCharToMultiByte(CP_UTF8, 0, file, -1, u, len, NULL, NULL) <= 0) {
        free(u);
        return LISTPLUGIN_ERROR;
    }
    rc = ListLoadNext(pw, lw, u, flags);
    free(u);
    return rc;
}

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
__declspec(dllexport) int __stdcall ListSendCommand(HWND w, int c, int p) {
    MDViewData* d = (MDViewData*)mdview_get_window_ptr(w, GWLP_USERDATA);
    if (!d) return LISTPLUGIN_ERROR;

    switch (c) {
    case LC_COPY:
        do_copy_text(d);
        return LISTPLUGIN_OK;

    case LC_SELECT_ALL:
        do_select_all(d);
        return LISTPLUGIN_OK;

    case LC_SETPERCENT:
        if (d->pBrowser) {
            wchar_t js[600];
            if (p < 0) p = 0;
            if (p > 100) p = 100;
            _snwprintf_s(js, _countof(js), _TRUNCATE,
                L"(function(){var p=window.mdvScrollPane?mdvScrollPane():null,de=document.documentElement||document.body,b=document.body,sh,ch,y;"
                L"if(p){sh=p.scrollHeight;ch=p.clientHeight;y=((sh>ch)?((sh-ch)*%d/100):0);p.scrollTop=y;return;}"
                L"sh=Math.max((b&&b.scrollHeight)||0,(de&&de.scrollHeight)||0);"
                L"ch=Math.max((b&&b.clientHeight)||0,(de&&de.clientHeight)||0);"
                L"y=((sh>ch)?((sh-ch)*%d/100):0);window.scrollTo(0,y);})();",
                p, p);
            exec_js(d->pBrowser, js);
        }
        return LISTPLUGIN_OK;

    case LC_NEWPARAMS:
        d->listerFlags=p;
        if (d->pBrowser) refresh_color_theme(d);
        return LISTPLUGIN_OK;
    }

    return LISTPLUGIN_ERROR;
}

__declspec(dllexport) int __stdcall ListPrint(HWND w, char* fileToPrint, char* defPrinter, int printFlags, RECT* margins) {
    MDViewData* d = (MDViewData*)mdview_get_window_ptr(w, GWLP_USERDATA);
    (void)fileToPrint;
    (void)defPrinter;
    (void)printFlags;
    (void)margins;

    if (!d || !d->pBrowser) return LISTPLUGIN_ERROR;
    browser_execwb(d->pBrowser, OLECMDID_PRINT);
    return LISTPLUGIN_OK;
}

__declspec(dllexport) void __stdcall ListSetDefaultParams(ListDefaultParamStruct* p) {
    if (p) g_hasListerHost=1;
    if (p && p->DefaultIniName[0]) {
        strncpy_s(g_iniPath, MAX_PATH, p->DefaultIniName, _TRUNCATE);
    }
}
