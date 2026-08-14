// なしスタジオ - SSP との連携（SSTP クライアントと SSP の検出）
#pragma once

#include <string>
#include <vector>

namespace nashi {

struct SstpResult {
    bool ok = false;
    int status = 0;          // 200 / 204 など
    std::string body;        // 応答本文（GetName など）
    std::string error;       // つながらなかったときの説明
};

// SSP が SSTP を受け付けているか（127.0.0.1:9801）
bool SstpAvailable();

// さくらスクリプトを、いま動いているゴーストにしゃべらせる
SstpResult SstpSend(const std::string& scriptUtf8, const std::string& sender);

// EXECUTE（GetName など）
SstpResult SstpExecute(const std::string& command, const std::string& sender);

// イベントを投げる（OnMouseDoubleClick などの動作確認用）
SstpResult SstpNotify(const std::string& eventName, const std::vector<std::string>& refs,
                      const std::string& sender);

struct SspInfo {
    bool running = false;
    bool sstp = false;
    std::wstring exePath;      // ssp.exe
    std::wstring ghostDir;     // <ssp>\ghost
    std::string ghostName;     // いま動いているゴースト（取れたら）
};

// hint には設定に覚えてある ssp.exe か SSP のフォルダを渡す
SspInfo DetectSsp(const std::wstring& hint);

} // namespace nashi
