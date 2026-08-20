// なしスタジオ - プレビューの中身を見るためのコンソール
//
//   preview_host.exe <ghost.json> <かたまりのID> [ID:参照0,参照1 ...]
//
// スタジオの「ためす」（studio/src/preview.cpp）を、画面を出さずに動かします。
// 中で動いているのは栞そのもの（shiori/src/interp.cpp）を 64bit で組んだものです。
//
// 出力の形は shiori/test/test_host.exe とそろえてあります。
// 一致テスト（shiori/test/parity）が、両方の出力を同じ読みかたで比べられるようにするためです。
// 変数は 1 回動かすごとに持ちこします（test_host が 1 プロセスで持ちこすのと同じ）。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#include "preview.h"
#include "json.h"
#include "util.h"

using namespace nashi;

static std::string WideToUtf8Arg(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 1) return std::string();
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, NULL, NULL);
    return s;
}

// "p01" か "p01:参照0,参照1" をほどく
static void SplitSpec(const std::string& spec, std::string& id, std::vector<std::string>& refs) {
    size_t c = spec.find(':');
    if (c == std::string::npos) { id = spec; return; }
    id = spec.substr(0, c);
    std::string rest = spec.substr(c + 1);
    size_t pos = 0;
    while (true) {
        size_t e = rest.find(',', pos);
        if (e == std::string::npos) { refs.push_back(rest.substr(pos)); break; }
        refs.push_back(rest.substr(pos, e - pos));
        pos = e + 1;
    }
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 3) {
        printf("usage: preview_host <ghost.json> <id | id:ref0,ref1 ...>\n");
        return 1;
    }

    std::string text;
    if (!ReadTextFile(argv[1], text)) {
        printf("cannot read %s\n", WideToUtf8Arg(argv[1]).c_str());
        return 2;
    }
    JValue project;
    std::string err;
    if (!JsonParse(text, project, err)) {
        printf("JSON parse error: %s\n", err.c_str());
        return 3;
    }

    // 変数は動かすたびに持ちこす（栞が 1 プロセスの中で持ちこすのと同じにする）
    JValue vars = JValue::makeObj();

    for (int i = 2; i < argc; i++) {
        std::string id;
        std::vector<std::string> refs;
        SplitSpec(WideToUtf8Arg(argv[i]), id, refs);

        PreviewRequest pr;
        pr.project = project;
        pr.scriptId = id;
        pr.refs = refs;
        pr.vars = vars;
        // 起動していない状態にそろえる。栞（test_host）も OnBoot が来るまでは
        // 回数が 0 で、ゴースト名もシェル名も空なので、そこに合わせます。
        pr.boots = 0;
        pr.ghostName.clear();
        pr.shellName.clear();

        PreviewResult r = RunPreview(pr);
        printf("---- %s\n", id.c_str());
        if (!r.ok) {
            printf("SHIORI/3.0 500 Internal Server Error\n");
            printf("Error: %s\n", r.error.c_str());
            continue;
        }
        vars = r.vars;
        if (r.script.empty()) {
            printf("SHIORI/3.0 204 No Content\n");
            continue;
        }
        printf("SHIORI/3.0 200 OK\n");
        printf("Value: %s\n", r.script.c_str());
        if (!r.commTo.empty()) printf("Reference0: %s\n", r.commTo.c_str());
    }
    return 0;
}
