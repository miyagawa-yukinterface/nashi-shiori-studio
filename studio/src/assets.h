// なしスタジオ - exe に埋め込んだファイル（UI 一式・栞・サンプル）
#pragma once

#include <string>

#define IDI_NASHI_APP        101
#define IDR_NASHI_DLL        900
#define IDR_SAMPLE_PROJECT   901
#define IDR_WEB_ASSET_BASE  1000

namespace nashi {

// リソースをそのまま取り出す
bool LoadEmbedded(int id, std::string& out);

// "/index.html" のようなパスから UI ファイルを取り出す
bool FindWebAsset(const std::string& path, std::string& out, std::string& mime);

} // namespace nashi
