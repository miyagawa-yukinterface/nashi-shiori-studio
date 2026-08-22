// なしスタジオ - 置き場所と、覚えておく設定
//
// exe のとなりに作るフォルダ（projects / output）と、
// 覚えておくこと（窓の場所・前に開いていたもの・SSP のありか）を扱います。
// ファイルは exe のとなりの nashi-studio.json です。
#pragma once

#include "json.h"

#include <string>

namespace nashi {

class Config {
public:
    /** フォルダを用意して、初回なら見本のゴーストを置きます。 */
    void Init();

    const std::wstring& projectsDir() const { return projectsDir_; }
    const std::wstring& outputDir() const { return outputDir_; }

    /** 覚えてあることを、まるごと読みます。 */
    JValue Load() const;
    /** その 1 つだけを書きかえます。 */
    void Save(const std::string& key, const JValue& value);

    /** 書き出しに使う栞（exe に入れてあるもの）。 */
    std::string DllBytes() const;

private:
    std::wstring exeDir_;
    std::wstring projectsDir_;
    std::wstring outputDir_;
    std::wstring path_;
};

} // namespace nashi
