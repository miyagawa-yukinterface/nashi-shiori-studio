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
#include <io.h>
#include <fcntl.h>

#include "exporter.h"
#include "json.h"
#include "util.h"
#include "fsutil.h"

using namespace nashi;

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    // 書き出したものをそのまま出す（\n を \r\n に化けさせない）。
    // 答え合わせテストが中身をバイトで見くらべるので、ここは素通しにします。
    _setmode(_fileno(stdout), _O_BINARY);
    if (argc < 2) {
        printf("usage: export_host <project.json> [name-filter]\n");
        return 1;
    }

    // export_host --dau <フォルダ>
    //   そのフォルダから updates2.dau を作って見せる（0x01 は <1> と書く）
    if (std::wstring(argv[1]) == L"--dau") {
        if (argc < 3) { printf("usage: export_host --dau <folder>\n"); return 1; }
        std::string dau = BuildUpdatesDau(argv[2]);
        for (size_t i = 0; i < dau.size(); i++) {
            if (dau[i] == '\x01') printf("<1>");
            else if (dau[i] == '\r') printf("<CR>");
            else if (dau[i] == '\n') printf("<LF>\n");
            else putchar(dau[i]);
        }
        return 0;
    }

    // export_host <project.json> --write <フォルダ>
    //   書き出すファイルを、そのまま置く（PNG のように目で見たいとき用）
    if (argc >= 4 && std::wstring(argv[2]) == L"--write") {
        std::string src;
        if (!ReadBinaryFile(argv[1], src)) { printf("読めません: %ls\n", argv[1]); return 2; }
        JValue proj;
        std::string perr;
        if (!JsonParse(src, proj, perr)) { printf("JSON が壊れています: %s\n", perr.c_str()); return 3; }
        std::wstring dir = argv[3];
        std::vector<OutFile> out = BuildGhostFiles(proj, std::string(), true, NULL);
        for (size_t i = 0; i < out.size(); i++) {
            std::wstring rel = Utf8ToWide(out[i].name);
            for (size_t k = 0; k < rel.size(); k++) if (rel[k] == L'/') rel[k] = L'\\';
            std::wstring dest = PathJoin(dir, rel);
            EnsureDir(ParentDir(dest));
            if (!WriteBinaryFile(dest, out[i].data)) { printf("書けません: %s\n", out[i].name.c_str()); return 4; }
            printf("%s (%u バイト)\n", out[i].name.c_str(), (unsigned)out[i].data.size());
        }
        return 0;
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
