// なしスタジオ（ネイティブ版）- ブロックの置き場所を、画面を出さずに見る
//
//   layout_host.exe <ghost.json> [かたまりの id]
//   layout_host.exe --defs                    ブロック定義の表を出す
//   layout_host.exe <ghost.json> --hit x y    その場所にあるものを言う
//
// layout.cpp には GDI が出てこないので、こうしてコンソールで確かめられます。
// 文字の幅は「1 文字ぶんいくつ」と決め打ちにして、どの環境でも同じ数が出るようにします
// （本物の画面では GDI で測ります）。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>

#include "w2k/blockdefs.h"
#include "w2k/layout.h"
#include "json.h"
#include "util.h"

using namespace nashi;
using namespace nashi::w2k;

// テスト用のものさし。半角 7px、全角 14px と決め打ちします。
struct FixedMeasurer : public TextMeasurer {
    int Width(const std::string& s) const {
        int w = 0;
        for (size_t i = 0; i < s.size();) {
            unsigned char ch = (unsigned char)s[i];
            int len = ch < 0x80 ? 1 : (ch < 0xE0 ? 2 : (ch < 0xF0 ? 3 : 4));
            w += (len == 1) ? 7 : 14;
            i += len;
        }
        return w;
    }
};

static std::string WideToUtf8Arg(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 1) return std::string();
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, NULL, NULL);
    return s;
}

static const char* KindName(PieceKind k) {
    switch (k) {
        case PieceKind::Block: return "ブロック";
        case PieceKind::Label: return "文字    ";
        case PieceKind::Slot:  return "欄      ";
        case PieceKind::Arm:   return "腕      ";
    }
    return "?";
}

static const char* ShapeName(Shape s) {
    switch (s) {
        case Shape::Hat: return "hat";
        case Shape::Stack: return "stack";
        case Shape::CBlock: return "c";
        case Shape::Cap: return "cap";
        case Shape::Reporter: return "reporter";
        case Shape::Boolean: return "boolean";
    }
    return "?";
}

static void DumpDefs() {
    int n = 0;
    const CategoryDef* cats = AllCategories(&n);
    printf("== カテゴリ %d ==\n", n);
    for (int i = 0; i < n; i++) printf("  %-10s %s %s\n", cats[i].id, cats[i].color, cats[i].name);

    const BlockDef* blocks = AllBlocks(&n);
    printf("\n== ブロック %d ==\n", n);
    for (int i = 0; i < n; i++) {
        const BlockDef& d = blocks[i];
        printf("  %-14s %-10s %-9s 引数%d ならび%d 腕%d%s\n",
               d.key, d.category, ShapeName(d.shape),
               d.argCount, d.partCount, d.subCount, d.hat ? " 帽子" : "");
    }
    int pc = 0;
    Palette(&pc);
    printf("\n== パレットに出す数: %d ==\n", pc);
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc >= 2 && std::wstring(argv[1]) == L"--defs") { DumpDefs(); return 0; }
    if (argc < 2) {
        printf("usage: layout_host <ghost.json> [id] | --defs | <ghost.json> --hit x y\n");
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

    FixedMeasurer tm;
    Metrics m;

    const JValue& scripts = project["scripts"];
    std::string want = (argc >= 3 && std::wstring(argv[2]) != L"--hit")
                       ? WideToUtf8Arg(argv[2]) : std::string();

    bool hit = false;
    int hx = 0, hy = 0;
    for (int i = 2; i + 2 < argc; i++) {
        if (std::wstring(argv[i]) == L"--hit") {
            hit = true;
            hx = _wtoi(argv[i + 1]);
            hy = _wtoi(argv[i + 2]);
        }
    }

    for (size_t i = 0; i < scripts.size(); i++) {
        const JValue& s = scripts.at(i);
        std::string id = s["id"].asStr();
        if (!want.empty() && id != want) continue;

        Layout lay;
        LayoutScript(s, 0, 0, m, tm, &lay);

        printf("---- %s  (%s)  %d x %d  かけら %d\n",
               id.c_str(), s["kind"].asStr("event").c_str(),
               lay.width, lay.height, (int)lay.pieces.size());

        if (hit) {
            int p = lay.HitTest(hx, hy);
            if (p < 0) { printf("     (%d,%d) には何もありません\n", hx, hy); continue; }
            const Piece& q = lay.pieces[p];
            int b = lay.BlockAt(p);
            printf("     (%d,%d) -> %s  %s%s%s   ブロック: %s\n", hx, hy, KindName(q.kind),
                   q.text.c_str(), q.argName.empty() ? "" : "欄=", q.argName.c_str(),
                   (b >= 0 && lay.pieces[b].def) ? lay.pieces[b].def->key : "-");
            continue;
        }

        for (size_t k = 0; k < lay.pieces.size(); k++) {
            const Piece& p = lay.pieces[k];
            printf("  %*s%s (%4d,%4d) %4dx%-4d %s%s%s\n",
                   p.depth * 2, "", KindName(p.kind), p.x, p.y, p.w, p.h,
                   p.def ? p.def->key : "",
                   p.argName.empty() ? "" : (" 欄=" + p.argName).c_str(),
                   p.text.empty() ? "" : (" 「" + p.text + "」").c_str());
        }
    }
    return 0;
}
