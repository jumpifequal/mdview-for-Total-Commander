
#include <windows.h>
#include <commdlg.h>
#include <string>

typedef HWND (WINAPI *PFN_ListLoadW)(HWND, const WCHAR*, int);
typedef int  (WINAPI *PFN_ListGetDetectString)(char*, int);
typedef int  (WINAPI *PFN_ListCloseWindow)(HWND);

static std::wstring OpenFileDialog(const wchar_t* filter) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Select a md file";
    if (GetOpenFileNameW(&ofn))
        return file;
    return L"";
}

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_SIZE: {
            // Retrieve the first child window (the loaded WLX plugin)
            HWND child = GetWindow(hwnd, GW_CHILD);
            if (child && wParam != SIZE_MINIMIZED) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                MoveWindow(child, 0, 0, rc.right, rc.bottom, TRUE);
            }
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // Ask for WLX path
    wchar_t wlxPath[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = L"Lister Plugin (*.wlx)\0*.wlx\0\0";
    ofn.lpstrFile = wlxPath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Select mdview.wlx";
    if (!GetOpenFileNameW(&ofn)) return 0;

    // Load plugin
    HMODULE mod = LoadLibraryW(wlxPath);
    if (!mod) { MessageBoxW(NULL, L"Failed to load WLX", L"WLXHarness", MB_ICONERROR); return 0; }

    auto pListLoadW = (PFN_ListLoadW)GetProcAddress(mod, "ListLoadW");
    auto pListGetDetectString = (PFN_ListGetDetectString)GetProcAddress(mod, "ListGetDetectString");
    auto pListCloseWindow = (PFN_ListCloseWindow)GetProcAddress(mod, "ListCloseWindow");

    if (!pListLoadW || !pListGetDetectString || !pListCloseWindow) {
        MessageBoxW(NULL, L"Missing exports in WLX", L"WLXHarness", MB_ICONERROR);
        return 0;
    }

    // Create host parent window (like TC Lister)
    WNDCLASSW wc{};
    wc.lpfnWndProc = HostWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"WLXHarnessHost";
    RegisterClassW(&wc);

    HWND host = CreateWindowExW(0, wc.lpszClassName, L"WLX Harness Host",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
                                NULL, NULL, hInst, NULL);
    if (!host) return 0;

    // Choose a md file
    std::wstring md = OpenFileDialog(L"md Files (*.md)\0*.md\0All Files (*.*)\0*.*\0\0");
    if (md.empty()) return 0;

    // Call ListLoadW
    HWND child = pListLoadW(host, md.c_str(), 0);
    if (!child) {
        MessageBoxW(NULL, L"ListLoadW returned NULL.", L"WLXHarness", MB_ICONERROR);
        return 0;
    }

    // Resize child to fill host client area
    RECT rc; GetClientRect(host, &rc);
    SetWindowPos(child, NULL, 0, 0, rc.right-rc.left, rc.bottom-rc.top, SWP_SHOWWINDOW);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Close plugin window
    pListCloseWindow(child);
    FreeLibrary(mod);
    return 0;
}
