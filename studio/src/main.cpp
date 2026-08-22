// なしスタジオ - Windows ネイティブアプリ
//
// ・画面は Win32 と GDI だけで描きます（studio\src\w2k）。古い Windows でも動きます
// ・栞 nashi.dll は exe に埋め込み済み。ネットワークは使いません
// ・--webview を付けると、前の WebView2 版の画面が出ます（見くらべ用。いずれ外します）
//
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <cstdio>
#include <string>

#include "api.h"
#include "assets.h"
#include "fsutil.h"
#include "util.h"
#include "webview.h"
#include "w2k/window.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")

using namespace nashi;

static const wchar_t* kWindowClass = L"NashiStudioWindow";
static const wchar_t* kMutexName = L"Local\\NashiStudioSingleInstance";
static const UINT kSaveTimeoutTimer = 1;

static Api g_api;
static WebView g_web;
static HWND g_hwnd = NULL;
static bool g_closing = false;
static bool g_waitingSave = false;

// ------------------------------------------------------------ ウィンドウ位置

static void RestorePlacement(HWND hwnd) {
    JValue cfg = g_api.LoadConfig();
    const JValue& win = cfg["window"];

    int w = win["w"].asInt(1440);
    int h = win["h"].asInt(920);
    if (w < 960) w = 960;
    if (h < 620) h = 620;

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int maxW = work.right - work.left;
    int maxH = work.bottom - work.top;
    if (w > maxW) w = maxW;
    if (h > maxH) h = maxH;

    int x = win.has("x") ? win["x"].asInt(0) : work.left + (maxW - w) / 2;
    int y = win.has("y") ? win["y"].asInt(0) : work.top + (maxH - h) / 2;
    if (x < work.left - 40 || x > work.right - 80) x = work.left + (maxW - w) / 2;
    if (y < work.top - 10 || y > work.bottom - 80) y = work.top + (maxH - h) / 2;

    SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER);
    ShowWindow(hwnd, win["max"].asBool(false) ? SW_SHOWMAXIMIZED : SW_SHOW);
}

static void SavePlacement(HWND hwnd) {
    WINDOWPLACEMENT wp;
    wp.length = sizeof(wp);
    if (!GetWindowPlacement(hwnd, &wp)) return;
    const RECT& r = wp.rcNormalPosition;

    JValue win = JValue::makeObj();
    win.set("x", JValue::makeNum(r.left));
    win.set("y", JValue::makeNum(r.top));
    win.set("w", JValue::makeNum(r.right - r.left));
    win.set("h", JValue::makeNum(r.bottom - r.top));
    win.set("max", JValue::makeBool(wp.showCmd == SW_SHOWMAXIMIZED));
    g_api.SaveConfig("window", win);
}

// ---------------------------------------------------------------- 終了処理

static void FinishClose(HWND hwnd) {
    g_closing = true;
    g_waitingSave = false;
    KillTimer(hwnd, kSaveTimeoutTimer);
    DestroyWindow(hwnd);
}

static void AskThenClose(HWND hwnd) {
    if (g_closing || !g_web.ready()) { FinishClose(hwnd); return; }

    g_web.ExecuteScript(
        L"(function(){try{return NASHI.Model.dirty?1:0}catch(e){return 0}})()",
        [hwnd](const std::wstring& result) {
            bool dirty = result.find(L'1') != std::wstring::npos;
            if (!dirty) { FinishClose(hwnd); return; }

            int answer = MessageBoxW(hwnd,
                                     L"保存していない変更があります。\n保存して終了しますか？",
                                     L"なしスタジオ",
                                     MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
            if (answer == IDCANCEL) return;
            if (answer == IDNO) { FinishClose(hwnd); return; }

            g_waitingSave = true;
            SetTimer(hwnd, kSaveTimeoutTimer, 5000, NULL);   // 保存が終わらなくても閉じる
            g_web.ExecuteScript(L"NASHI.App.requestSaveAndClose()", NULL);
        });
}

// ------------------------------------------------------------------ ウィンドウ

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            g_web.Resize(hwnd);
            return 0;

        case WM_SETFOCUS:
            g_web.Focus();
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = 960;
            mm->ptMinTrackSize.y = 620;
            return 0;
        }

        case WM_TIMER:
            if (wp == kSaveTimeoutTimer && g_waitingSave) FinishClose(hwnd);
            return 0;

        case WM_CLOSE:
            AskThenClose(hwnd);
            return 0;

        case WM_DESTROY:
            SavePlacement(hwnd);
            g_web.Shutdown();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------ ネイティブ版の画面

/** ネイティブ版（Win32 と GDI だけの画面）を出します。 */
static int RunNativeEditor(HINSTANCE hInstance, const std::wstring& ghostPath) {
    // 二重起動したときは、すでに開いているウィンドウを前に出すだけ
    HANDLE mutex = CreateMutexW(NULL, TRUE, kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(w2k::EditorWindowClass(), NULL);
        if (other) {
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    g_api.Init();
    w2k::SetShioriDll(g_api.DllBytes());   // 書き出しに使います

    w2k::EditorOptions opt;
    opt.ghostPath = ghostPath;
    if (opt.ghostPath.empty()) {
        // 何も言われなければ、前に開いていたものを出します
        const std::string last = g_api.LoadConfig()["lastProject"].asStr();
        if (!last.empty()) {
            const std::wstring file =
                PathJoin(g_api.projectsDir(), Utf8ToWide(last) + L".json");
            if (PathExists(file)) opt.ghostPath = file;
        }
    }
    opt.icon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_NASHI_APP), IMAGE_ICON,
                                 0, 0, LR_DEFAULTSIZE);
    opt.iconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_NASHI_APP), IMAGE_ICON,
                                      16, 16, 0);

    // 前に覚えていた窓の場所
    const JValue win = g_api.LoadConfig()["window"];
    opt.x = win["x"].asInt(0);
    opt.y = win["y"].asInt(0);
    opt.w = win["w"].asInt(0);
    opt.h = win["h"].asInt(0);
    opt.maximized = win["max"].asBool(false);

    w2k::EditorState state;
    const int code = w2k::RunEditor(hInstance, opt, &state);

    if (state.w > 0) {
        JValue out = JValue::makeObj();
        out.set("x", JValue::makeNum(state.x));
        out.set("y", JValue::makeNum(state.y));
        out.set("w", JValue::makeNum(state.w));
        out.set("h", JValue::makeNum(state.h));
        out.set("max", JValue::makeBool(state.maximized));
        g_api.SaveConfig("window", out);
    }
    if (mutex) CloseHandle(mutex);
    return code;
}

// -------------------------------------------------------------------- main

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // ふだんはネイティブ版の画面を出します。
    // --webview を付けたときだけ、前の WebView2 版が出ます（見くらべ用）。
    bool useWebView = false;
    std::wstring openPath;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; i++) {
                const std::wstring a = argv[i];
                if (a == L"--webview") useWebView = true;
                else if (a == L"--w2k") { /* 前の名まえ。いまは既定なので、何もしません */ }
                else if (!a.empty() && a[0] != L'-') openPath = a;
            }
            LocalFree(argv);
        }
    }

    if (!useWebView) return RunNativeEditor(hInstance, openPath);

    // 二重起動したときは、すでに開いているウィンドウを前に出すだけ
    HANDLE mutex = CreateMutexW(NULL, TRUE, kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(kWindowClass, NULL);
        if (other) {
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    g_api.Init();

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(233, 238, 244));
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_NASHI_APP), IMAGE_ICON,
                                 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_NASHI_APP), IMAGE_ICON,
                                   16, 16, 0);
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(0, kWindowClass, L"なしスタジオ", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1440, 920,
                             NULL, NULL, hInstance, NULL);
    if (!g_hwnd) {
        MessageBoxW(NULL, L"ウィンドウを作れませんでした。", L"なしスタジオ", MB_ICONERROR);
        return 1;
    }
    RestorePlacement(g_hwnd);

    g_web.onMessage = [](const std::wstring& message) {
        if (message == L"saved" && g_waitingSave) FinishClose(g_hwnd);
    };

    bool started = g_web.Create(
        g_hwnd,
        PathJoin(ExeDir(), L"webview-data"),
        [](const HttpRequest& req, HttpResponse& res) { g_api.Handle(req, res); },
        [](bool ok, const std::wstring& message) {
            if (ok) {
                g_web.Focus();
                return;
            }
            std::wstring text = message +
                L"\n\nこのアプリは Microsoft Edge WebView2 ランタイムを使います。\n"
                L"「OK」を押すと配布ページを開きます。";
            if (MessageBoxW(g_hwnd, text.c_str(), L"なしスタジオ", MB_OKCANCEL | MB_ICONERROR) == IDOK) {
                ShellExecuteW(NULL, L"open",
                              L"https://developer.microsoft.com/microsoft-edge/webview2/",
                              NULL, NULL, SW_SHOWNORMAL);
            }
            g_closing = true;
            DestroyWindow(g_hwnd);
        },
        [](const std::wstring& title) {
            std::wstring t = title.empty() ? L"なしスタジオ" : title;
            SetWindowTextW(g_hwnd, t.c_str());
        });

    if (!started) {
        // Create の中でメッセージは出している
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_web.Shutdown();
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
