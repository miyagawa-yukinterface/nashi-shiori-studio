#include "deflate.h"

#include <vector>
#include <cstring>

namespace nashi {

// ------------------------------------------------------------------ checksums

static unsigned g_crcTable[256];
static bool g_crcReady = false;

static void InitCrc() {
    if (g_crcReady) return;
    for (unsigned n = 0; n < 256; n++) {
        unsigned c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
        g_crcTable[n] = c;
    }
    g_crcReady = true;
}

unsigned Crc32(const unsigned char* data, size_t len) {
    InitCrc();
    unsigned c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = g_crcTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

unsigned Adler32(const unsigned char* data, size_t len) {
    unsigned a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

// -------------------------------------------------------------- bit writer

namespace {

struct BitWriter {
    std::string out;
    unsigned bitBuf = 0;
    int bitCount = 0;

    // 通常のビット（LSB 側から詰める）
    void bits(unsigned value, int count) {
        bitBuf |= (value & ((1u << count) - 1)) << bitCount;
        bitCount += count;
        while (bitCount >= 8) {
            out += (char)(unsigned char)(bitBuf & 0xFF);
            bitBuf >>= 8;
            bitCount -= 8;
        }
    }
    // ハフマン符号（MSB 側から詰める決まり）
    void huff(unsigned code, int count) {
        for (int i = count - 1; i >= 0; i--) bits((code >> i) & 1, 1);
    }
    void flush() {
        if (bitCount > 0) {
            out += (char)(unsigned char)(bitBuf & 0xFF);
            bitBuf = 0;
            bitCount = 0;
        }
    }
};

// RFC1951 の固定ハフマン
inline void EmitLiteral(BitWriter& w, unsigned char c) {
    if (c < 144) w.huff(0x30 + c, 8);
    else w.huff(0x190 + (c - 144), 9);
}

inline void EmitCode(BitWriter& w, int code) {   // 256..287
    if (code < 280) w.huff(code - 256, 7);
    else w.huff(0xC0 + (code - 280), 8);
}

const struct { int extra; int base; } kLen[29] = {
    {0,3},{0,4},{0,5},{0,6},{0,7},{0,8},{0,9},{0,10},
    {1,11},{1,13},{1,15},{1,17},{2,19},{2,23},{2,27},{2,31},
    {3,35},{3,43},{3,51},{3,59},{4,67},{4,83},{4,99},{4,115},
    {5,131},{5,163},{5,195},{5,227},{0,258}
};

const struct { int extra; int base; } kDist[30] = {
    {0,1},{0,2},{0,3},{0,4},{1,5},{1,7},{2,9},{2,13},
    {3,17},{3,25},{4,33},{4,49},{5,65},{5,97},{6,129},{6,193},
    {7,257},{7,385},{8,513},{8,769},{9,1025},{9,1537},{10,2049},{10,3073},
    {11,4097},{11,6145},{12,8193},{12,12289},{13,16385},{13,24577}
};

inline void EmitLength(BitWriter& w, int length) {
    int i = 28;
    while (i > 0 && kLen[i].base > length) i--;
    EmitCode(w, 257 + i);
    if (kLen[i].extra) w.bits((unsigned)(length - kLen[i].base), kLen[i].extra);
}

inline void EmitDistance(BitWriter& w, int dist) {
    int i = 29;
    while (i > 0 && kDist[i].base > dist) i--;
    w.huff((unsigned)i, 5);
    if (kDist[i].extra) w.bits((unsigned)(dist - kDist[i].base), kDist[i].extra);
}

const int kWindow = 32768;
const int kMinMatch = 3;
const int kMaxMatch = 258;
const int kHashBits = 15;
const int kHashSize = 1 << kHashBits;
const int kMaxChain = 96;

inline unsigned HashAt(const unsigned char* p) {
    return (unsigned)(((p[0] << 10) ^ (p[1] << 5) ^ p[2]) & (kHashSize - 1));
}

} // namespace

// --------------------------------------------------------------- deflate

std::string DeflateRaw(const unsigned char* data, size_t len) {
    BitWriter w;
    w.bits(1, 1);   // final block
    w.bits(1, 2);   // 固定ハフマン

    if (len == 0) {
        EmitCode(w, 256);
        w.flush();
        return w.out;
    }

    std::vector<int> head((size_t)kHashSize, -1);
    std::vector<int> prev(len, -1);

    size_t pos = 0;
    while (pos < len) {
        int bestLen = 0, bestDist = 0;
        if (pos + kMinMatch <= len) {
            unsigned h = HashAt(data + pos);
            int cand = head[h];
            int chain = kMaxChain;
            int limit = (int)pos - kWindow;
            while (cand >= 0 && cand > limit && chain-- > 0) {
                const unsigned char* a = data + cand;
                const unsigned char* b = data + pos;
                size_t maxLen = len - pos;
                if (maxLen > (size_t)kMaxMatch) maxLen = kMaxMatch;
                if (bestLen < (int)maxLen && a[bestLen] == b[bestLen]) {
                    size_t n = 0;
                    while (n < maxLen && a[n] == b[n]) n++;
                    if ((int)n > bestLen) {
                        bestLen = (int)n;
                        bestDist = (int)(pos - (size_t)cand);
                        if (bestLen >= kMaxMatch) break;
                    }
                }
                cand = prev[(size_t)cand];
            }
            prev[pos] = head[h];
            head[h] = (int)pos;
        }

        if (bestLen >= kMinMatch) {
            EmitLength(w, bestLen);
            EmitDistance(w, bestDist);
            // 一致した分もハッシュに登録しておく（次の一致が見つかりやすくなる）
            for (int i = 1; i < bestLen; i++) {
                size_t p = pos + (size_t)i;
                if (p + kMinMatch <= len) {
                    unsigned h2 = HashAt(data + p);
                    prev[p] = head[h2];
                    head[h2] = (int)p;
                }
            }
            pos += (size_t)bestLen;
        } else {
            EmitLiteral(w, data[pos]);
            pos++;
        }
    }

    EmitCode(w, 256);
    w.flush();
    return w.out;
}

std::string ZlibCompress(const unsigned char* data, size_t len) {
    std::string out;
    out += (char)0x78;      // CM=8, CINFO=7 (32K window)
    out += (char)0x9C;      // FLEVEL=2, FCHECK が合うように
    out += DeflateRaw(data, len);
    unsigned adler = Adler32(data, len);
    out += (char)(unsigned char)((adler >> 24) & 0xFF);
    out += (char)(unsigned char)((adler >> 16) & 0xFF);
    out += (char)(unsigned char)((adler >> 8) & 0xFF);
    out += (char)(unsigned char)(adler & 0xFF);
    return out;
}

} // namespace nashi
