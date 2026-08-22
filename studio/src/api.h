// なしスタジオ - /api/* と UI ファイルの配信
#pragma once

#include "webreq.h"
#include "json.h"
#include "sstp.h"

#include <string>

namespace nashi {

class Api {
public:
    void Init();
    void Handle(const HttpRequest& req, HttpResponse& res);

    const std::wstring& projectsDir() const { return projectsDir_; }
    const std::wstring& outputDir() const { return outputDir_; }

    // ウィンドウの大きさなどの記憶用
    JValue LoadConfig() const;
    void SaveConfig(const std::string& key, const JValue& value);

    // 書き出しに使う栞。ネイティブ版の画面にも渡します。
    std::string DllBytes() const;

private:
    void HandleApi(const HttpRequest& req, HttpResponse& res);
    std::wstring DefaultOutDir() const;
    SspInfo Ssp() const;

    std::wstring exeDir_;
    std::wstring projectsDir_;
    std::wstring outputDir_;
    std::wstring configPath_;

    // 短時間に何度も探しにいかないための控え
    mutable SspInfo sspCache_;
    mutable unsigned long sspCacheTick_ = 0;
};

} // namespace nashi
