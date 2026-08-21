// なしスタジオ - 依存なしの inflate（deflate をほどく）
#pragma once

#include <string>

#include "deflate.h"   // Adler32 / Crc32

namespace nashi {

// 生の deflate をほどきます。maxOut より大きくなりそうなら false
// （こわれた・わざと大きくした絵で、記憶をつかいきらないため）。
bool InflateRaw(const unsigned char* data, size_t len, std::string* out,
                size_t maxOut = 64u * 1024 * 1024);

// zlib（頭 2 バイト＋ deflate ＋ Adler32）をほどきます。PNG の IDAT はこれです。
bool ZlibDecompress(const unsigned char* data, size_t len, std::string* out,
                    size_t maxOut = 64u * 1024 * 1024);

} // namespace nashi
