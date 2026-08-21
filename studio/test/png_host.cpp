// なしスタジオ - PNG の読み書きを、画面を出さずに確かめる
//
//   png_host.exe --info <a.png>          大きさと、色の中身を言う
//   png_host.exe --pixel <a.png> <x> <y> その画素の RGBA を言う
//   png_host.exe --hash <a.png>          ほどいた RGBA の checksum
//   png_host.exe --round <a.png>         読む → 書く → また読む、で同じか
//   png_host.exe --inflate <a.bin>       zlib をほどいて、長さと checksum を言う
//
// pngread.cpp と inflate.cpp には画面まわりが出てこないので、こうして確かめられます。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "image.h"
#include "inflate.h"

using namespace nashi;

static bool ReadAll(const wchar_t* path, std::string* out) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > (1 << 28)) { CloseHandle(h); return false; }
    out->resize((size_t)size.QuadPart);
    DWORD got = 0;
    BOOL ok = out->empty() ? TRUE : ReadFile(h, &(*out)[0], (DWORD)out->size(), &got, NULL);
    CloseHandle(h);
    return ok != FALSE && got == out->size();
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 3) {
        printf("usage: png_host --info|--pixel|--hash|--round|--inflate <file> [x y]\n");
        return 1;
    }
    const std::wstring mode = argv[1];

    std::string data;
    if (!ReadAll(argv[2], &data)) { printf("読めません\n"); return 2; }

    if (mode == L"--inflate") {
        std::string out;
        if (!ZlibDecompress((const unsigned char*)data.data(), data.size(), &out)) {
            printf("ほどけません\n");
            return 3;
        }
        printf("長さ %d  crc32 %08X\n", (int)out.size(),
               Crc32((const unsigned char*)out.data(), out.size()));
        return 0;
    }

    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
    if (!DecodePng(data, &w, &h, &rgba)) { printf("読めません\n"); return 3; }

    if (mode == L"--info") {
        // 中に何色あるか、透けている画素があるかも言う（見た目の当たりをつけるため）
        int opaque = 0, clear = 0;
        for (size_t i = 3; i < rgba.size(); i += 4) {
            if (rgba[i] == 255) opaque++;
            else if (rgba[i] == 0) clear++;
        }
        printf("%d x %d  画素 %d  すきとおり %d  なかば %d  crc32 %08X\n",
               w, h, w * h, clear, w * h - opaque - clear,
               Crc32(rgba.data(), rgba.size()));
        return 0;
    }

    if (mode == L"--hash") {
        printf("%08X\n", Crc32(rgba.data(), rgba.size()));
        return 0;
    }

    if (mode == L"--pixel") {
        if (argc < 5) { printf("--pixel <a.png> <x> <y>\n"); return 1; }
        const int x = _wtoi(argv[3]), y = _wtoi(argv[4]);
        if (x < 0 || y < 0 || x >= w || y >= h) { printf("外です\n"); return 4; }
        const unsigned char* q = &rgba[((size_t)y * w + x) * 4];
        printf("%d %d %d %d\n", q[0], q[1], q[2], q[3]);
        return 0;
    }

    if (mode == L"--round") {
        // 自分で書いたものを、自分で読みなおして、同じになるか
        const std::string again = EncodePng(w, h, rgba);
        int w2 = 0, h2 = 0;
        std::vector<unsigned char> rgba2;
        if (!DecodePng(again, &w2, &h2, &rgba2)) { printf("書いたものが読めません\n"); return 5; }
        const bool same = (w == w2 && h == h2 && rgba == rgba2);
        printf("%s  %dx%d -> %dx%d  %d バイト\n", same ? "おなじ" : "ちがう",
               w, h, w2, h2, (int)again.size());
        return same ? 0 : 6;
    }

    printf("知らないやりかたです\n");
    return 1;
}
