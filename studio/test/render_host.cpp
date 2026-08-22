// なしスタジオ（ネイティブ版）- ブロックを描いて PNG に出す（画面は出しません）
//
//   render_host.exe <ghost.json> <かたまりのid> <out.png>
//   render_host.exe <ghost.json> --all <出す先のフォルダ>
//   render_host.exe <ghost.json> --window <out.png> [幅 高さ [たなの番号]]
//                                        編集の画面ぜんぶ（窓を出さずに）
//   render_host.exe <ghost.json> --normalize
//                                        読みこんだときの下ごしらえの結果
//   render_host.exe <ghost.json> --titles
//                                        かたまりの見出しをならべる
//   render_host.exe <ghost.json> --lint
//                                        チェックの結果
//   render_host.exe <ghost.json> --summary
//                                        かたまり・ブロックの言いあらわし
//   render_host.exe <ghost.json> --panel <たなの番号> [--q 言葉]
//                                        [--click 目じるし [打ちこむ中身]] [--dir 出す先]
//                                        右の作業だなを調べる／押してみる
//   render_host.exe <ghost.json> --fields
//                                        画面に出ている欄をぜんぶならべる
//   render_host.exe <ghost.json> --field <x> <y> [書きこむ中身]
//                                        その場所の欄を調べる／書きかえる
//   render_host.exe <ghost.json> --palette
//                                        置き場所に何がどこにあるか
//   render_host.exe <ghost.json> --drag <out.png> <x1> <y1> <x2> <y2>
//                                        [--drop] [--from <ブロックの名前>]
//                                        つまんで動かしてみる。--drop ではなす。
//                                        はなしたあとの ghost.json も出します。
//
// 窓を作らずに、記憶の中のビットマップへ GDI で描いて、PNG にして書き出します。
// おかげで、絵の出来を目で確かめられますし、テストからも動かせます
// （画面まわりは動かしてみないと分からないことが多いので、ここを作っておきます）。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

#include "w2k/blockdefs.h"
#include "w2k/layout.h"
#include "w2k/paint.h"
#include "w2k/lint.h"
#include "w2k/panel.h"
#include "w2k/window.h"
#include "image.h"
#include "json.h"
#include "util.h"

using namespace nashi;
using namespace nashi::w2k;

static std::string WideToUtf8Arg(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 1) return std::string();
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, NULL, NULL);
    return s;
}

/** 1 つのかたまりを描いて PNG のバイト列にする。 */
static bool RenderScript(const JValue& script, std::string& outPng, int* wOut, int* hOut) {
    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!dc) return false;

    PaintTools tools;
    if (!tools.Create()) { DeleteDC(dc); return false; }

    // ---- まず、置き場所を決める（文字は GDI で測る）
    GdiMeasurer tm(dc, tools.blockFont);
    Metrics m;
    Layout lay;
    LayoutScript(script, 0, 0, m, tm, &lay);

    const int pad = 12;
    const int w = lay.width + pad * 2;
    const int h = lay.height + pad * 2;
    if (w <= 0 || h <= 0 || w > 4000 || h > 8000) {
        tools.Free();
        DeleteDC(dc);
        return false;
    }

    // ---- 記憶の中の絵に描く（上から下へ並ぶ 32bit の面）
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;              // マイナスで「上が先」
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) { tools.Free(); DeleteDC(dc); return false; }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    PaintStyle style;
    style.gridStep = 24;
    RECT rc = { 0, 0, w, h };
    PaintBackground(dc, rc, style, pad, pad);
    PaintLayout(dc, lay, tools, style, -pad, -pad);

    GdiFlush();

    // ---- PNG にする（image.cpp は RGBA を待っているので、並びを直す）
    std::vector<unsigned char> rgba((size_t)w * h * 4);
    const unsigned char* src = (const unsigned char*)bits;
    for (int i = 0; i < w * h; i++) {
        rgba[i * 4 + 0] = src[i * 4 + 2];   // B G R X -> R
        rgba[i * 4 + 1] = src[i * 4 + 1];
        rgba[i * 4 + 2] = src[i * 4 + 0];
        rgba[i * 4 + 3] = 255;
    }
    outPng = EncodePng(w, h, rgba);
    if (wOut) *wOut = w;
    if (hOut) *hOut = h;

    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    tools.Free();
    DeleteDC(dc);
    return !outPng.empty();
}

static bool WriteBytes(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data.c_str(), (DWORD)data.size(), &written, NULL);
    CloseHandle(h);
    return ok != FALSE && written == data.size();
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 3) {
        printf("usage: render_host <ghost.json> <id|--all|--window|--drag|--palette> ...\n");
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

    // 読みこんだときの下ごしらえの結果を出す
    if (std::wstring(argv[2]) == L"--normalize") {
        NormalizeProject(project);
        printf("%s\n", project.dump(2).c_str());
        return 0;
    }

    // かたまりの見出しだけを出す（1 つずつ確かめるため）
    if (std::wstring(argv[2]) == L"--titles") {
        NormalizeProject(project);
        const JValue& scripts = project["scripts"];
        for (size_t i = 0; i < scripts.size(); i++) {
            printf("%s\n", ScriptTitle(scripts.at(i)).c_str());
        }
        return 0;
    }

    // チェックの結果を出す（JavaScript 版とくらべるため）
    if (std::wstring(argv[2]) == L"--lint") {
        NormalizeProject(project);
        std::vector<LintIssue> issues;
        LintProject(project, &issues);
        for (size_t i = 0; i < issues.size(); i++) {
            printf("%s\n%s\n%s\n",
                   issues[i].level == LintLevel::Error ? "error" : "warn",
                   issues[i].message.c_str(), issues[i].hint.c_str());
        }
        printf("---- %d 件（まちがい %d）\n", (int)issues.size(), CountLintErrors(issues));
        return 0;
    }

    // かたまりとブロックの言いあらわしを出す（画面の言葉と同じか、くらべるため）
    if (std::wstring(argv[2]) == L"--summary") {
        // 読みこんだときと同じ下ごしらえをしてから見ます（filter をほどくため）
        NormalizeProject(project);
        const JValue& scripts = project["scripts"];
        for (size_t i = 0; i < scripts.size(); i++) {
            const JValue& s = scripts.at(i);
            printf("title\t%s\n", ScriptTitle(s).c_str());
            std::vector<const JValue*> blocks;
            CollectBlocks(s, &blocks);
            for (size_t k = 0; k < blocks.size(); k++) {
                printf("block\t%s\n", BlockSummary(*blocks[k]).c_str());
            }
        }
        return 0;
    }

    // 右の作業だなを調べる／押してみる
    if (std::wstring(argv[2]) == L"--panel") {
        if (argc < 4) { printf("--panel <たなの番号> [--q 言葉] [--click 目じるし [中身]]\n"); return 1; }
        PanelProbe probe;
        probe.ghostPath = argv[1];
        probe.tab = _wtoi(argv[3]);
        for (int i = 4; i < argc; i++) {
            const std::wstring a = argv[i];
            if (a == L"--q" && i + 1 < argc) { probe.query = WideToUtf8Arg(argv[++i]); }
            else if (a == L"--dir" && i + 1 < argc) { probe.exportDir = WideToUtf8Arg(argv[++i]); }
            else if (a == L"--click" && i + 1 < argc) {
                probe.clickId = WideToUtf8Arg(argv[++i]);
                if (i + 1 < argc && std::wstring(argv[i + 1]).compare(0, 2, L"--") != 0) {
                    probe.type = true;
                    probe.typeValue = WideToUtf8Arg(argv[++i]);
                }
            }
        }
        std::string items, json;
        if (!ProbePanel(probe, &items, &json)) { printf("できませんでした\n"); return 5; }
        printf("%s", items.c_str());
        if (!probe.clickId.empty()) printf("---- ghost.json\n%s\n", json.c_str());
        return 0;
    }

    // 画面に出ている欄を、ぜんぶならべる
    if (std::wstring(argv[2]) == L"--fields") {
        std::vector<FieldSpot> spots;
        if (!EditorFieldSpots(argv[1], 1100, 760, &spots)) { printf("できませんでした\n"); return 5; }
        for (size_t i = 0; i < spots.size(); i++) {
            printf("%-28s %-8s %-10s (%4d,%4d) %4dx%-4d\n", spots[i].owner.c_str(),
                   spots[i].arg.c_str(), spots[i].kind.c_str(),
                   spots[i].x, spots[i].y, spots[i].w, spots[i].h);
        }
        return 0;
    }

    // 欄（打ちこみ・えらぶ）を調べる／書きかえる
    if (std::wstring(argv[2]) == L"--field") {
        if (argc < 5) { printf("--field <x> <y> [書きこむ中身]\n"); return 1; }
        FieldProbe probe;
        probe.ghostPath = argv[1];
        probe.x = _wtoi(argv[3]);
        probe.y = _wtoi(argv[4]);
        if (argc >= 6) {
            probe.set = true;
            probe.value = WideToUtf8Arg(argv[5]);
        }
        std::string info, json;
        if (!ProbeField(probe, &info, &json)) { printf("できませんでした\n"); return 5; }
        printf("%s", info.c_str());
        if (probe.set) printf("---- ghost.json\n%s\n", json.c_str());
        return 0;
    }

    // 左のブロック置き場に、何がどこにあるか
    if (std::wstring(argv[2]) == L"--palette") {
        std::vector<PaletteSpot> spots;
        if (!EditorPaletteSpots(1100, 760, &spots)) { printf("できませんでした\n"); return 5; }
        for (size_t i = 0; i < spots.size(); i++) {
            printf("%-16s (%4d,%4d) %4dx%-4d\n", spots[i].key.c_str(),
                   spots[i].x, spots[i].y, spots[i].w, spots[i].h);
        }
        return 0;
    }

    // つまんで動かしてみる（窓を出さずに、マウスの動きだけまねます）
    if (std::wstring(argv[2]) == L"--drag") {
        if (argc < 8) {
            printf("--drag <out.png> <x1> <y1> <x2> <y2> [--drop]\n");
            return 1;
        }
        EditorProbe probe;
        probe.ghostPath = argv[1];
        probe.drag = true;
        probe.fromX = _wtoi(argv[4]);
        probe.fromY = _wtoi(argv[5]);
        probe.toX = _wtoi(argv[6]);
        probe.toY = _wtoi(argv[7]);
        for (int i = 8; i < argc; i++) {
            if (std::wstring(argv[i]) == L"--drop") probe.release = true;
            else if (std::wstring(argv[i]) == L"--from" && i + 1 < argc) {
                probe.grabPalette = WideToUtf8Arg(argv[i + 1]);   // 置き場所から名前でつまむ
            }
        }
        std::string png, json;
        if (!ProbeEditor(probe, &png, &json)) { printf("できませんでした\n"); return 5; }
        if (!WriteBytes(argv[3], png)) { printf("書けませんでした\n"); return 6; }
        printf("%s\n", json.c_str());
        return 0;
    }

    // 編集の画面まるごと（左のブロック置き場もふくめて）
    if (std::wstring(argv[2]) == L"--window") {
        const int w = (argc >= 5) ? _wtoi(argv[4]) : 1100;
        const int h = (argc >= 6) ? _wtoi(argv[5]) : 760;
        const int tab = (argc >= 7) ? _wtoi(argv[6]) : -1;
        std::string png;
        if (!RenderEditor(argv[1], w, h, 0, 0, &png, tab)) {
            printf("画面を描けませんでした\n");
            return 5;
        }
        if (!WriteBytes(argv[3], png)) { printf("書けませんでした\n"); return 6; }
        printf("画面 %4d x %-4d  %6d バイト  %s\n", w, h, (int)png.size(),
               WideToUtf8Arg(argv[3]).c_str());
        return 0;
    }

    if (argc < 4) {
        printf("かたまりの id と、出す先が要ります\n");
        return 1;
    }
    const bool all = (std::wstring(argv[2]) == L"--all");
    const std::string want = all ? std::string() : WideToUtf8Arg(argv[2]);
    std::wstring dest = argv[3];

    const JValue& scripts = project["scripts"];
    int done = 0;
    for (size_t i = 0; i < scripts.size(); i++) {
        const JValue& s = scripts.at(i);
        std::string id = s["id"].asStr();
        if (!all && id != want) continue;

        std::string png;
        int w = 0, h = 0;
        if (!RenderScript(s, png, &w, &h)) {
            printf("描けませんでした: %s\n", id.c_str());
            continue;
        }
        std::wstring out = dest;
        if (all) {
            if (!out.empty() && out[out.size() - 1] != L'\\') out += L'\\';
            out += MbToWide(id, CP_UTF8) + L".png";
        }
        if (!WriteBytes(out, png)) {
            printf("書けませんでした: %s\n", WideToUtf8Arg(out.c_str()).c_str());
            continue;
        }
        printf("%-14s %4d x %-4d  %6d バイト  %s\n", id.c_str(), w, h,
               (int)png.size(), WideToUtf8Arg(out.c_str()).c_str());
        done++;
    }
    if (!done) { printf("描くものがありませんでした\n"); return 4; }
    return 0;
}
