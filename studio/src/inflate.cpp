// なしスタジオ - 依存なしの inflate（deflate をほどく）
//
// 書きこむほう（DeflateRaw）は deflate.cpp にあります。ここは読むほうです。
// PNG を読むために要ります（IDAT は zlib で包まれた deflate です）。
//
// deflate には 3 とおりの入れかたがあって、ぜんぶ扱います。
//   0: そのまま（stored）
//   1: 決まりきったハフマン（fixed）
//   2: その場で作ったハフマン（dynamic）
//
// RFC 1950（zlib）と RFC 1951（deflate）のとおりです。
#include "inflate.h"

#include <cstring>
#include <vector>

namespace nashi {

namespace {

// 長さと距離の表（RFC 1951 の 3.2.5）
const unsigned short kLenBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
const unsigned char kLenExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
const unsigned short kDistBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
const unsigned char kDistExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
// 符号の長さそのものを並べる順（RFC 1951 の 3.2.7）
const unsigned char kClOrder[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/**
 * ハフマンの読み表。
 * 「長さ n の符号は、いくつあって、どの値から始まるか」だけを持ちます
 * （正準ハフマンなので、これだけで 1 ビットずつたどれます）。
 */
struct Huffman {
    int count[16];           // 長さごとの個数
    std::vector<int> symbol; // 短い符号から順にならんだ、符号の指す値

    /** 符号の長さのならびから作る。おかしければ false。 */
    bool Build(const unsigned char* lengths, int n) {
        std::memset(count, 0, sizeof(count));
        for (int i = 0; i < n; i++) {
            if (lengths[i] > 15) return false;
            count[lengths[i]]++;
        }
        count[0] = 0;   // 長さ 0 は「使わない」

        // すき間なく埋まっているか（余っていても足りなくてもいけない）
        int left = 1;
        for (int len = 1; len <= 15; len++) {
            left <<= 1;
            left -= count[len];
            if (left < 0) return false;   // 詰めこみすぎ
        }

        int offs[16];
        offs[0] = 0;
        offs[1] = 0;
        for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + count[len];
        symbol.assign((size_t)n, 0);
        for (int i = 0; i < n; i++) {
            if (lengths[i]) symbol[(size_t)offs[lengths[i]]++] = i;
        }
        return true;
    }
};

/** ビットを下の桁から取り出す読み手。 */
struct BitReader {
    const unsigned char* data;
    size_t len;
    size_t pos = 0;      // 次に読むバイト
    unsigned buf = 0;    // 溜めてあるビット
    int bits = 0;        // 溜めてある数
    bool bad = false;    // 足りなくなったら立てる

    BitReader(const unsigned char* d, size_t n) : data(d), len(n) {}

    unsigned Get(int need) {
        while (bits < need) {
            if (pos >= len) { bad = true; return 0; }
            buf |= (unsigned)data[pos++] << bits;
            bits += 8;
        }
        unsigned v = buf & ((1u << need) - 1);
        buf >>= need;
        bits -= need;
        return v;
    }

    /** 次のバイトの切れ目までとばす。 */
    void Align() { buf = 0; bits = 0; }

    /** ハフマンの符号を 1 つ読む。おかしければ -1。 */
    int Decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int length = 1; length <= 15; length++) {
            code |= (int)Get(1);
            if (bad) return -1;
            int cnt = h.count[length];
            if (code - first < cnt) return h.symbol[(size_t)(index + (code - first))];
            index += cnt;
            first = (first + cnt) << 1;
            code <<= 1;
        }
        return -1;
    }
};

/** 決まりきったハフマン（block type 1）の表。 */
void BuildFixed(Huffman* lit, Huffman* dist) {
    unsigned char l[288];
    for (int i = 0; i < 144; i++) l[i] = 8;
    for (int i = 144; i < 256; i++) l[i] = 9;
    for (int i = 256; i < 280; i++) l[i] = 7;
    for (int i = 280; i < 288; i++) l[i] = 8;
    lit->Build(l, 288);

    unsigned char d[30];
    for (int i = 0; i < 30; i++) d[i] = 5;
    dist->Build(d, 30);
}

/** その場で作ったハフマン（block type 2）の表を読む。 */
bool ReadDynamic(BitReader& br, Huffman* lit, Huffman* dist) {
    const int hlit = (int)br.Get(5) + 257;
    const int hdist = (int)br.Get(5) + 1;
    const int hclen = (int)br.Get(4) + 4;
    if (br.bad || hlit > 288 || hdist > 30) return false;

    unsigned char clLen[19];
    std::memset(clLen, 0, sizeof(clLen));
    for (int i = 0; i < hclen; i++) clLen[kClOrder[i]] = (unsigned char)br.Get(3);
    if (br.bad) return false;

    Huffman cl;
    if (!cl.Build(clLen, 19)) return false;

    // 符号の長さそのものが、またハフマンで縮めてあります
    unsigned char lengths[288 + 30];
    std::memset(lengths, 0, sizeof(lengths));
    int i = 0;
    while (i < hlit + hdist) {
        int sym = br.Decode(cl);
        if (sym < 0) return false;
        if (sym < 16) {
            lengths[i++] = (unsigned char)sym;
        } else if (sym == 16) {
            if (i == 0) return false;
            const unsigned char prev = lengths[i - 1];
            int rep = 3 + (int)br.Get(2);
            while (rep-- && i < hlit + hdist) lengths[i++] = prev;
        } else if (sym == 17) {
            int rep = 3 + (int)br.Get(3);
            while (rep-- && i < hlit + hdist) lengths[i++] = 0;
        } else {
            int rep = 11 + (int)br.Get(7);
            while (rep-- && i < hlit + hdist) lengths[i++] = 0;
        }
        if (br.bad) return false;
    }
    if (i != hlit + hdist) return false;

    if (!lit->Build(lengths, hlit)) return false;
    // 距離の表が 1 つだけで長さ 0、という書きかたも世の中にはあるので通します
    if (hdist == 1 && lengths[hlit] == 0) {
        unsigned char one[1] = { 1 };
        dist->Build(one, 1);
        return true;
    }
    return dist->Build(lengths + hlit, hdist);
}

} // namespace

bool InflateRaw(const unsigned char* data, size_t len, std::string* out, size_t maxOut) {
    if (!out) return false;
    out->clear();
    BitReader br(data, len);

    for (;;) {
        const int last = (int)br.Get(1);
        const int type = (int)br.Get(2);
        if (br.bad) return false;

        if (type == 0) {
            // そのまま入っている
            br.Align();
            if (br.pos + 4 > br.len) return false;
            const unsigned n = (unsigned)data[br.pos] | ((unsigned)data[br.pos + 1] << 8);
            const unsigned m = (unsigned)data[br.pos + 2] | ((unsigned)data[br.pos + 3] << 8);
            br.pos += 4;
            if ((n ^ 0xFFFFu) != m) return false;
            if (br.pos + n > br.len) return false;
            if (out->size() + n > maxOut) return false;
            out->append((const char*)data + br.pos, n);
            br.pos += n;
        } else if (type == 1 || type == 2) {
            Huffman lit, dist;
            if (type == 1) BuildFixed(&lit, &dist);
            else if (!ReadDynamic(br, &lit, &dist)) return false;

            for (;;) {
                const int sym = br.Decode(lit);
                if (sym < 0) return false;
                if (sym < 256) {
                    if (out->size() + 1 > maxOut) return false;
                    out->push_back((char)(unsigned char)sym);
                    continue;
                }
                if (sym == 256) break;          // このかたまりはここまで

                const int li = sym - 257;
                if (li >= 29) return false;
                const int length = kLenBase[li] + (int)br.Get(kLenExtra[li]);

                const int di = br.Decode(dist);
                if (di < 0 || di >= 30) return false;
                const int distance = kDistBase[di] + (int)br.Get(kDistExtra[di]);
                if (br.bad) return false;
                if ((size_t)distance > out->size()) return false;   // まだ無いところを指している

                if (out->size() + (size_t)length > maxOut) return false;
                // 1 バイトずつ写します。重なっていることがある（「ここから 3 バイトを
                // 10 回」のような書きかた）ので、まとめて写してはいけません。
                size_t from = out->size() - (size_t)distance;
                for (int k = 0; k < length; k++) out->push_back((*out)[from + (size_t)k]);
            }
        } else {
            return false;   // type 3 は決まっていない
        }

        if (last) break;
    }
    return true;
}

bool ZlibDecompress(const unsigned char* data, size_t len, std::string* out, size_t maxOut) {
    if (len < 6) return false;                       // 頭 2 ＋ おしり 4
    const unsigned cmf = data[0], flg = data[1];
    if ((cmf & 0x0F) != 8) return false;             // deflate 以外は知りません
    if (((cmf << 8) | flg) % 31 != 0) return false;  // 頭の check が合わない
    if (flg & 0x20) return false;                    // 辞書つきは扱いません

    if (!InflateRaw(data + 2, len - 2 - 4, out, maxOut)) return false;

    const unsigned char* tail = data + len - 4;
    const unsigned want = ((unsigned)tail[0] << 24) | ((unsigned)tail[1] << 16)
                        | ((unsigned)tail[2] << 8) | (unsigned)tail[3];
    return Adler32((const unsigned char*)out->c_str(), out->size()) == want;
}

} // namespace nashi
