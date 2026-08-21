// なしスタジオ（ネイティブ版）- 編集の窓
//
// WebView2 のかわりに、Win32 と GDI だけで作った編集画面です。
// 置き場所は layout.cpp、つなぎかたは drag.cpp、絵は paint.cpp が受けもちます。
// ここがするのは「窓を出して、マウスとキーを受けて、それらを呼ぶ」ことです。
//
// Windows 2000 でも動くように、新しい API は使いません
// （テーマも、二重描きの仕掛けも、ホイールの水平回しも使いません）。
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

namespace nashi {
namespace w2k {

/**
 * 編集の窓を出して、閉じられるまで動かします。
 * ghostPath が空でなければ、その ghost.json を読んでおきます。
 * 返すのは終了コード（0 なら何ごともなし）。
 */
int RunEditor(HINSTANCE hInstance, const std::wstring& ghostPath);

/**
 * 窓を出さずに、いまの画面をそのまま PNG にします（確かめ用）。
 * render_host と同じ考えで、画面まわりも目で見られるようにしておきます。
 */
bool RenderEditor(const std::wstring& ghostPath, int width, int height,
                  int scrollX, int scrollY, std::string* png);

/**
 * 窓を出さずに、マウスの動きまでまねてみる（確かめ用）。
 * 画面まわりは動かしてみないと分からないので、テストからここを呼びます。
 */
struct EditorProbe {
    std::wstring ghostPath;
    int width = 1100, height = 760;
    int scrollX = 0, scrollY = 0;
    bool drag = false;        // つまむところまでやるか
    int fromX = 0, fromY = 0; // 押した場所（窓の中の座標）
    // 左のブロック置き場からつまむときは、場所ではなく名前で言えます
    // （並びが変わっても、テストが壊れないように）。
    std::string grabPalette;
    int toX = 0, toY = 0;     // 動かした先
    bool release = false;     // はなすところまでやるか
};

/** png は途中の絵、json は ghost.json がどうなったか。要らないほうは NULL で。 */
bool ProbeEditor(const EditorProbe& probe, std::string* png, std::string* json);

/** 左のブロック置き場に、いま何がどこにならんでいるか（確かめ用）。 */
struct PaletteSpot {
    std::string key;
    int x, y, w, h;
};
bool EditorPaletteSpots(int width, int height, std::vector<PaletteSpot>* out);

} // namespace w2k
} // namespace nashi
