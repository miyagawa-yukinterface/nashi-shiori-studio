// なしスタジオ - exe に埋め込んだファイル（栞・見本のゴースト・アイコン）
#pragma once

#include <string>

#define IDI_NASHI_APP        101
#define IDR_NASHI_DLL        900
#define IDR_SAMPLE_PROJECT   901

namespace nashi {

// リソースをそのまま取り出す
bool LoadEmbedded(int id, std::string& out);

} // namespace nashi
