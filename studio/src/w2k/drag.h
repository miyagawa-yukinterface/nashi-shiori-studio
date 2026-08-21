// なしスタジオ（ネイティブ版）- ブロックをつまんで、つなぐ
//
// **ここにも GDI が出てきません。** 「どこに置けるか」「いちばん近いのはどれか」
// 「つまむ・はなす と ghost.json がどう変わるか」だけを扱います。
// 画面のことは window.cpp、絵は paint.cpp です。
//
// 決まりごとは ui\js\drag.js と同じにしてあります
//   ・つながる先は 46px まで（縦のずれを重く見る）
//   ・欄にはまるのは 34px まで
//   ・「ここでおわる」ブロックの後ろにはつなげない
//   ・六角の欄には六角のブロックしか入らない
#pragma once

#include "blockdefs.h"
#include "layout.h"
#include "../../../shiori/src/json.h"

#include <string>
#include <vector>

namespace nashi {
namespace w2k {

// ------------------------------------------------------------------ 道しるべ
// ghost.json の中のどこか、を指すもの。JValue の場所は差しこみで動くので、
// 生のポインタではなく「たどりかた」で持ちます。
struct JStep {
    bool isIndex = false;
    std::string key;
    int index = 0;

    static JStep Key(const std::string& k) { JStep s; s.key = k; return s; }
    static JStep Index(int i) { JStep s; s.isIndex = true; s.index = i; return s; }
};

struct JPath {
    std::vector<JStep> steps;
    JPath() {}
    JPath Then(const JStep& s) const { JPath p = *this; p.steps.push_back(s); return p; }
    std::string ToString() const;      // "scripts[0].blocks[2]" のような形（言いあらわす用）
};

/** その場所をたどる。無ければ NULL。 */
JValue* JResolve(JValue& root, const JPath& path);
const JValue* JResolve(const JValue& root, const JPath& path);

// -------------------------------------------------------------- 置ける場所
enum class DropKind {
    Stack,   // ならびの、あるところに差しこむ
    Slot,    // 欄にはめる
};

struct DropTarget {
    DropKind kind = DropKind::Stack;
    JPath owner;            // Stack: ならび（配列）の場所 / Slot: 欄を持つブロックの場所
    int index = 0;          // Stack: 何番目に差しこむか
    std::string argName;    // Slot: どの欄か
    bool boolSlot = false;  // Slot: 六角の欄か
    int x = 0, y = 0;       // つなぎ目の場所（Stack）／欄の左上（Slot）
    int w = 0, h = 0;       // 目印を出す大きさ
};

/** つまんでいるものの形。どこに置けるかが変わります。 */
enum class DragShape {
    Stack,      // ふつうのブロック（ならびに差しこむ）
    Reporter,   // 丸いもの（欄にはまる）
    Boolean,    // 六角のもの（六角の欄にはまる）
};

/**
 * その置き場所にある「置ける場所」をぜんぶ集めます。
 * script は lay を作るときに渡したものと同じ JValue でなければいけません
 * （かけらが指している場所から、たどりかたを割り出すため）。
 * scriptPath は script が ghost.json のどこにあるか（例: scripts[0]）。
 */
void CollectDropTargets(const Layout& lay, const JValue& script, const JPath& scriptPath,
                        const Metrics& m, DragShape shape, bool endsWithCap,
                        std::vector<DropTarget>* out);

/**
 * (x, y) にいちばん近い置き場所をえらびます。無ければ -1。
 * x, y は、つまんでいるかたまりの左上（Stack）／左のとがりのあたり（Reporter・Boolean）。
 */
int NearestDropTarget(const std::vector<DropTarget>& targets, DragShape shape, int x, int y);

// ------------------------------------------------------------ つまむ・はなす

/**
 * ならびの index 番目から下を、まとめて切りはなします。
 * 取ったものは outBlocks（配列）に入ります。
 */
bool PickUpStack(JValue& root, const JPath& listPath, int index, JValue* outBlocks);

/** 欄にはまっているブロックを取りはずします。取ったものは outBlock に入ります。 */
bool PickUpSlot(JValue& root, const JPath& ownerPath, const std::string& argName,
                const ArgDef* arg, JValue* outBlock);

/** 置き場所へ、つまんでいたもの（配列、または 1 つのブロック）を置きます。 */
bool DropAt(JValue& root, const DropTarget& target, const JValue& payload);

/** その欄を、はまっているブロックを外したときの空の値にもどします。 */
JValue EmptySlotValue(const ArgDef* arg);

} // namespace w2k
} // namespace nashi
