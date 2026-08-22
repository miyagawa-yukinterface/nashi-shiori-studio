// なしスタジオ（ネイティブ版）- 右の作業だな
//
// **ここにも GDI が出てきません。** 右がわに出す「ためす／ゴースト／変数／さがす／
// チェック／書き出し／ヘルプ」の中身を、置き場所つきのならびにするだけです。
// 描くのと押されたときのことは window.cpp が受けもちます。
//
// layout.cpp と同じ考えかたです。分けてあるのは、画面を出さずにテストするためです。
#pragma once

#include "blockdefs.h"
#include "layout.h"
#include "../../../shiori/src/json.h"

#include <string>
#include <vector>

namespace nashi {
namespace w2k {

// ------------------------------------------------------------------- たな
enum class Tab {
    Run,       // ためす
    Ghost,     // ゴースト
    Shell,     // 立ち絵
    Anim,      // うごき
    Vars,      // 変数
    Search,    // さがす
    Check,     // チェック
    Export,    // 書き出し
    Help,      // ヘルプ
};

const int kTabCount = 9;

/** たなの見出し（"ためす" など）。 */
const char* TabName(Tab tab);
/** i 番目のたな。 */
Tab TabAt(int i);

// --------------------------------------------------------------- 中身の部品
enum class ItemKind {
    Head,      // 見出し
    Hint,      // 小さな説明
    Text,      // ふつうの字
    Button,    // 押せるもの
    Field,     // 打ちこむ欄（左に見出し、右に中身）
    Row,       // ならびの 1 行（押すとその場所へ飛ぶ）
    Color,     // 色の欄（右はしに、その色の四角を出す）
    Image,     // 立ち絵（絵は window.cpp が描きます。ここは場所だけ決めます）
    Choice,    // 決まった中からえらぶ欄（押すとならびが出ます）
};

struct PanelItem {
    ItemKind kind = ItemKind::Text;
    std::string id;      // "var.add" や "search.hit.3" のような目じるし
    std::string text;    // 出す字
    std::string value;   // Field のときの中身
    std::string sub;     // Row のときの、うしろに小さく出す字
    int x = 0, y = 0, w = 0, h = 0;
    int mark = 0;        // Row のとき 0=ふつう 1=注意 2=まちがい
    int surface = -1;    // Image のとき、立ち絵の番号

    // Choice のとき、えらべるもの（見出しと値）
    std::vector<std::pair<std::string, std::string> > options;
};

/** たなを組み立てるときの、いまのようす。 */
struct PanelState {
    Tab tab = Tab::Vars;
    std::string query;                 // さがす言葉
    int scroll = 0;

    // ---- ためす
    std::string runTitle;              // さいごに動かしたかたまり
    std::vector<std::string> runOut;   // 出てきたさくらスクリプト（行ごと）

    // ---- SSP まわり（ためす・書き出しの両方で使います）
    std::string sspState;              // 「動いています」など、いまの様子
    std::vector<std::string> sspOut;   // 送ったときの言い分

    // ---- 書き出し
    std::string exportDir;             // 出す先
    std::vector<std::string> exportOut;
};

/** かたまりに id が無ければ付けます（「ためす」で 1 つえらぶのに要ります）。 */
void EnsureScriptIds(JValue& project);

/** 右の作業だなの中身を組み立てます。x は左端、width は幅。 */
void BuildPanel(const JValue& project, const PanelState& state,
                const TextMeasurer& tm, int x, int width,
                std::vector<PanelItem>* out);

/** (px, py) にある部品を返します。無ければ -1。 */
int PanelHitTest(const std::vector<PanelItem>& items, int px, int py);

// ------------------------------------------------------------------- 中身
/**
 * ブロック 1 つを、画面に出ているのと同じ言葉にします。
 * さがすときと、一覧の見出しに使います（ui\js\search.js の blockSummary と同じ）。
 */
std::string BlockSummary(const JValue& block, int depth = 0);

/** かたまりの見出し（ui\js\model.js の scriptTitle と同じ）。 */
std::string ScriptTitle(const JValue& script);

struct SearchHit {
    int scriptIndex = -1;
    bool isBlock = false;   // false ならかたまりそのもの
    std::string text;       // 出す字
    std::string title;      // どのかたまりか
};

/**
 * 読みこんだ ghost.json を、編集できる形にととのえます。
 * ui\js\model.js の normalize と同じことをします（かたまりのぶん）。
 *   ・足りないものを既定値でうめる（kind・blocks・置き場所・トークの名前など）
 *   ・**filter をほどく**（読みこんだゴーストは area / who / from / contains を
 *     filter の中に持っています。編集するときは、じかに持たせます）
 */
void NormalizeProject(JValue& project);

/** かたまりの中のブロックを、入れ子もふくめて順にならべます。 */
void CollectBlocks(const JValue& script, std::vector<const JValue*>* out);

/** ブロックの中身を字で引きます。 */
void SearchProject(const JValue& project, const std::string& query,
                   std::vector<SearchHit>* out);

} // namespace w2k
} // namespace nashi
