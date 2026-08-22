// なしスタジオ（ネイティブ版）- ブロックの定義
//
// 中身の表は tools\gen-blockdefs.js が tools\blocks.js から作ります
// （blockdefs_gen.h）。ブロックを足すときに触るのは、これまでどおり blocks.js だけです。
//
// ここにあるのは、その表を読むための型と、引きかたです。
#pragma once

#include <string>
#include <vector>

namespace nashi {
namespace w2k {

// ブロックの形。描きかたが変わります。
enum class Shape {
    Hat,        // 帽子（上が丸い。スクリプトの先頭）
    Stack,      // ふつうの命令（上に切り欠き、下に出っぱり）
    CBlock,     // 中にブロックを入れられるもの（もし〜なら）
    Cap,        // 下に何もつなげないもの（ここでおわる）
    Reporter,   // 値を返す丸いもの
    Boolean,    // はい／いいえを返す六角形
};

// 引数の入れかた
enum class ArgKind {
    Input,       // 文字や数を打ちこむ
    Dropdown,    // 決まった中からえらぶ
    EventName,   // イベント名をえらぶ（その他は打ちこみ）
    AreaName,    // 当たり判定の名前をえらぶ
    FuncName,    // トーク名をえらぶ
    VarName,     // 変数名をえらぶ
};

enum class ArgMode { Text, Number, Bool };

struct CategoryDef { const char* id; const char* name; const char* color; };
struct OptionDef   { const char* label; const char* value; };
struct ArgDef {
    const char* name;
    ArgKind kind;
    ArgMode mode;
    bool wide;              // 長めの入力欄にする
    const char* defValue;
    int optionStart, optionCount;
};
struct PartDef  { bool isArg; const char* text; };   // isArg なら text は引数の名前
struct SubDef   { const char* key; const char* label; };
struct FixedDef { const char* key; const char* value; };

struct BlockDef {
    const char* key;        // "say" や "arith#+"
    const char* type;       // ghost.json に入る型
    const char* category;
    Shape shape;
    const char* scriptKind; // 帽子のときだけ "event" / "talk" / "function"
    bool hat;
    const char* dynamic;    // 「つぎのどれかをランダムに」の腕を入れる名前
    int argStart, argCount;
    int partStart, partCount;
    int subStart, subCount;
    int fixedStart, fixedCount;
};

struct PaletteRow { const char* category; const char* key; };

// ------------------------------------------------------------------- 引きかた

/** key（"say" や "arith#+"）から定義を引く。無ければ NULL。 */
const BlockDef* FindBlock(const std::string& key);

/** ghost.json のブロックから、描くのに使う定義を引く（op ちがいも見ます）。 */
const BlockDef* FindBlockFor(const std::string& type, const std::string& op);

const CategoryDef* FindCategory(const std::string& id);

/** そのブロックの引数・ならび・腕を取り出す。 */
const ArgDef*    BlockArgs(const BlockDef& d, int* count);
const PartDef*   BlockParts(const BlockDef& d, int* count);
const SubDef*    BlockSubs(const BlockDef& d, int* count);
const FixedDef*  BlockFixed(const BlockDef& d, int* count);
const OptionDef* ArgOptions(const ArgDef& a, int* count);

/** その引数の定義を名前で引く。無ければ NULL。 */
const ArgDef* FindArg(const BlockDef& d, const std::string& name);

/** パレットに出すならび。 */
const PaletteRow* Palette(int* count);

/** イベントの名前のならび（「〜されたとき」でえらべるもの）。 */
const OptionDef* AllEvents(int* count);

/** そのイベントは、マウス系（どこを・だれを でしぼり込めるもの）か。 */
bool IsMouseEvent(const std::string& event);

/** うごき（SERIKO）の言葉。きっかけ・こまの重ねかた・当たり判定のかたち。 */
const OptionDef* AllAnimIntervals(int* count);
const OptionDef* AllAnimMethods(int* count);
const OptionDef* AllAnimShapes(int* count);

/** 当たり判定の名前と、だれの当たり判定か。 */
const OptionDef* AllAreas(int* count);
const OptionDef* AllWho(int* count);

/** 表ぜんぶ（点検やテスト用）。 */
const BlockDef* AllBlocks(int* count);
const CategoryDef* AllCategories(int* count);

} // namespace w2k
} // namespace nashi
