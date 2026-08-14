// なしスタジオ - 依存なしの deflate 圧縮（固定ハフマン + LZ77）
#pragma once

#include <string>

namespace nashi {

// ZIP のメソッド 8 で使う生の deflate ストリーム
std::string DeflateRaw(const unsigned char* data, size_t len);
// PNG の IDAT で使う zlib ストリーム
std::string ZlibCompress(const unsigned char* data, size_t len);

unsigned Crc32(const unsigned char* data, size_t len);
unsigned Adler32(const unsigned char* data, size_t len);

} // namespace nashi
