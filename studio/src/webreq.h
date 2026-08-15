// なしスタジオ - 画面からの要求（WebView2 が横取りしたリクエスト）
#pragma once

#include <string>
#include <map>

namespace nashi {

struct HttpRequest {
    std::string method;                          // GET / POST
    std::string path;                            // "/api/project"
    std::map<std::string, std::string> query;    // デコード済み
    std::map<std::string, std::string> headers;  // 小文字のキー
    std::string body;
    // この要求が、なしスタジオ自身の画面から出たものか
    // （外のページから叩かれていないか。webview.cpp が Referer / Origin を見て決めます）
    bool sameOrigin = false;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    bool cacheable = false;  // true なら画面側にキャッシュさせる（生成した画像など）
};

std::string UrlDecode(const std::string& s);

// "/api/project?name=%E3%81%82" を path と query に分ける
void ParseTarget(const std::string& target, HttpRequest& req);

const char* StatusText(int status);

} // namespace nashi
