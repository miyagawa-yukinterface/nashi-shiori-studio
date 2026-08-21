// なしスタジオ（ネイティブ版）- ブロックの置き場所を決める
//
// **ここには GDI が出てきません。** 幅と高さを計算して、どこに何を置くかを
// 決めるだけです。絵を描くのは paint.cpp、押されたところを探すのもここです。
//
// 分けてある理由：画面を出さずにテストできるようにするためです。
// 文字の幅だけは環境で変わるので、外から測る関数を渡してもらいます。
//
// もとの見た目（ui/style.css）に近い形をねらっていますが、GDI には
// なめらかな角丸や影がないので、そこは素朴になります。
#pragma once

#include "blockdefs.h"
#include "../../../shiori/src/json.h"

#include <string>
#include <vector>

namespace nashi {
namespace w2k {

// ---------------------------------------------------------------- 寸法の決め
struct Metrics {
    int textHeight    = 17;   // 1 行の文字の高さ
    int padX          = 10;   // ブロックの左右の余白
    int padY          = 6;    // ブロックの上下の余白
    int gap           = 6;    // ならびのあいだ
    int minHeight     = 30;   // ブロックの最低の高さ
    int notchX        = 14;   // 上の切り欠きの左端
    int notchW        = 18;   // 切り欠きの幅
    int notchH        = 4;    // 切り欠きの深さ
    int corner        = 5;    // 角の丸み
    int hatHeight     = 16;   // 帽子の出っぱり
    int armIndent     = 14;   // C ブロックの中身の下げ幅
    int armMinHeight  = 24;   // 中身が空のときの腕の高さ
    int armFooter     = 12;   // C ブロックの下の桟
    int slotPadX      = 7;    // 入力欄の左右の余白
    int slotMinW      = 26;   // 入力欄のいちばん狭い幅
    int slotWideMinW  = 46;   // 長めの入力欄のいちばん狭い幅
    int slotMaxW      = 320;
    int slotHeight    = 20;
};

/** 文字の幅を測る道具。GDI でも、テスト用の作りものでも構いません。 */
struct TextMeasurer {
    virtual ~TextMeasurer() {}
    virtual int Width(const std::string& utf8) const = 0;
};

// ------------------------------------------------------------------ 置き場所
enum class PieceKind {
    Block,      // ブロックそのもの
    Label,      // 文字
    Slot,       // 入力欄・えらぶ欄（中にブロックが入ることもある）
    Arm,        // C ブロックの中身
};

struct Piece {
    PieceKind kind = PieceKind::Label;
    int x = 0, y = 0, w = 0, h = 0;      // 置き場所（親からの相対ではなく、通し）

    std::string text;                    // Label のとき出す文字
    std::string argName;                 // Slot のとき、どの引数か
    std::string subKey;                  // Arm のとき、どの腕か
    const BlockDef* def = NULL;          // Block のとき
    const JValue* node = NULL;           // Block / Slot がつながっている ghost.json の場所
    ArgKind argKind = ArgKind::Input;
    ArgMode argMode = ArgMode::Text;
    bool boolSlot = false;               // 六角のスロット
    int depth = 0;                       // 入れ子の深さ（当たり判定の優先に使う）

    int firstChild = -1, childCount = 0; // Layout::pieces の中での位置
};

/** 1 つのスクリプト（帽子＋つながっているブロック）を並べた結果。 */
struct Layout {
    std::vector<Piece> pieces;
    int width = 0, height = 0;

    /** (x, y) にある、いちばん内側のかけらを返す。無ければ -1。 */
    int HitTest(int x, int y) const;
    /** そのかけらを含んでいるブロックを返す（自分がブロックならそれ）。無ければ -1。 */
    int BlockAt(int piece) const;
};

// -------------------------------------------------------------------- 並べる

/**
 * ブロックの列（ghost.json の "blocks" の中身）を、(x, y) から下へ並べます。
 * project は変数名などを引くために渡します（無ければ NULL で構いません）。
 */
void LayoutStack(const JValue& blocks, int x, int y,
                 const Metrics& m, const TextMeasurer& tm, Layout* out);

/** スクリプト 1 つぶん（帽子とその下）を並べます。 */
void LayoutScript(const JValue& script, int x, int y,
                  const Metrics& m, const TextMeasurer& tm, Layout* out);

/** ブロック 1 つを、単独で並べます（パレットに出すとき用）。 */
void LayoutOne(const JValue& block, int x, int y,
               const Metrics& m, const TextMeasurer& tm, Layout* out);

/** その欄に出す文字（引数の値、またはえらんだ物の名前）。 */
std::string SlotText(const JValue& owner, const ArgDef& arg);

} // namespace w2k
} // namespace nashi
