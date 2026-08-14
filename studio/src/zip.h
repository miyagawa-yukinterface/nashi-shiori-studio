// なしスタジオ - 最小の ZIP 書き出し（.nar 用）
#pragma once

#include <string>
#include <vector>

namespace nashi {

struct ZipEntry {
    std::string name;   // "ghost/master/descript.txt" のように / 区切り
    std::string data;
};

std::string CreateZip(const std::vector<ZipEntry>& entries);

} // namespace nashi
