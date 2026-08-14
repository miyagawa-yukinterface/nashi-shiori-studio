// なしスタジオ - WebView2 をウィンドウに埋め込む
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <functional>

#include "webreq.h"

namespace nashi {

// 画面はこの見せかけのアドレスから配信する（実体は exe の中）
extern const wchar_t* kVirtualOrigin;   // L"https://nashi.example"

class WebView {
public:
    typedef std::function<void(const HttpRequest&, HttpResponse&)> ResourceFn;

    // 失敗したら false（WebView2 ランタイムが無いときなど）
    bool Create(HWND parent,
                const std::wstring& userDataDir,
                ResourceFn resource,
                std::function<void(bool ok, const std::wstring& message)> onReady,
                std::function<void(const std::wstring& title)> onTitle);

    void Resize(HWND parent);
    void Focus();
    void ExecuteScript(const std::wstring& script, std::function<void(const std::wstring&)> done);
    void Shutdown();
    bool ready() const { return ready_; }

    // 画面から window.chrome.webview.postMessage() で送られてきた合図
    std::function<void(const std::wstring& message)> onMessage;

private:
    void* controller_ = NULL;   // ICoreWebView2Controller*
    void* webview_ = NULL;      // ICoreWebView2*
    void* environment_ = NULL;  // ICoreWebView2Environment*
    bool ready_ = false;
    ResourceFn resource_;
    std::function<void(const std::wstring&)> onTitle_;
};

} // namespace nashi
