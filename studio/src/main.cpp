// なしスタジオ - Windows ネイティブアプリ
//
// ・画面は Win32 と GDI だけで描きます（studio\src\w2k）
// ・栞 nashi.dll は exe に埋め込み済み。ネットワークは使いません
//   （SSP と話すときだけ 127.0.0.1:9801 へ接続します）
//
// ここがするのは、窓を出すまでの段どりだけです。
// 画面のことは w2k\window.cpp が受けもちます。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <string>

#include "assets.h"
#include "config.h"
#include "fsutil.h"
#include "util.h"
#include "w2k/window.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

using namespace nashi;

static const wchar_t* kMutexName = L"Local\\NashiStudioSingleInstance";

static Config g_config;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // 二重起動したときは、すでに開いている窓を前に出すだけ
    HANDLE mutex = CreateMutexW(NULL, TRUE, kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(w2k::EditorWindowClass(), NULL);
        if (other) {
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    // 開くファイルを、言われていれば受けとる
    std::wstring openPath;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; i++) {
                const std::wstring a = argv[i];
                if (!a.empty() && a[0] != L'-') { openPath = a; break; }
            }
            LocalFree(argv);
        }
    }

    g_config.Init();

    w2k::EditorOptions opt;
    opt.ghostPath = openPath;
    if (opt.ghostPath.empty()) {
        // 何も言われなければ、前に開いていたものを出します
        const std::wstring last = Utf8ToWide(g_config.Load()["lastFile"].asStr());
        if (!last.empty() && PathExists(last)) opt.ghostPath = last;
    }
    opt.icon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_NASHI_APP), IMAGE_ICON,
                                 0, 0, LR_DEFAULTSIZE);
    opt.iconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_NASHI_APP), IMAGE_ICON,
                                      16, 16, 0);

    // 前に覚えていた窓の場所
    const JValue cfg = g_config.Load();
    const JValue& win = cfg["window"];
    opt.x = win["x"].asInt(0);
    opt.y = win["y"].asInt(0);
    opt.w = win["w"].asInt(0);
    opt.h = win["h"].asInt(0);
    opt.maximized = win["max"].asBool(false);

    w2k::SetShioriDll(g_config.DllBytes());                    // 書き出しに使います
    w2k::SetSspHint(Utf8ToWide(cfg["sspPath"].asStr()));       // SSP を探す手がかり
    w2k::SetProjectsDir(g_config.projectsDir());               // 「名前をつけて保存」の置き場所

    w2k::EditorState state;
    const int code = w2k::RunEditor(hInstance, opt, &state);

    if (state.w > 0) {
        JValue out = JValue::makeObj();
        out.set("x", JValue::makeNum(state.x));
        out.set("y", JValue::makeNum(state.y));
        out.set("w", JValue::makeNum(state.w));
        out.set("h", JValue::makeNum(state.h));
        out.set("max", JValue::makeBool(state.maximized));
        g_config.Save("window", out);
    }
    if (!state.lastFile.empty()) {
        g_config.Save("lastFile", JValue::makeStr(WideToUtf8(state.lastFile)));
    }
    if (mutex) CloseHandle(mutex);
    return code;
}
