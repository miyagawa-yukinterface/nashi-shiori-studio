// なしスタジオ - PNG を読む（依存なし）
//
// 書き出しは image.cpp、ほどくのは inflate.cpp です。ここは、その間をつなぎます。
// シェルの絵を出すのに要ります。
//
// 扱えるもの（PNG の決まり ISO/IEC 15948 のうち、絵として出てくるところ）
//   ・色の入れかた 0（灰）/ 2（RGB）/ 3（色見本）/ 4（灰＋透け）/ 6（RGBA）
//   ・1 色あたり 1, 2, 4, 8, 16 ビット（16 は 8 に落とします）
//   ・行ごとの下ごしらえ 0〜4（なし・左・上・平均・Paeth）
//   ・とびとびの並べかた（Adam7）
//   ・tRNS（灰・RGB・色見本の透け）
//
// 扱わないもの: 動く PNG（APNG は 1 枚目だけ）、gAMA などの色あわせ。
#include "image.h"

#include <cstring>
#include <vector>

#include "inflate.h"

namespace nashi {

namespace {

unsigned Be32(const unsigned char* p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

/** 1 色あたりのビット数と色の入れかたから、1 画素のバイト数（8bit に直したあと）。 */
int SamplesPerPixel(int colorType) {
    switch (colorType) {
        case 0: return 1;   // 灰
        case 2: return 3;   // RGB
        case 3: return 1;   // 色見本の番号
        case 4: return 2;   // 灰＋透け
        case 6: return 4;   // RGBA
    }
    return 0;
}

int PaethPredictor(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/**
 * 下ごしらえ（filter）をほどく。
 * raw は「1 バイトの種類 ＋ 1 行ぶん」がならんだもの。ほどいた行を out に書きます。
 */
bool Unfilter(const unsigned char* raw, size_t rawLen, int height, size_t rowBytes,
              int bytesPerPixel, std::vector<unsigned char>* out) {
    if (rawLen < (size_t)height * (rowBytes + 1)) return false;
    out->assign((size_t)height * rowBytes, 0);

    for (int y = 0; y < height; y++) {
        const unsigned char type = raw[(size_t)y * (rowBytes + 1)];
        const unsigned char* src = raw + (size_t)y * (rowBytes + 1) + 1;
        unsigned char* cur = &(*out)[(size_t)y * rowBytes];
        const unsigned char* up = y > 0 ? &(*out)[((size_t)y - 1) * rowBytes] : NULL;

        for (size_t x = 0; x < rowBytes; x++) {
            const int a = (x >= (size_t)bytesPerPixel) ? cur[x - bytesPerPixel] : 0;  // 左
            const int b = up ? up[x] : 0;                                             // 上
            const int c = (up && x >= (size_t)bytesPerPixel) ? up[x - bytesPerPixel] : 0; // 左上
            int v = src[x];
            switch (type) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += PaethPredictor(a, b, c); break;
                default: return false;   // 知らない下ごしらえ
            }
            cur[x] = (unsigned char)(v & 0xFF);
        }
    }
    return true;
}

/** ほどいた行から、1 画素ぶんの色（0〜255）を取り出す。 */
struct PixelReader {
    int bitDepth, colorType;
    const std::vector<unsigned char>* palette;    // 色見本（RGB×3）
    const std::vector<unsigned char>* paletteA;   // 色見本の透け
    bool hasTrns;
    unsigned short trns[3];                       // 灰 or RGB の「透ける色」

    /** row の x 画素目を、RGBA の 4 バイトにして dst に書く。 */
    void Read(const unsigned char* row, int x, unsigned char* dst) const {
        const int spp = SamplesPerPixel(colorType);
        unsigned short s[4] = { 0, 0, 0, 0 };

        if (bitDepth == 16) {
            const unsigned char* p = row + (size_t)x * spp * 2;
            for (int i = 0; i < spp; i++) s[i] = (unsigned short)((p[i * 2] << 8) | p[i * 2 + 1]);
        } else if (bitDepth == 8) {
            const unsigned char* p = row + (size_t)x * spp;
            for (int i = 0; i < spp; i++) s[i] = p[i];
        } else {
            // 1, 2, 4 ビット。1 バイトに何画素か詰まっています。
            const int per = 8 / bitDepth;
            const unsigned char byte = row[x / per];
            const int shift = 8 - bitDepth * (x % per + 1);
            s[0] = (unsigned short)((byte >> shift) & ((1 << bitDepth) - 1));
        }

        // 16 ビットは 8 ビットに落とす（上の 8 ビットを取ります）
        const int shiftTo8 = (bitDepth == 16) ? 8 : 0;
        // 1, 2, 4 ビットの灰色は、0〜255 にのばす（1bit なら 0/255）
        const int maxVal = (1 << bitDepth) - 1;

        unsigned char r = 0, g = 0, b = 0, a = 255;
        switch (colorType) {
            case 0: {   // 灰
                unsigned char v = (bitDepth >= 8)
                    ? (unsigned char)(s[0] >> shiftTo8)
                    : (unsigned char)(s[0] * 255 / maxVal);
                r = g = b = v;
                if (hasTrns && s[0] == trns[0]) a = 0;
                break;
            }
            case 2: {   // RGB
                r = (unsigned char)(s[0] >> shiftTo8);
                g = (unsigned char)(s[1] >> shiftTo8);
                b = (unsigned char)(s[2] >> shiftTo8);
                if (hasTrns && s[0] == trns[0] && s[1] == trns[1] && s[2] == trns[2]) a = 0;
                break;
            }
            case 3: {   // 色見本
                const size_t idx = s[0];
                if (palette && idx * 3 + 2 < palette->size()) {
                    r = (*palette)[idx * 3];
                    g = (*palette)[idx * 3 + 1];
                    b = (*palette)[idx * 3 + 2];
                }
                if (paletteA && idx < paletteA->size()) a = (*paletteA)[idx];
                break;
            }
            case 4: {   // 灰＋透け
                r = g = b = (unsigned char)(s[0] >> shiftTo8);
                a = (unsigned char)(s[1] >> shiftTo8);
                break;
            }
            case 6: {   // RGBA
                r = (unsigned char)(s[0] >> shiftTo8);
                g = (unsigned char)(s[1] >> shiftTo8);
                b = (unsigned char)(s[2] >> shiftTo8);
                a = (unsigned char)(s[3] >> shiftTo8);
                break;
            }
            default: break;
        }
        dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = a;
    }
};

// Adam7（とびとびに並べる書きかた）の刻み
const int kAdam7X0[7] = { 0, 4, 0, 2, 0, 1, 0 };
const int kAdam7Y0[7] = { 0, 0, 4, 0, 2, 0, 1 };
const int kAdam7DX[7] = { 8, 8, 4, 4, 2, 2, 1 };
const int kAdam7DY[7] = { 8, 8, 8, 4, 4, 2, 2 };

/** その段の幅・高さ。 */
void Adam7Size(int pass, int w, int h, int* pw, int* ph) {
    *pw = (w - kAdam7X0[pass] + kAdam7DX[pass] - 1) / kAdam7DX[pass];
    *ph = (h - kAdam7Y0[pass] + kAdam7DY[pass] - 1) / kAdam7DY[pass];
    if (*pw < 0) *pw = 0;
    if (*ph < 0) *ph = 0;
}

size_t RowBytesFor(int width, int bitDepth, int colorType) {
    const int bitsPerPixel = SamplesPerPixel(colorType) * bitDepth;
    return ((size_t)width * bitsPerPixel + 7) / 8;
}

} // namespace

bool DecodePng(const std::string& data, int* widthOut, int* heightOut,
               std::vector<unsigned char>* rgbaOut) {
    if (data.size() < 8) return false;
    const unsigned char* p = (const unsigned char*)data.data();
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (std::memcmp(p, sig, 8) != 0) return false;

    int width = 0, height = 0, bitDepth = 0, colorType = 0, interlace = 0;
    bool sawIhdr = false;
    std::string idat;
    std::vector<unsigned char> palette, paletteA;
    bool hasTrns = false;
    unsigned short trns[3] = { 0, 0, 0 };

    size_t pos = 8;
    while (pos + 8 <= data.size()) {
        const unsigned len = Be32(p + pos);
        if (len > 0x7FFFFFFFu || pos + 12 + len > data.size()) return false;
        const char* type = (const char*)p + pos + 4;
        const unsigned char* body = p + pos + 8;

        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len < 13) return false;
            width = (int)Be32(body);
            height = (int)Be32(body + 4);
            bitDepth = body[8];
            colorType = body[9];
            if (body[10] != 0) return false;          // 縮めかたは deflate だけ
            interlace = body[12];
            sawIhdr = true;

            if (width <= 0 || height <= 0 || width > 20000 || height > 20000) return false;
            if (interlace != 0 && interlace != 1) return false;
            if (SamplesPerPixel(colorType) == 0) return false;
            const bool depthOk =
                (colorType == 3) ? (bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8)
              : (colorType == 0) ? (bitDepth == 1 || bitDepth == 2 || bitDepth == 4
                                    || bitDepth == 8 || bitDepth == 16)
                                 : (bitDepth == 8 || bitDepth == 16);
            if (!depthOk) return false;
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.assign(body, body + len);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            if (colorType == 3) {
                paletteA.assign(body, body + len);
            } else if (colorType == 0 && len >= 2) {
                hasTrns = true;
                trns[0] = (unsigned short)((body[0] << 8) | body[1]);
                if (bitDepth < 16) trns[0] = (unsigned short)(trns[0] & ((1 << bitDepth) - 1));
            } else if (colorType == 2 && len >= 6) {
                hasTrns = true;
                for (int i = 0; i < 3; i++) {
                    trns[i] = (unsigned short)((body[i * 2] << 8) | body[i * 2 + 1]);
                }
            }
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.append((const char*)body, len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + len;   // 長さ(4) ＋ 名前(4) ＋ 中身 ＋ CRC(4)
    }

    if (!sawIhdr || idat.empty()) return false;
    if (colorType == 3 && palette.empty()) return false;

    // 16 ビットで縦横いっぱいでも収まるだけ見こむ
    const size_t maxRaw = (size_t)height * (RowBytesFor(width, bitDepth, colorType) + 1) + 64;
    std::string raw;
    if (!ZlibDecompress((const unsigned char*)idat.data(), idat.size(), &raw,
                        interlace ? maxRaw * 2 + 4096 : maxRaw)) {
        return false;
    }

    PixelReader pr;
    pr.bitDepth = bitDepth;
    pr.colorType = colorType;
    pr.palette = &palette;
    pr.paletteA = paletteA.empty() ? NULL : &paletteA;
    pr.hasTrns = hasTrns;
    pr.trns[0] = trns[0]; pr.trns[1] = trns[1]; pr.trns[2] = trns[2];

    std::vector<unsigned char> rgba((size_t)width * height * 4, 0);
    const int bpp = ((SamplesPerPixel(colorType) * bitDepth) + 7) / 8;   // 左どなりまでの距離

    if (interlace == 0) {
        const size_t rowBytes = RowBytesFor(width, bitDepth, colorType);
        std::vector<unsigned char> rows;
        if (!Unfilter((const unsigned char*)raw.data(), raw.size(), height, rowBytes, bpp, &rows)) {
            return false;
        }
        for (int y = 0; y < height; y++) {
            const unsigned char* row = &rows[(size_t)y * rowBytes];
            for (int x = 0; x < width; x++) {
                pr.Read(row, x, &rgba[((size_t)y * width + x) * 4]);
            }
        }
    } else {
        // Adam7。7 回に分けて、まばらに置いていきます。
        size_t used = 0;
        for (int pass = 0; pass < 7; pass++) {
            int pw = 0, ph = 0;
            Adam7Size(pass, width, height, &pw, &ph);
            if (pw == 0 || ph == 0) continue;

            const size_t rowBytes = RowBytesFor(pw, bitDepth, colorType);
            const size_t need = (size_t)ph * (rowBytes + 1);
            if (used + need > raw.size()) return false;

            std::vector<unsigned char> rows;
            if (!Unfilter((const unsigned char*)raw.data() + used, need, ph, rowBytes, bpp, &rows)) {
                return false;
            }
            used += need;

            for (int y = 0; y < ph; y++) {
                const unsigned char* row = &rows[(size_t)y * rowBytes];
                const int dy = kAdam7Y0[pass] + y * kAdam7DY[pass];
                for (int x = 0; x < pw; x++) {
                    const int dx = kAdam7X0[pass] + x * kAdam7DX[pass];
                    if (dx >= width || dy >= height) continue;
                    pr.Read(row, x, &rgba[((size_t)dy * width + dx) * 4]);
                }
            }
        }
    }

    if (widthOut) *widthOut = width;
    if (heightOut) *heightOut = height;
    if (rgbaOut) rgbaOut->swap(rgba);
    return true;
}

} // namespace nashi
