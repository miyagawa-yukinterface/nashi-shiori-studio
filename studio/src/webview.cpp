#include "webview.h"
#include "util.h"

#include <wrl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <WebView2.h>

#include <vector>

#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace nashi {

const wchar_t* kVirtualOrigin = L"https://nashi.example";

namespace {

std::string ReadStream(IStream* stream) {
    std::string out;
    if (!stream) return out;
    char buf[8192];
    ULONG read = 0;
    while (SUCCEEDED(stream->Read(buf, sizeof(buf), &read)) && read > 0) {
        out.append(buf, read);
        if (out.size() > (32u << 20)) break;
        read = 0;
    }
    return out;
}

// 外に出してよい URL か（http / https だけ）。
// ShellExecute は file:// や独自スキームも開いてしまうので、ここで絞ります。
bool IsWebUrl(const std::wstring& uri) {
    return _wcsnicmp(uri.c_str(), L"http://", 7) == 0 ||
           _wcsnicmp(uri.c_str(), L"https://", 8) == 0;
}

// 自分の画面（https://nashi.example/…）を指しているか
bool IsOwnOrigin(const std::wstring& uri) {
    std::wstring origin = kVirtualOrigin;
    if (_wcsnicmp(uri.c_str(), origin.c_str(), origin.size()) != 0) return false;
    wchar_t next = uri.size() > origin.size() ? uri[origin.size()] : L'/';
    return next == L'/' || next == L'\0';       // nashi.example.evil.com よけ
}

std::string TakeCoStr(LPWSTR s) {
    if (!s) return std::string();
    std::string out = WideToUtf8(s);
    CoTaskMemFree(s);
    return out;
}

} // namespace

bool WebView::Create(HWND parent,
                     const std::wstring& userDataDir,
                     ResourceFn resource,
                     std::function<void(bool, const std::wstring&)> onReady,
                     std::function<void(const std::wstring&)> onTitle) {
    resource_ = resource;
    onTitle_ = onTitle;

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataDir.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, parent, onReady](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    if (onReady) onReady(false, L"WebView2 を初期化できませんでした。");
                    return S_OK;
                }
                env->AddRef();
                environment_ = env;

                env->CreateCoreWebView2Controller(
                    parent,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, parent, onReady](HRESULT result2,
                                                ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result2) || !controller) {
                                if (onReady) onReady(false, L"WebView2 のウィンドウを作れませんでした。");
                                return S_OK;
                            }
                            controller->AddRef();
                            controller_ = controller;

                            ComPtr<ICoreWebView2> web;
                            controller->get_CoreWebView2(&web);
                            if (!web) {
                                if (onReady) onReady(false, L"WebView2 を取得できませんでした。");
                                return S_OK;
                            }
                            web->AddRef();
                            webview_ = web.Get();

                            // ブラウザっぽい要素を消して、アプリらしくする
                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(web->get_Settings(&settings)) && settings) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_IsZoomControlEnabled(FALSE);
                                settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                                settings->put_AreDevToolsEnabled(TRUE);
                            }

                            std::wstring filter = std::wstring(kVirtualOrigin) + L"/*";
                            web->AddWebResourceRequestedFilter(filter.c_str(),
                                                               COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);

                            EventRegistrationToken token;
                            web->add_WebResourceRequested(
                                Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                                    [this](ICoreWebView2*,
                                           ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                                        ComPtr<ICoreWebView2WebResourceRequest> request;
                                        if (FAILED(args->get_Request(&request)) || !request) return S_OK;

                                        LPWSTR raw = nullptr;
                                        request->get_Uri(&raw);
                                        std::string uri = TakeCoStr(raw);
                                        std::string origin = WideToUtf8(kVirtualOrigin);
                                        std::string target = uri.compare(0, origin.size(), origin) == 0
                                                                 ? uri.substr(origin.size())
                                                                 : uri;
                                        if (target.empty()) target = "/";

                                        HttpRequest req;
                                        ParseTarget(target, req);

                                        raw = nullptr;
                                        request->get_Method(&raw);
                                        req.method = TakeCoStr(raw);

                                        ComPtr<ICoreWebView2HttpRequestHeaders> headers;
                                        if (SUCCEEDED(request->get_Headers(&headers)) && headers) {
                                            BOOL has = FALSE;
                                            if (SUCCEEDED(headers->Contains(L"X-Nashi", &has)) && has) {
                                                LPWSTR v = nullptr;
                                                if (SUCCEEDED(headers->GetHeader(L"X-Nashi", &v))) {
                                                    req.headers["x-nashi"] = TakeCoStr(v);
                                                }
                                            }
                                            // どこのページから出た要求か。自分の画面からのものだけ通します
                                            // （画像の <img src> は独自ヘッダを付けられないので、ここで見ます）。
                                            const wchar_t* keys[] = { L"Referer", L"Origin" };
                                            for (int k = 0; k < 2 && !req.sameOrigin; k++) {
                                                has = FALSE;
                                                if (FAILED(headers->Contains(keys[k], &has)) || !has) continue;
                                                LPWSTR v = nullptr;
                                                if (FAILED(headers->GetHeader(keys[k], &v)) || !v) continue;
                                                std::wstring from = v;
                                                CoTaskMemFree(v);
                                                if (IsOwnOrigin(from)) req.sameOrigin = true;
                                            }
                                        }

                                        ComPtr<IStream> content;
                                        if (SUCCEEDED(request->get_Content(&content)) && content) {
                                            req.body = ReadStream(content.Get());
                                        }

                                        HttpResponse res;
                                        if (resource_) resource_(req, res);

                                        ComPtr<IStream> stream;
                                        stream.Attach(SHCreateMemStream(
                                            (const BYTE*)res.body.data(), (UINT)res.body.size()));
                                        std::wstring resHeaders =
                                            L"Content-Type: " + Utf8ToWide(res.contentType) +
                                            (res.cacheable ? L"\r\nCache-Control: max-age=86400"
                                                           : L"\r\nCache-Control: no-store");

                                        ComPtr<ICoreWebView2WebResourceResponse> response;
                                        ICoreWebView2Environment* env = (ICoreWebView2Environment*)environment_;
                                        if (env && SUCCEEDED(env->CreateWebResourceResponse(
                                                stream.Get(), res.status,
                                                Utf8ToWide(StatusText(res.status)).c_str(),
                                                resHeaders.c_str(), &response))) {
                                            args->put_Response(response.Get());
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);

                            web->add_DocumentTitleChanged(
                                Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                                    [this](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                                        LPWSTR title = nullptr;
                                        if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
                                            if (onTitle_) onTitle_(title);
                                            CoTaskMemFree(title);
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);

                            web->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this](ICoreWebView2*,
                                           ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR text = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&text)) && text) {
                                            if (onMessage) onMessage(text);
                                            CoTaskMemFree(text);
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);

                            // 外部リンクは既定のブラウザに任せる。
                            // ただし渡すのは http / https だけ。file:// や独自スキームを
                            // そのまま ShellExecute すると、プログラムを起動できてしまいます。
                            web->add_NewWindowRequested(
                                Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                    [](ICoreWebView2*,
                                       ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                        LPWSTR uri = nullptr;
                                        if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                                            if (IsWebUrl(uri)) {
                                                ShellExecuteW(NULL, L"open", uri, NULL, NULL,
                                                              SW_SHOWNORMAL);
                                            }
                                            CoTaskMemFree(uri);
                                        }
                                        args->put_Handled(TRUE);
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);

                            // 画面が外のページに変わらないようにする。
                            // 変わってしまうと、見た目はなしスタジオのまま中身が別物になります。
                            web->add_NavigationStarting(
                                Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [](ICoreWebView2*,
                                       ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                        LPWSTR raw = nullptr;
                                        if (FAILED(args->get_Uri(&raw)) || !raw) return S_OK;
                                        std::wstring uri = raw;
                                        CoTaskMemFree(raw);
                                        if (IsOwnOrigin(uri) || uri == L"about:blank") return S_OK;
                                        args->put_Cancel(TRUE);
                                        if (IsWebUrl(uri)) {          // 外のページはブラウザで
                                            ShellExecuteW(NULL, L"open", uri.c_str(), NULL, NULL,
                                                          SW_SHOWNORMAL);
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);

                            Resize(parent);
                            ready_ = true;

                            std::wstring url = std::wstring(kVirtualOrigin) + L"/index.html";
                            web->Navigate(url.c_str());
                            if (onReady) onReady(true, L"");
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());

    if (FAILED(hr)) {
        if (onReady) {
            onReady(false, hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
                               ? L"WebView2 ランタイムが見つかりませんでした。"
                               : L"WebView2 を開始できませんでした。");
        }
        return false;
    }
    return true;
}

void WebView::Resize(HWND parent) {
    if (!controller_) return;
    RECT rc;
    GetClientRect(parent, &rc);
    ((ICoreWebView2Controller*)controller_)->put_Bounds(rc);
}

void WebView::Focus() {
    if (!controller_) return;
    ((ICoreWebView2Controller*)controller_)
        ->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void WebView::ExecuteScript(const std::wstring& script, std::function<void(const std::wstring&)> done) {
    if (!webview_) {
        if (done) done(L"");
        return;
    }
    ((ICoreWebView2*)webview_)
        ->ExecuteScript(script.c_str(),
                        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                            [done](HRESULT, LPCWSTR result) -> HRESULT {
                                if (done) done(result ? result : L"");
                                return S_OK;
                            })
                            .Get());
}

void WebView::Shutdown() {
    ready_ = false;
    if (controller_) {
        ((ICoreWebView2Controller*)controller_)->Close();
        ((ICoreWebView2Controller*)controller_)->Release();
        controller_ = NULL;
    }
    if (webview_) {
        ((ICoreWebView2*)webview_)->Release();
        webview_ = NULL;
    }
    if (environment_) {
        ((ICoreWebView2Environment*)environment_)->Release();
        environment_ = NULL;
    }
}

} // namespace nashi
