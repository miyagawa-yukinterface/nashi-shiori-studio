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
                  int scrollX, int scrollY, std::string* png, int tab = -1);

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
    int tab = -1;             // 0 以上なら、右の作業だなをそのたなにします
};

/** png は途中の絵、json は ghost.json がどうなったか。要らないほうは NULL で。 */
bool ProbeEditor(const EditorProbe& probe, std::string* png, std::string* json);

/**
 * 欄（打ちこみ・えらぶ）をさわってみる（確かめ用）。
 * (x, y) にある欄を調べて、set が立っていれば value を書きこみます。
 */
struct FieldProbe {
    std::wstring ghostPath;
    int width = 1100, height = 760;
    int x = 0, y = 0;
    bool set = false;
    std::string value;
};
bool ProbeField(const FieldProbe& probe, std::string* info, std::string* json);

/**
 * 右の作業だなを調べる（確かめ用）。
 * tab は 0=ためす 1=ゴースト 2=変数 3=さがす 4=チェック 5=書き出し 6=ヘルプ。
 * clickId を渡すと、その目じるしの部品を押してみます。
 */
struct PanelProbe {
    std::wstring ghostPath;
    int width = 1200, height = 760;
    int tab = 2;
    std::string query;        // 「さがす言葉」
    std::string clickId;      // 押す部品（"var.add" など）
    std::string typeValue;    // 打ちこむ欄なら、この中身にします
    bool type = false;
};

/** items は部品のならび、json はそのあとの ghost.json。要らないほうは NULL で。 */
bool ProbePanel(const PanelProbe& probe, std::string* items, std::string* json);

/**
 * 画面に出ている欄を、ぜんぶならべます（確かめ用）。
 * テストが「文字の幅で動くよこの位置」を決めうちにしないで済むようにするためです。
 */
struct FieldSpot {
    std::string owner;     // どのブロックの（scripts[0].blocks[1] のような形）
    std::string arg;       // どの欄か
    std::string kind;      // input / dropdown / eventname / areaname / funcname / varname
    int x, y, w, h;        // 画面の中の場所（左のブロック置き場のぶんも入っています）
};
bool EditorFieldSpots(const std::wstring& ghostPath, int width, int height,
                      std::vector<FieldSpot>* out);

/** 左のブロック置き場に、いま何がどこにならんでいるか（確かめ用）。 */
struct PaletteSpot {
    std::string key;
    int x, y, w, h;
};
bool EditorPaletteSpots(int width, int height, std::vector<PaletteSpot>* out);

} // namespace w2k
} // namespace nashi
