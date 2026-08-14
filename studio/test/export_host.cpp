// なしスタジオ - 書き出しの中身を見るためのコンソール
//
//   export_host.exe <project.json> [ファイル名の一部]
//
// プロジェクトを読んで、書き出されるファイルの一覧と中身を出します。
// 第 2 引数を付けると、名前にそれをふくむファイルの中身だけを出します。
// surfaces.txt の中身（当たり判定・SERIKO のアニメーション）を確かめるのに使います。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>

#include "exporter.h"
#include "json.h"
#include "util.h"
#include "fsutil.h"

using namespace nashi;

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) {
        printf("usage: export_host <project.json> [name-filter]\n");
        return 1;
    }

    std::string text;
    if (!ReadBinaryFile(argv[1], text)) {
        printf("読めません: %ls\n", argv[1]);
        return 2;
    }

    JValue project;
    std::string err;
    if (!JsonParse(text, project, err)) {
        printf("JSON が壊れています: %s\n", err.c_str());
        return 3;
    }

    std::string want;
    if (argc >= 3) want = WideToUtf8(argv[2]);

    std::string folder;
    // dll は中身を見ないので空でよい
    std::vector<OutFile> files = BuildGhostFiles(project, std::string(), true, &folder);
    printf("== folder: %s / %u ファイル\n", folder.c_str(), (unsigned)files.size());

    for (size_t i = 0; i < files.size(); i++) {
        const OutFile& f = files[i];
        if (!want.empty() && f.name.find(want) == std::string::npos) continue;
        printf("---- %s (%u バイト)\n", f.name.c_str(), (unsigned)f.data.size());
        if (want.empty()) continue;                 // 一覧だけのときは中身を出さない
        printf("%s\n", f.data.c_str());
    }
    return 0;
}
