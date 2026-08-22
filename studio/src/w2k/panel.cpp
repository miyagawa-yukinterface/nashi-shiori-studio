#include "panel.h"

#include "lint.h"

#include <cstdio>
#include <cstdlib>

namespace nashi {
namespace w2k {

namespace {

// たなの中の寸法
const int kPad = 12;          // 左右の余白
const int kGap = 8;           // 部品のあいだ
const int kHeadH = 26;
const int kHintH = 18;
const int kTextH = 20;
const int kButtonH = 26;
const int kFieldH = 24;
const int kRowH = 36;
const int kImageH = 96;       // 立ち絵の見本の高さ
const int kLabelW = 96;       // Field の見出しの幅

const char* const kTabNames[kTabCount] = {
    "ためす", "ゴースト", "立ち絵", "うごき", "変数", "さがす", "チェック", "書き出し", "ヘルプ",
};

/** 大文字を小文字にする（半角のぶんだけ。字を引くのに使います）。 */
std::string Lower(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        if (out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
    }
    return out;
}

bool Contains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return false;
    return Lower(hay).find(Lower(needle)) != std::string::npos;
}

/** 空白をつめる（改行やならんだ空白を 1 つに）。 */
std::string Squeeze(const std::string& s) {
    std::string out;
    bool space = false;
    for (size_t i = 0; i < s.size(); i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { space = true; continue; }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += s[i];
    }
    return out;
}

// ---------------------------------------------------------------- 組み立て
struct Builder {
    std::vector<PanelItem>* out;
    const TextMeasurer* tm;
    int x, width, y;

    void Add(ItemKind kind, const std::string& id, const std::string& text,
             int h, const std::string& value = std::string(),
             const std::string& sub = std::string(), int mark = 0) {
        PanelItem it;
        it.kind = kind;
        it.id = id;
        it.text = text;
        it.value = value;
        it.sub = sub;
        it.mark = mark;
        it.x = x + kPad;
        it.y = y;
        it.w = width - kPad * 2;
        it.h = h;
        out->push_back(it);
        y += h + kGap;
    }

    void Head(const std::string& text) { Add(ItemKind::Head, "", text, kHeadH); }
    void Hint(const std::string& text) { Add(ItemKind::Hint, "", text, kHintH); }
    void Text(const std::string& text) { Add(ItemKind::Text, "", text, kTextH); }
    void Button(const std::string& id, const std::string& text) {
        Add(ItemKind::Button, id, text, kButtonH);
        // 押しやすいように、幅は字の広さに合わせます
        PanelItem& it = (*out)[out->size() - 1];
        const int w = tm->Width(text) + 24;
        if (w < it.w) it.w = w;
    }
    void Field(const std::string& id, const std::string& label, const std::string& value) {
        Add(ItemKind::Field, id, label, kFieldH, value);
    }
    void Row(const std::string& id, const std::string& text, const std::string& sub, int mark) {
        Add(ItemKind::Row, id, text, kRowH, "", sub, mark);
    }
    void Color(const std::string& id, const std::string& label, const std::string& value) {
        Add(ItemKind::Color, id, label, kFieldH, value);
    }
    void Image(const std::string& id, const std::string& label, const std::string& path,
               int surface) {
        Add(ItemKind::Image, id, label, kImageH, path);
        (*out)[out->size() - 1].surface = surface;
    }
    /** 決まった中からえらぶ欄。いまの値に合う見出しを出します。 */
    void Choice(const std::string& id, const std::string& label, const std::string& value,
                const OptionDef* opts, int count) {
        std::string shown = value;
        for (int i = 0; i < count; i++) {
            if (value == opts[i].value) { shown = opts[i].label; break; }
        }
        Add(ItemKind::Choice, id, label, kFieldH, shown);
        PanelItem& it = (*out)[out->size() - 1];
        for (int i = 0; i < count; i++) {
            it.options.push_back(std::make_pair(std::string(opts[i].label),
                                                std::string(opts[i].value)));
        }
    }
};

// 立ち絵の番号と、その名前
struct ShellSlot { int id; const char* name; };
const ShellSlot kShellSlots[] = {
    { 0, "さくら／通常" }, { 1, "さくら／笑顔" }, { 2, "さくら／驚き" },
    { 10, "うにゅう／通常" }, { 11, "うにゅう／笑顔" }, { 12, "うにゅう／驚き" },
};
const int kShellSlotCount = 6;

std::string IntToStr(int v) {
    char buf[24];
    sprintf(buf, "%d", v);
    return buf;
}

// -------------------------------------------------------------- 変数のたな
void BuildVars(Builder& b, const JValue& project) {
    b.Head("変数");
    b.Hint("好感度やフラグなど、ゴーストが覚えておく値です。");
    b.Button("var.add", "＋ 変数をつくる");

    const JValue& vars = project["variables"];
    if (vars.size() == 0) {
        b.Text("（まだ変数はありません）");
        return;
    }
    for (size_t i = 0; i < vars.size(); i++) {
        const JValue& v = vars.at(i);
        const std::string n = IntToStr((int)i);
        b.Field("var.name." + n, "なまえ", v["name"].asStr());
        b.Field("var.value." + n, "はじめの値", v["value"].asStr());
        b.Button("var.del." + n, "けす");
    }
}

// -------------------------------------------------------------- さがすたな
void BuildSearch(Builder& b, const JValue& project, const PanelState& state) {
    b.Head("さがす");
    b.Hint("セリフ・変数・トーク名などから探します。");
    b.Field("search.query", "さがす言葉", state.query);

    if (state.query.empty()) {
        b.Text("さがす言葉を入れてください。");
        return;
    }
    std::vector<SearchHit> hits;
    SearchProject(project, state.query, &hits);
    if (hits.empty()) {
        b.Text("見つかりませんでした");
        return;
    }
    for (size_t i = 0; i < hits.size(); i++) {
        b.Row("search.hit." + IntToStr((int)i), hits[i].text,
              hits[i].isBlock ? hits[i].title : std::string(), 0);
    }
}

// -------------------------------------------------------------- ためすたな
void BuildRun(Builder& b, const JValue& project, const PanelState& state) {
    b.Head("ためす");
    b.Hint("かたまりを押すと、その場で動かしてみます。");

    const JValue& scripts = project["scripts"];
    if (scripts.size() == 0) {
        b.Text("（まだ、かたまりがありません）");
    }
    for (size_t i = 0; i < scripts.size(); i++) {
        b.Row("run.go." + IntToStr((int)i), ScriptTitle(scripts.at(i)),
              scripts.at(i)["id"].asStr(), 0);
    }

    if (!state.runOut.empty()) {
        b.Head("さくらスクリプト");
        if (!state.runTitle.empty()) b.Hint(state.runTitle);
        for (size_t i = 0; i < state.runOut.size(); i++) b.Text(state.runOut[i]);
    }

    // ---- 動いている SSP に送ってみる
    b.Head("SSP に送る");
    b.Hint(state.sspState.empty() ? "「様子を見る」を押すと、SSP を探します。"
                                  : state.sspState);
    b.Button("ssp.check", "様子を見る");
    b.Button("ssp.say", "いまのスクリプトをしゃべらせる");
    b.Button("ssp.event", "このイベントを送る");
    b.Button("ssp.comm", "話しかけてみる");
    b.Button("ssp.forget", "ゴーストの記憶を消す");
    b.Hint("「記憶を消す」は、書き出したゴーストが覚えた変数（nashi_save.json）を消します。");
    for (size_t i = 0; i < state.sspOut.size(); i++) b.Text(state.sspOut[i]);
}

// ------------------------------------------------------------ ゴーストのたな
void BuildGhost(Builder& b, const JValue& project) {
    const JValue& meta = project["meta"];
    const JValue& st = project["settings"];

    b.Head("ゴーストの設定");
    b.Field("meta.name", "ゴースト名", meta["name"].asStr());
    b.Field("meta.sakuraName", "本体の名前", meta["sakuraName"].asStr());
    b.Field("meta.keroName", "相方の名前", meta["keroName"].asStr());
    b.Field("meta.craftman", "作者名", meta["craftman"].asStr());
    b.Field("meta.craftmanUrl", "作者URL", meta["craftmanUrl"].asStr());
    b.Field("meta.homeUrl", "更新のありか", meta["homeUrl"].asStr());
    b.Hint("ネットワーク更新に使う URL です。うしろに / を付けます。");
    b.Field("meta.version", "バージョン", meta["version"].asStr());
    b.Field("meta.description", "説明", meta["description"].asStr());

    b.Head("ランダムトーク");
    const bool on = !(st["randomTalkEnabled"].type == JType::Bool
                      && !st["randomTalkEnabled"].b);
    b.Button("settings.randomTalkEnabled",
             on ? "自動でしゃべる：する" : "自動でしゃべる：しない");
    b.Field("settings.randomTalkInterval", "しゃべる間隔（秒）",
            st["randomTalkInterval"].asStr());
    b.Field("settings.noRepeatCount", "同じを避ける数",
            st["noRepeatCount"].asStr());
    b.Hint("0 ならおまかせ（トーク数の半分・最大 8）です。");

    b.Head("立ち絵・うごき");
    b.Hint("それぞれ「立ち絵」「うごき」のたなにあります。");
}

// -------------------------------------------------------------- 立ち絵のたな
void BuildShell(Builder& b, const JValue& project) {
    const JValue& sh = project["shell"];

    b.Head("立ち絵の画像");
    b.Hint("用意した PNG を割りあてられます。えらばなかった番号は、下の色で作ります。");

    const JValue& images = sh["images"];
    for (int i = 0; i < kShellSlotCount; i++) {
        const int id = kShellSlots[i].id;
        std::string path;
        for (size_t k = 0; k < images.size(); k++) {
            if (images.at(k)["id"].asInt(-1) == id) { path = images.at(k)["path"].asStr(); break; }
        }
        b.Image("shell.pick." + IntToStr(id), kShellSlots[i].name, path, id);
        if (!path.empty()) b.Button("shell.clear." + IntToStr(id), "はずす");
    }

    b.Head("見た目（仮シェル）");
    b.Hint("絵を用意していないときは、この色でかんたんな立ち絵を作ります。");
    b.Color("shell.sakuraColor", "さくらの髪", sh["sakuraColor"].asStr());
    b.Color("shell.sakuraCloth", "さくらの服", sh["sakuraCloth"].asStr());
    b.Color("shell.keroColor", "うにゅうの髪", sh["keroColor"].asStr());
    b.Color("shell.keroCloth", "うにゅうの服", sh["keroCloth"].asStr());

    b.Head("バルーン");
    const bool on = sh["balloonEnabled"].asBool(false);
    b.Button("shell.balloonEnabled", on ? "バルーンも作る：する" : "バルーンも作る：しない");
    b.Color("shell.balloonColor", "バルーンの色", sh["balloonColor"].asStr());
    b.Hint("作らないときは、使う人が持っているバルーンで出ます。");
}

// ---------------------------------------------------------------- うごきのたな
void BuildAnim(Builder& b, const JValue& project) {
    b.Head("うごき");
    b.Hint("立ち絵をパラパラ切りかえて動かします。「うごき N を再生する」で呼べます。");
    b.Button("anim.add", "＋ うごきを作る");

    const JValue& anims = project["animations"];
    if (anims.size() == 0) {
        b.Text("（まだ、うごきはありません）");
        return;
    }

    int nInterval = 0, nMethod = 0, nShape = 0;
    const OptionDef* intervals = AllAnimIntervals(&nInterval);
    const OptionDef* methods = AllAnimMethods(&nMethod);
    const OptionDef* shapes = AllAnimShapes(&nShape);

    for (size_t i = 0; i < anims.size(); i++) {
        const JValue& a = anims.at(i);
        const std::string n = IntToStr((int)i);

        b.Head("うごき " + a["id"].asStr("0"));
        b.Field("anim." + n + ".id", "番号", a["id"].asStr("0"));
        b.Field("anim." + n + ".base", "土台の立ち絵", a["base"].asStr("0"));
        b.Choice("anim." + n + ".interval", "きっかけ",
                 a["interval"].asStr("never"), intervals, nInterval);
        b.Field("anim." + n + ".every", "◯のところ", a["every"].asStr("4"));
        b.Button("anim." + n + ".del", "このうごきをけす");

        // ---- こま
        b.Hint("こま（上から順に出します）");
        const JValue& patterns = a["patterns"];
        for (size_t k = 0; k < patterns.size(); k++) {
            const JValue& p = patterns.at(k);
            const std::string pk = "anim." + n + ".pattern." + IntToStr((int)k);
            b.Field(pk + ".surface", "立ち絵の番号", p["surface"].asStr("0"));
            b.Choice(pk + ".method", "重ねかた", p["method"].asStr("base"), methods, nMethod);
            b.Field(pk + ".wait", "待つ（ミリ秒）", p["wait"].asStr("200"));
            b.Field(pk + ".x", "よこの位置", p["x"].asStr("0"));
            b.Field(pk + ".y", "たての位置", p["y"].asStr("0"));
            b.Button(pk + ".del", "このこまをけす");
        }
        b.Button("anim." + n + ".pattern.add", "＋ こまを足す");

        // ---- このうごきの間だけの当たり判定
        b.Hint("このうごきの間だけの当たり判定");
        const JValue& cols = a["collisions"];
        for (size_t k = 0; k < cols.size(); k++) {
            const JValue& c = cols.at(k);
            const std::string ck = "anim." + n + ".area." + IntToStr((int)k);
            b.Field(ck + ".name", "名前", c["name"].asStr());
            b.Choice(ck + ".shape", "かたち", c["shape"].asStr("rect"), shapes, nShape);
            if (c["shape"].asStr("rect") == "polygon") {
                b.Field(ck + ".points", "かどのならび", c["points"].asStr());
            } else {
                b.Field(ck + ".x1", "左上 よこ", c["x1"].asStr("0"));
                b.Field(ck + ".y1", "左上 たて", c["y1"].asStr("0"));
                b.Field(ck + ".x2", "右下 よこ", c["x2"].asStr("0"));
                b.Field(ck + ".y2", "右下 たて", c["y2"].asStr("0"));
            }
            b.Button(ck + ".del", "この当たり判定をけす");
        }
        b.Button("anim." + n + ".area.add", "＋ 当たり判定を足す");
    }
}

// ------------------------------------------------------------------ ヘルプのたな
void BuildHelp(Builder& b) {
    b.Head("なしスタジオ");
    b.Hint("ブロックを組んで、伺かのゴーストを作る道具です。");

    b.Head("つかいかた");
    b.Text("・左の置き場からブロックをつまんで、まん中に置きます");
    b.Text("・帽子（◯◯されたとき）につなげると動きます");
    b.Text("・ブロックの左のはしをつかむと、下のものもついてきます");
    b.Text("・置き場へもどすと、すてられます");
    b.Text("・欄を押すと、打ちこみ・えらびになります");

    b.Head("キー");
    b.Text("Ctrl+S  保存する");
    b.Text("Ctrl+O  ひらく");
    b.Text("Ctrl+Z  ひとつ前にもどす（もう一度でやりなおし）");
    b.Text("Esc     つまんでいるのをやめる");
    b.Text("矢印    画面を動かす（ホイールでも動きます）");

    b.Head("たな");
    b.Text("ためす   その場で動かして、さくらスクリプトを見ます");
    b.Text("ゴースト 名前や作者、ランダムトークの設定");
    b.Text("立ち絵   絵をえらぶ／色を決める");
    b.Text("うごき   立ち絵をパラパラ切りかえる");
    b.Text("変数     ゴーストが覚えておく値");
    b.Text("さがす   セリフや名前から引く");
    b.Text("チェック あぶないところを言います");
    b.Text("書き出し SSP に入れられる形にします");

    b.Head("こまったら");
    b.Text("docs\\ の中に、作りかたの手引きがあります。");
    b.Text("チェックのたなを見ると、たいていの原因が分かります。");
}

// ------------------------------------------------------------ 書き出しのたな
void BuildExport(Builder& b, const PanelState& state) {
    b.Head("書き出し");
    b.Hint("SSP に入れられる形で書き出します。");
    b.Field("export.dir", "出す先", state.exportDir);
    b.Button("export.folder", "フォルダに書き出す");
    b.Button("export.nar", ".nar にまとめる");

    b.Head("SSP に入れる");
    b.Hint(state.sspState.empty() ? "「様子を見る」を押すと、SSP を探します。"
                                  : state.sspState);
    b.Button("ssp.check", "様子を見る");
    b.Button("ssp.install", "SSP に入れて動かす");
    b.Hint("SSP の ghost フォルダへ書き出して、そのゴーストに切りかえます。");

    for (size_t i = 0; i < state.exportOut.size(); i++) b.Text(state.exportOut[i]);
    for (size_t i = 0; i < state.sspOut.size(); i++) b.Text(state.sspOut[i]);
}

// ------------------------------------------------------------ チェックのたな
void BuildCheck(Builder& b, const JValue& project) {
    b.Head("チェック");
    b.Hint("気になるところをならべます。押すと、その場所へ動きます。");

    std::vector<LintIssue> issues;
    LintProject(project, &issues);
    if (issues.empty()) {
        b.Text("✓ 気になるところはありません");
        return;
    }
    const int errors = CountLintErrors(issues);
    char head[96];
    sprintf(head, "まちがい %d / 気になる %d", errors, (int)issues.size() - errors);
    b.Text(head);

    for (size_t i = 0; i < issues.size(); i++) {
        b.Row("check.hit." + IntToStr((int)i), issues[i].message, issues[i].hint,
              issues[i].level == LintLevel::Error ? 2 : 1);
    }
}

} // namespace

// ------------------------------------------------------------------- たな
const char* TabName(Tab tab) {
    const int i = (int)tab;
    return (i >= 0 && i < kTabCount) ? kTabNames[i] : "";
}

Tab TabAt(int i) {
    if (i < 0) i = 0;
    if (i >= kTabCount) i = kTabCount - 1;
    return (Tab)i;
}

// ------------------------------------------------------------------- 中身
std::string BlockSummary(const JValue& block, int depth) {
    if (!block.isObj()) return std::string();
    if (depth > 6) return "…";

    const BlockDef* def = FindBlockFor(block["type"].asStr(), block["op"].asStr());
    if (!def) return block["type"].asStr();

    std::string s;
    int n = 0;
    const PartDef* parts = BlockParts(*def, &n);
    for (int i = 0; i < n; i++) {
        if (!parts[i].isArg) { s += parts[i].text; continue; }

        const JValue& v = block[parts[i].text];
        if (v.isNull() || (v.type == JType::Str && v.str.empty())) { s += "◯"; continue; }
        if (v.isObj()) {
            if (!v["type"].asStr().empty()) s += "（" + BlockSummary(v, depth + 1) + "）";
            else s += "…";
            continue;
        }
        const ArgDef* arg = FindArg(*def, parts[i].text);
        if (arg) s += SlotText(block, *arg);
        else s += v.asStr();
    }
    return Squeeze(s);
}

std::string ScriptTitle(const JValue& s) {
    if (!s.isObj()) return std::string();
    const std::string kind = s["kind"].asStr("event");

    if (kind == "event") {
        const std::string event = s["event"].asStr();
        int n = 0;
        const OptionDef* events = AllEvents(&n);
        std::string label = event.empty() ? "イベント" : event;
        for (int i = 0; i < n; i++) {
            if (event == events[i].value) { label = events[i].label; break; }
        }

        if (event == "OnSecondChange") {
            int every = s["everySec"].asInt(1);
            if (every < 1) every = 1;
            if (every > 1) {
                char buf[64];
                sprintf(buf, "%d秒ごとにくりかえす", every);
                return buf;
            }
            return label;
        }

        if (IsMouseEvent(event)) {
            const std::string areaValue = s["area"].asStr();
            const int whoValue = s.has("who") ? s["who"].asInt(-1) : -1;

            std::string area = areaValue;
            int an = 0;
            const OptionDef* areas = AllAreas(&an);
            for (int i = 0; i < an; i++) {
                if (areaValue == areas[i].value) { area = areas[i].label; break; }
            }
            std::string who;
            int wn = 0;
            const OptionDef* whos = AllWho(&wn);
            for (int i = 0; i < wn; i++) {
                char buf[16];
                sprintf(buf, "%d", whoValue);
                if (std::string(buf) == whos[i].value) { who = whos[i].label; break; }
            }
            if (!areaValue.empty() || whoValue >= 0) label = who + "の" + area + "が" + label;
        }

        if (event == "OnCommunicate") {
            const std::string from = s["from"].asStr();
            const std::string contains = s["contains"].asStr();
            if (!from.empty() || !contains.empty()) {
                const std::string who = from.empty() ? "だれか" : from;
                label = contains.empty() ? (who + "に" + label)
                                         : (who + "に「" + contains + "」と" + label);
            }
        }
        return label;
    }

    if (kind == "talk") {
        std::string group = s["group"].asStr();
        // 前後の空白は落とす
        while (!group.empty() && (group[0] == ' ' || group[0] == '\t')) group.erase(0, 1);
        while (!group.empty() && (group[group.size() - 1] == ' '
                                  || group[group.size() - 1] == '\t')) {
            group.erase(group.size() - 1);
        }
        std::string out = "ランダムトーク「" + s["name"].asStr() + "」";
        if (!group.empty()) out += "（" + group + "）";
        return out;
    }

    if (kind == "function") return "「" + s["name"].asStr() + "」";
    return "ブロックのかたまり";
}

namespace {

/** ブロックの中を、入れ子もふくめて順に見る。 */
void WalkBlocks(const JValue& list, std::vector<const JValue*>* out);

void WalkInner(const JValue& b, std::vector<const JValue*>* out) {
    const BlockDef* def = FindBlockFor(b["type"].asStr(), b["op"].asStr());
    if (!def) return;

    int n = 0;
    const SubDef* subs = BlockSubs(*def, &n);
    for (int i = 0; i < n; i++) WalkBlocks(b[subs[i].key], out);

    if (def->dynamic && def->dynamic[0]) {
        const JValue& groups = b[def->dynamic];
        for (size_t i = 0; i < groups.size(); i++) WalkBlocks(groups.at(i), out);
    }

    const ArgDef* args = BlockArgs(*def, &n);
    for (int i = 0; i < n; i++) {
        const JValue& v = b[args[i].name];
        if (!v.isObj() || v["type"].asStr().empty()) continue;
        out->push_back(&v);
        WalkInner(v, out);
    }
}

void WalkBlocks(const JValue& list, std::vector<const JValue*>* out) {
    for (size_t i = 0; i < list.size(); i++) {
        const JValue& b = list.at(i);
        if (!b.isObj()) continue;
        out->push_back(&b);
        WalkInner(b, out);
    }
}

} // namespace

namespace {

/** 無ければ既定値を入れる（あるものは触らない）。 */
void FillDefault(JValue& obj, const char* key, const JValue& value) {
    if (obj.has(key)) return;
    obj.set(key, value);
}

/** obj["key"] を取り出す（無ければ作る）。 */
JValue* Sub(JValue& obj, const char* key) {
    if (!obj[key].isObj()) obj.set(key, JValue::makeObj());
    for (size_t i = 0; i < obj.obj.size(); i++) {
        if (obj.obj[i].first == key) return &obj.obj[i].second;
    }
    return NULL;
}

/** obj["key"] のならびを取り出す（無ければ作る）。 */
JValue* SubArr(JValue& obj, const char* key) {
    if (!obj[key].isArr()) obj.set(key, JValue::makeArr());
    for (size_t i = 0; i < obj.obj.size(); i++) {
        if (obj.obj[i].first == key) return &obj.obj[i].second;
    }
    return NULL;
}

/** 前後の空白を落とす。 */
std::string Strip(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'
                     || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

/** その値が、表にある値か。 */
bool IsOneOf(const std::string& v, const OptionDef* opts, int count) {
    for (int i = 0; i < count; i++) if (v == opts[i].value) return true;
    return false;
}

/** 数として読む（読めなければ既定値）。 */
double NumOr(const JValue& v, double def) {
    if (v.type == JType::Num) return v.num;
    if (v.type == JType::Str) {
        const char* p = v.str.c_str();
        char* end = NULL;
        const double d = strtod(p, &end);
        if (end && *end == 0 && end != p) return d;
    }
    return def;
}

/**
 * 変数のならびをととのえる。
 * 名前のないものは落とし、値が無ければ 0 にします（model.js と同じ）。
 */
void NormalizeVariables(JValue& project) {
    JValue* vars = SubArr(project, "variables");
    if (!vars) return;
    JValue out = JValue::makeArr();
    for (size_t i = 0; i < vars->arr.size(); i++) {
        const JValue& v = vars->arr[i];
        if (!v.isObj()) continue;
        const std::string name = v["name"].asStr();
        if (name.empty()) continue;
        JValue one = JValue::makeObj();
        one.set("name", JValue::makeStr(name));
        one.set("value", v["value"].isNull() ? JValue::makeNum(0) : v["value"]);
        out.arr.push_back(one);
    }
    project.set("variables", out);
}

/**
 * うごきのならびをととのえる。
 * 知らない重ねかたは「土台をとりかえる」に、知らないかたちは「四角」に倒し、
 * 名前のない当たり判定は落とします（model.js と同じ）。
 */
void NormalizeAnimations(JValue& project) {
    JValue* anims = SubArr(project, "animations");
    if (!anims) return;

    int nMethod = 0, nShape = 0;
    const OptionDef* methods = AllAnimMethods(&nMethod);
    const OptionDef* shapes = AllAnimShapes(&nShape);

    JValue out = JValue::makeArr();
    for (size_t i = 0; i < anims->arr.size(); i++) {
        const JValue& a = anims->arr[i];
        JValue one = JValue::makeObj();
        one.set("id", JValue::makeNum(NumOr(a["id"], (double)i)));
        one.set("base", JValue::makeNum(NumOr(a["base"], 0)));
        one.set("interval", JValue::makeStr(a["interval"].asStr().empty()
                                            ? "never" : a["interval"].asStr()));
        double every = NumOr(a["every"], 4);
        if (every == 0) every = 4;
        one.set("every", JValue::makeNum(every));

        JValue patterns = JValue::makeArr();
        const JValue& ps = a["patterns"];
        for (size_t k = 0; k < ps.size(); k++) {
            const JValue& p = ps.at(k);
            JValue q = JValue::makeObj();
            q.set("surface", JValue::makeNum(NumOr(p["surface"], 0)));
            q.set("wait", JValue::makeNum(NumOr(p["wait"], 200)));
            const std::string m = p["method"].asStr();
            q.set("method", JValue::makeStr(IsOneOf(m, methods, nMethod) ? m : "base"));
            q.set("x", JValue::makeNum(NumOr(p["x"], 0)));
            q.set("y", JValue::makeNum(NumOr(p["y"], 0)));
            q.set("file", JValue::makeStr(p["file"].asStr()));
            patterns.arr.push_back(q);
        }
        one.set("patterns", patterns);

        JValue cols = JValue::makeArr();
        const JValue& cs = a["collisions"];
        for (size_t k = 0; k < cs.size(); k++) {
            const JValue& c = cs.at(k);
            const std::string name = Strip(c["name"].asStr());
            if (name.empty()) continue;
            JValue q = JValue::makeObj();
            q.set("name", JValue::makeStr(name));
            const std::string sh = c["shape"].asStr();
            q.set("shape", JValue::makeStr(IsOneOf(sh, shapes, nShape) ? sh : "rect"));
            q.set("x1", JValue::makeNum(NumOr(c["x1"], 0)));
            q.set("y1", JValue::makeNum(NumOr(c["y1"], 0)));
            q.set("x2", JValue::makeNum(NumOr(c["x2"], 0)));
            q.set("y2", JValue::makeNum(NumOr(c["y2"], 0)));
            q.set("points", JValue::makeStr(c["points"].asStr()));
            cols.arr.push_back(q);
        }
        one.set("collisions", cols);
        out.arr.push_back(one);
    }
    project.set("animations", out);
}

} // namespace

void EnsureScriptIds(JValue& project) {
    JValue* scripts = NULL;
    for (size_t i = 0; i < project.obj.size(); i++) {
        if (project.obj[i].first == "scripts") { scripts = &project.obj[i].second; break; }
    }
    if (!scripts || !scripts->isArr()) return;

    std::vector<std::string> used;
    for (size_t i = 0; i < scripts->arr.size(); i++) {
        const std::string id = scripts->arr[i]["id"].asStr();
        if (!id.empty()) used.push_back(id);
    }
    for (size_t i = 0; i < scripts->arr.size(); i++) {
        JValue& s = scripts->arr[i];
        if (!s.isObj() || !s["id"].asStr().empty()) continue;
        for (int n = (int)i; ; n++) {
            const std::string candidate = "s" + IntToStr(n);
            bool taken = false;
            for (size_t k = 0; k < used.size(); k++) {
                if (used[k] == candidate) { taken = true; break; }
            }
            if (taken) continue;
            s.set("id", JValue::makeStr(candidate));
            used.push_back(candidate);
            break;
        }
    }
}

void NormalizeProject(JValue& project) {
    if (!project.isObj()) return;
    if (!project.has("scripts")) project.set("scripts", JValue::makeArr());
    if (!project.has("variables")) project.set("variables", JValue::makeArr());

    EnsureScriptIds(project);
    NormalizeVariables(project);
    NormalizeAnimations(project);

    // ---- ゴーストの情報（ui\js\model.js の newProject と同じ既定値）
    if (JValue* meta = Sub(project, "meta")) {
        FillDefault(*meta, "name", JValue::makeStr("なしゴースト"));
        FillDefault(*meta, "sakuraName", JValue::makeStr("さくら"));
        FillDefault(*meta, "keroName", JValue::makeStr("うにゅう"));
        FillDefault(*meta, "craftman", JValue::makeStr(""));
        FillDefault(*meta, "craftmanUrl", JValue::makeStr(""));
        FillDefault(*meta, "homeUrl", JValue::makeStr(""));
        FillDefault(*meta, "version", JValue::makeStr("1.0.0"));
        FillDefault(*meta, "description", JValue::makeStr(""));
    }
    if (JValue* st = Sub(project, "settings")) {
        FillDefault(*st, "randomTalkInterval", JValue::makeNum(180));
        FillDefault(*st, "randomTalkEnabled", JValue::makeBool(true));
        FillDefault(*st, "noRepeatCount", JValue::makeNum(0));
        FillDefault(*st, "defaultSurfaceSakura", JValue::makeNum(0));
        FillDefault(*st, "defaultSurfaceKero", JValue::makeNum(10));
    }
    if (JValue* sh = Sub(project, "shell")) {
        FillDefault(*sh, "balloonEnabled", JValue::makeBool(false));
        FillDefault(*sh, "balloonColor", JValue::makeStr("#fffdf5"));
        FillDefault(*sh, "sakuraColor", JValue::makeStr("#f08cae"));
        FillDefault(*sh, "sakuraCloth", JValue::makeStr("#6e82c8"));
        FillDefault(*sh, "keroColor", JValue::makeStr("#8fd18a"));
        FillDefault(*sh, "keroCloth", JValue::makeStr("#e8b45c"));
        FillDefault(*sh, "images", JValue::makeArr());
    }

    JValue* scripts = NULL;
    for (size_t i = 0; i < project.obj.size(); i++) {
        if (project.obj[i].first == "scripts") { scripts = &project.obj[i].second; break; }
    }
    if (!scripts || !scripts->isArr()) return;

    int y = 40;
    for (size_t i = 0; i < scripts->arr.size(); i++) {
        JValue& s = scripts->arr[i];
        if (!s.isObj()) continue;

        if (s["kind"].type != JType::Str) s.set("kind", JValue::makeStr("event"));
        if (!s["blocks"].isArr()) s.set("blocks", JValue::makeArr());
        if (s["x"].type != JType::Num) s.set("x", JValue::makeNum(60));
        if (s["y"].type != JType::Num) { s.set("y", JValue::makeNum(y)); y += 180; }

        const std::string kind = s["kind"].asStr();
        if (kind == "talk") {
            if (s["weight"].isNull()) s.set("weight", JValue::makeNum(1));
            if (s["group"].type != JType::Str) s.set("group", JValue::makeStr(""));
        }
        if (kind == "talk" || kind == "function") {
            if (s["name"].asStr().empty()) {
                s.set("name", JValue::makeStr(kind == "talk" ? "トーク" : "なまえのないトーク"));
            }
        }
        if (kind != "event") continue;

        if (s["event"].asStr().empty()) s.set("event", JValue::makeStr("OnBoot"));
        if (s["area"].type != JType::Str) s.set("area", JValue::makeStr(""));
        if (s["who"].type != JType::Num) s.set("who", JValue::makeNum(-1));
        if (s["from"].type != JType::Str) s.set("from", JValue::makeStr(""));
        if (s["contains"].type != JType::Str) s.set("contains", JValue::makeStr(""));
        int every = s["everySec"].asInt(1);
        if (every < 1) every = 1;
        s.set("everySec", JValue::makeNum(every));

        // 読みこんだゴーストは、しぼり込みを filter の中に持っています。ほどきます。
        if (!s["filter"].isObj()) continue;
        const JValue filter = s["filter"];
        if (filter["area"].type == JType::Str) s.set("area", filter["area"]);
        if (filter["who"].type == JType::Num) s.set("who", filter["who"]);
        if (filter["from"].type == JType::Str) s.set("from", filter["from"]);
        if (filter["contains"].type == JType::Str) s.set("contains", filter["contains"]);
        for (size_t k = 0; k < s.obj.size(); k++) {
            if (s.obj[k].first == "filter") { s.obj.erase(s.obj.begin() + k); break; }
        }
    }
}

void CollectBlocks(const JValue& script, std::vector<const JValue*>* out) {
    out->clear();
    WalkBlocks(script["blocks"], out);
}

void SearchProject(const JValue& project, const std::string& query,
                   std::vector<SearchHit>* out) {
    out->clear();
    if (query.empty()) return;

    const JValue& scripts = project["scripts"];
    for (size_t i = 0; i < scripts.size(); i++) {
        const JValue& s = scripts.at(i);
        const std::string title = ScriptTitle(s);
        if (Contains(title, query)) {
            SearchHit hit;
            hit.scriptIndex = (int)i;
            hit.isBlock = false;
            hit.text = title;
            out->push_back(hit);
        }

        std::vector<const JValue*> blocks;
        WalkBlocks(s["blocks"], &blocks);
        for (size_t k = 0; k < blocks.size(); k++) {
            if (out->size() > 200) return;
            const std::string text = BlockSummary(*blocks[k]);
            if (!Contains(text, query)) continue;
            SearchHit hit;
            hit.scriptIndex = (int)i;
            hit.isBlock = true;
            hit.text = text;
            hit.title = title;
            out->push_back(hit);
        }
    }
}

// ------------------------------------------------------------------ 組み立て
void BuildPanel(const JValue& project, const PanelState& state,
                const TextMeasurer& tm, int x, int width,
                std::vector<PanelItem>* out) {
    out->clear();
    Builder b;
    b.out = out;
    b.tm = &tm;
    b.x = x;
    b.width = width;
    b.y = kPad - state.scroll;

    switch (state.tab) {
        case Tab::Vars:   BuildVars(b, project); break;
        case Tab::Search: BuildSearch(b, project, state); break;
        case Tab::Check:  BuildCheck(b, project); break;
        case Tab::Run:    BuildRun(b, project, state); break;
        case Tab::Ghost:  BuildGhost(b, project); break;
        case Tab::Shell:  BuildShell(b, project); break;
        case Tab::Anim:   BuildAnim(b, project); break;
        case Tab::Help:   BuildHelp(b); break;
        case Tab::Export: BuildExport(b, state); break;
        default:
            b.Head(TabName(state.tab));
            b.Hint("このたなは、まだ作っていません。");
            b.Text("いまは WebView2 版でお使いください。");
            break;
    }
}

int PanelHitTest(const std::vector<PanelItem>& items, int px, int py) {
    for (size_t i = 0; i < items.size(); i++) {
        const PanelItem& it = items[i];
        if (it.kind != ItemKind::Button && it.kind != ItemKind::Field
            && it.kind != ItemKind::Row && it.kind != ItemKind::Color
            && it.kind != ItemKind::Image && it.kind != ItemKind::Choice) continue;
        if (px < it.x || px >= it.x + it.w || py < it.y || py >= it.y + it.h) continue;
        return (int)i;
    }
    return -1;
}

} // namespace w2k
} // namespace nashi
