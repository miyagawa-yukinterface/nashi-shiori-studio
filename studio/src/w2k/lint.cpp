#include "lint.h"

#include <cstdio>

#include "blockdefs.h"
#include "panel.h"

namespace nashi {
namespace w2k {

namespace {

void Add(std::vector<LintIssue>* out, LintLevel level, const std::string& message,
         const std::string& hint, int scriptIndex, const JValue* block) {
    LintIssue it;
    it.level = level;
    it.message = message;
    it.hint = hint;
    it.scriptIndex = scriptIndex;
    it.block = block;
    out->push_back(it);
}

/** かたまりの中のブロックを、入れ子もふくめて順に見る（panel.cpp と同じ）。 */
void Walk(const JValue& script, std::vector<const JValue*>* out) {
    CollectBlocks(script, out);
}

/** そのブロックの内がわ（くりかえしの中など）だけを見る。 */
void WalkInsideOnly(const JValue& block, std::vector<const JValue*>* out) {
    const BlockDef* def = FindBlockFor(block["type"].asStr(), block["op"].asStr());
    if (!def) return;

    std::vector<const JValue*> stacks;
    int n = 0;
    const SubDef* subs = BlockSubs(*def, &n);
    for (int i = 0; i < n; i++) stacks.push_back(&block[subs[i].key]);
    if (def->dynamic && def->dynamic[0]) {
        const JValue& groups = block[def->dynamic];
        for (size_t i = 0; i < groups.size(); i++) stacks.push_back(&groups.at(i));
    }
    for (size_t k = 0; k < stacks.size(); k++) {
        const JValue& stack = *stacks[k];
        for (size_t i = 0; i < stack.size(); i++) {
            const JValue& b = stack.at(i);
            if (!b.isObj()) continue;
            out->push_back(&b);
            WalkInsideOnly(b, out);
        }
    }
}

/** 出す言葉のためのかたまり名。もう「」が付いているものには足さない。 */
std::string NameOf(const JValue& script) {
    const std::string t = ScriptTitle(script);
    // 先頭が「なら、そのまま
    if (t.size() >= 3 && (unsigned char)t[0] == 0xE3 && (unsigned char)t[1] == 0x80
        && (unsigned char)t[2] == 0x8C) {
        return t;
    }
    return "「" + t + "」";
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'
                     || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

/** 空っぽの字か（JavaScript 版の isEmptyText と同じ）。 */
bool IsEmptyText(const JValue& v) {
    if (v.isNull()) return true;
    if (v.isObj() || v.isArr()) return false;      // 変数などが入っている
    return Trim(v.asStr()).empty();
}

bool HasName(const std::vector<std::string>& list, const std::string& name) {
    for (size_t i = 0; i < list.size(); i++) if (list[i] == name) return true;
    return false;
}

std::string TypeOf(const JValue& b) { return b["type"].asStr(); }

std::string NumToText(double v) {
    char buf[40];
    if (v == (double)(long long)v) sprintf(buf, "%lld", (long long)v);
    else sprintf(buf, "%g", v);
    return buf;
}

} // namespace

void LintProject(const JValue& project, std::vector<LintIssue>* out) {
    out->clear();
    if (!project.isObj()) return;

    const JValue& scripts = project["scripts"];
    const JValue& variables = project["variables"];

    std::vector<std::string> varNames;
    for (size_t i = 0; i < variables.size(); i++) {
        varNames.push_back(variables.at(i)["name"].asStr());
    }

    std::vector<std::string> groups, callable;
    int talkCount = 0;
    for (size_t i = 0; i < scripts.size(); i++) {
        const JValue& s = scripts.at(i);
        const std::string kind = s["kind"].asStr();
        if (kind == "talk" && !s["disabled"].asBool(false)) {
            const std::string g = Trim(s["group"].asStr());
            if (!g.empty()) groups.push_back(g);
        }
        if (kind == "function" || kind == "talk") callable.push_back(s["name"].asStr());
        if (kind == "talk") talkCount++;
    }

    // ---- 名前のかぶり
    {
        std::vector<std::string> seen;
        for (size_t i = 0; i < scripts.size(); i++) {
            const JValue& s = scripts.at(i);
            const std::string kind = s["kind"].asStr();
            if (kind != "function" && kind != "talk") continue;
            const std::string name = s["name"].asStr();
            if (HasName(seen, name)) {
                Add(out, LintLevel::Error, "「" + name + "」という名前が 2 つあります",
                    "よぶときにどちらか一方しか使われません。名前を変えてください。",
                    (int)i, NULL);
            }
            seen.push_back(name);
        }
    }

    // ---- かたまりごと
    for (size_t si = 0; si < scripts.size(); si++) {
        const JValue& s = scripts.at(si);
        const int idx = (int)si;
        const std::string title = NameOf(s);
        const std::string kind = s["kind"].asStr();
        const JValue& blocks = s["blocks"];

        if (kind != "event" && kind != "talk" && kind != "function") {
            Add(out, LintLevel::Warn, "どこにもつながっていないブロックがあります",
                "帽子ブロック（◯◯のとき など）につなげないと動きません。", idx, NULL);
        } else if (blocks.size() == 0) {
            Add(out, LintLevel::Warn, title + "の中身がからっぽです",
                "ブロックをつなげるか、いらなければ削除してください。", idx, NULL);
        }

        if (kind == "event" && Trim(s["event"].asStr()).empty()) {
            Add(out, LintLevel::Error, "イベント名が空のかたまりがあります",
                "イベントをえらぶか、名前を直接入力してください。", idx, NULL);
        }
        if ((kind == "talk" || kind == "function") && Trim(s["name"].asStr()).empty()) {
            Add(out, LintLevel::Error, "名前のないトークがあります",
                "名前をつけないと、よび出せません。", idx, NULL);
        }
        if (kind == "talk" && s["weight"].asNum(0) < 0) {
            Add(out, LintLevel::Warn, title + "のえらばれやすさがマイナスです",
                "0 として扱われるので、このトークは出ません。", idx, NULL);
        }

        std::vector<const JValue*> all;
        Walk(s, &all);

        // ---- ずっとくりかえす の重さ
        if (kind == "event" && s["event"].asStr() == "OnSecondChange") {
            int every = s["everySec"].asInt(1);
            if (every < 1) every = 1;

            int count = 0;
            bool hasLoop = false;
            for (size_t i = 0; i < all.size(); i++) {
                count++;
                const std::string t = TypeOf(*all[i]);
                if (t == "repeat" || t == "while") hasLoop = true;
            }
            bool speaksAlways = false;
            for (size_t i = 0; i < blocks.size(); i++) {
                const std::string t = TypeOf(blocks.at(i));
                if (t == "say" || t == "raw" || t == "call" || t == "choice") speaksAlways = true;
            }
            if (every <= 1 && speaksAlways) {
                Add(out, LintLevel::Warn, title + "は毎秒しゃべることになります",
                    "帽子ブロックの「◯秒ごと」を大きくするか、「もし」で条件をつけてください。",
                    idx, NULL);
            } else if (every <= 1 && (hasLoop || count >= 10)) {
                Add(out, LintLevel::Warn, title + "は毎秒これだけ動くので、重くなりがちです",
                    "「◯秒ごと」を 5 秒くらいにすると、体感は変わらずに軽くなります。",
                    idx, NULL);
            }
        }

        for (size_t bi = 0; bi < all.size(); bi++) {
            const JValue& b = *all[bi];
            const std::string type = TypeOf(b);

            // くりかえしの中で細かく待つと、長いスクリプトになって重くなる
            if (type == "repeat" || type == "while") {
                const JValue* tiny = NULL;
                std::vector<const JValue*> inner;
                WalkInsideOnly(b, &inner);
                for (size_t i = 0; i < inner.size(); i++) {
                    const JValue& q = *inner[i];
                    if (TypeOf(q) != "wait") continue;
                    if (q["ms"].type != JType::Num) continue;
                    const double ms = q["ms"].num;
                    if (ms > 0 && ms <= 20) tiny = inner[i];
                }
                if (tiny) {
                    Add(out, LintLevel::Warn,
                        title + "の「くりかえし」の中で、" + NumToText((*tiny)["ms"].num)
                        + " ミリ秒だけ待っています",
                        "待ちの数だけスクリプトが長くなります。回数を減らすか、"
                        "待ちを長くしてください。", idx, tiny);
                }
            }

            if (!FindBlockFor(type, b["op"].asStr())) {
                Add(out, LintLevel::Error,
                    title + "に、知らないブロック（" + type + "）が入っています",
                    "新しい版のなしスタジオで作られたファイルかもしれません。", idx, &b);
                continue;
            }

            if (type == "say") {
                if (IsEmptyText(b["text"])) {
                    Add(out, LintLevel::Warn, title + "に、なにも言わないセリフがあります",
                        "文章を入れるか、ブロックを削除してください。", idx, &b);
                }
            } else if (type == "raw") {
                if (IsEmptyText(b["text"])) {
                    Add(out, LintLevel::Warn,
                        title + "に、からっぽのさくらスクリプトがあります", "", idx, &b);
                }
            } else if (type == "communicate") {
                if (IsEmptyText(b["to"])) {
                    Add(out, LintLevel::Error,
                        title + "に、話しかける相手が空のブロックがあります",
                        "相手のゴースト名（本体の名前）を入れてください。"
                        "「話しかけてきた相手」ブロックを入れると、言われた相手に返せます。",
                        idx, &b);
                }
                if (IsEmptyText(b["text"])) {
                    Add(out, LintLevel::Warn,
                        title + "に、なにも言わずに話しかけるブロックがあります",
                        "文章がないと、相手には届きません。", idx, &b);
                }
            } else if (type == "ask") {
                const std::string into = b["into"].asStr();
                if (IsEmptyText(b["into"])) {
                    Add(out, LintLevel::Error,
                        title + "に、答えの入れ先が空の「たずねる」ブロックがあります",
                        "入れる変数を決めてください。決めないと、たずねても答えを使えません。",
                        idx, &b);
                } else if (!HasName(varNames, into)) {
                    Add(out, LintLevel::Error,
                        "変数「" + into + "」がありません（" + title + "）",
                        "変数タブで作るか、別の変数にえらび直してください。", idx, &b);
                } else if (into.find(',') != std::string::npos
                           || into.find('[') != std::string::npos
                           || into.find(']') != std::string::npos) {
                    Add(out, LintLevel::Error,
                        "変数「" + into + "」の名前は「たずねる」には使えません",
                        "カンマや角かっこが入っていると、SSP への命令が壊れます。"
                        "名前を変えてください。", idx, &b);
                }
            } else if (type == "call_group") {
                if (IsEmptyText(b["group"])) {
                    Add(out, LintLevel::Error,
                        title + "の「どれかよぶ」で、まとまりの名前が空です",
                        "ランダムトークの帽子ブロックで付けた「まとまり」の名前を"
                        "入れてください。", idx, &b);
                } else if (b["group"].type == JType::Str
                           && !HasName(groups, Trim(b["group"].str))) {
                    Add(out, LintLevel::Error,
                        "「" + b["group"].str + "」というまとまりのトークがありません（"
                        + title + "）",
                        "ランダムトークの帽子ブロックの「まとまり」に、"
                        "同じ名前を入れてください。", idx, &b);
                }
            } else if (type == "later") {
                if (IsEmptyText(b["name"])) {
                    Add(out, LintLevel::Error,
                        title + "の「◯秒後によぶ」で、行き先がえらばれていません", "", idx, &b);
                } else if (b["name"].type == JType::Str && !HasName(callable, b["name"].str)) {
                    Add(out, LintLevel::Error,
                        "「" + b["name"].str + "」というトークがありません（" + title + "）",
                        "名前を変えたときは、よび出し側も直してください。", idx, &b);
                }
            } else if (type == "raise") {
                if (IsEmptyText(b["event"])) {
                    Add(out, LintLevel::Warn,
                        title + "に、イベント名が空の「自分で起こす」ブロックがあります",
                        "OnBoot のようなイベント名を入れてください。", idx, &b);
                }
            } else if (type == "open_browser") {
                if (IsEmptyText(b["url"])) {
                    Add(out, LintLevel::Warn,
                        title + "に、ひらく先が空のブロックがあります", "", idx, &b);
                }
            } else if (type == "change_ghost") {
                if (IsEmptyText(b["name"])) {
                    Add(out, LintLevel::Warn,
                        title + "に、交代する相手が空のブロックがあります",
                        "ゴースト名か random を入れてください。", idx, &b);
                }
            } else if (type == "saori_call") {
                if (IsEmptyText(b["file"])) {
                    Add(out, LintLevel::Error,
                        title + "に、よぶファイルが空の SAORI ブロックがあります",
                        "ゴーストのフォルダに置いた DLL の名前を入れてください。", idx, &b);
                }
                const std::string into = b["into"].asStr();
                if (IsEmptyText(b["into"])) {
                    Add(out, LintLevel::Warn,
                        title + "に、答えの入れ先が空の SAORI ブロックがあります",
                        "答えを入れる変数を決めておかないと、届いた答えを使えません。"
                        "（「SAORI の答えがとどいたとき」でも受け取れます）", idx, &b);
                } else if (!HasName(varNames, into)) {
                    Add(out, LintLevel::Error,
                        "変数「" + into + "」がありません（" + title + "）",
                        "変数タブで作るか、別の変数にえらび直してください。", idx, &b);
                }
            } else if (type == "update") {
                // 「更新のありか」が無いと、SSP は更新しようがない
                if (IsEmptyText(project["meta"]["homeUrl"])) {
                    Add(out, LintLevel::Warn,
                        title + "に、ネットワーク更新のブロックがあります",
                        "ゴーストの設定の「更新のありか」が空です。"
                        "入れておかないと、SSP は更新できません。", idx, &b);
                }
            } else if (type == "var" || type == "set" || type == "change") {
                const std::string name = b["name"].asStr();
                if (IsEmptyText(b["name"])) {
                    Add(out, LintLevel::Error,
                        title + "に、変数をえらんでいないブロックがあります",
                        "変数タブで変数を作ってから、えらんでください。", idx, &b);
                } else if (!HasName(varNames, name)) {
                    Add(out, LintLevel::Error,
                        "変数「" + name + "」がありません（" + title + "）",
                        "変数タブで作るか、別の変数にえらび直してください。", idx, &b);
                }
            } else if (type == "call") {
                const std::string name = b["name"].asStr();
                if (IsEmptyText(b["name"])) {
                    Add(out, LintLevel::Error,
                        title + "の「よぶ」ブロックで、行き先がえらばれていません", "", idx, &b);
                } else if (!HasName(callable, name)) {
                    Add(out, LintLevel::Error,
                        "「" + name + "」というトークがありません（" + title + "）",
                        "名前を変えたときは、よび出し側も直してください。", idx, &b);
                }
            } else if (type == "choice") {
                // 欄にブロックが入っているときは、名前ではないので「（ブロック）」と言う
                const std::string target = b["target"].isObj() ? "（ブロック）"
                                                               : b["target"].asStr();
                if (IsEmptyText(b["target"])) {
                    Add(out, LintLevel::Error, title + "の選択肢に、行き先がありません",
                        "えらんでも何も起きません。行き先のトークをえらんでください。",
                        idx, &b);
                } else if (!HasName(callable, target)) {
                    Add(out, LintLevel::Error,
                        "選択肢の行き先「" + target + "」がありません（" + title + "）",
                        "", idx, &b);
                }
                if (IsEmptyText(b["label"])) {
                    Add(out, LintLevel::Warn, title + "に、文字のない選択肢があります",
                        "", idx, &b);
                }
            } else if (type == "link") {
                if (IsEmptyText(b["url"]) || Trim(b["url"].asStr()) == "https://") {
                    Add(out, LintLevel::Warn, title + "のリンクに、URL が入っていません",
                        "", idx, &b);
                }
            } else if (type == "wait") {
                if (b["ms"].asNum(0) > 60000) {
                    Add(out, LintLevel::Warn,
                        title + "に、1 分より長く待つブロックがあります",
                        "長すぎるとゴーストが固まったように見えます。", idx, &b);
                }
            }
        }
    }

    // ---- 設定まわり
    {
        const JValue& st = project["settings"];
        if (!(st["randomTalkEnabled"].type == JType::Bool && !st["randomTalkEnabled"].b)) {
            if (!talkCount) {
                Add(out, LintLevel::Warn,
                    "自動でしゃべる設定なのに、ランダムトークが 1 つもありません",
                    "イベントの「ランダムトーク」をキャンバスに置いてください。", -1, NULL);
            } else if (st["randomTalkInterval"].asNum(0) <= 0) {
                Add(out, LintLevel::Warn,
                    "しゃべる間隔が 0 秒なので、自動ではしゃべりません",
                    "ゴーストタブで秒数を入れてください。", -1, NULL);
            }
        }
    }

    // ---- 使われていない変数
    {
        std::vector<std::string> used;
        for (size_t si = 0; si < scripts.size(); si++) {
            std::vector<const JValue*> all;
            Walk(scripts.at(si), &all);
            for (size_t i = 0; i < all.size(); i++) {
                const JValue& b = *all[i];
                const std::string type = TypeOf(b);
                if ((type == "var" || type == "set" || type == "change")
                    && !b["name"].asStr().empty()) {
                    used.push_back(b["name"].asStr());
                }
                if (type == "saori_call" && !b["into"].asStr().empty()) {
                    used.push_back(b["into"].asStr());
                }
                if (type == "ask" && !b["into"].asStr().empty()) {
                    used.push_back(b["into"].asStr());
                }
            }
        }
        for (size_t i = 0; i < varNames.size(); i++) {
            if (HasName(used, varNames[i])) continue;
            Add(out, LintLevel::Warn,
                "変数「" + varNames[i] + "」はどこでも使われていません",
                "いらなければ変数タブで削除できます。", -1, NULL);
        }
    }

    // ---- まちがいを先に、あとは見つけた順（ならびは変えません）
    std::vector<LintIssue> sorted;
    for (size_t i = 0; i < out->size(); i++) {
        if ((*out)[i].level == LintLevel::Error) sorted.push_back((*out)[i]);
    }
    for (size_t i = 0; i < out->size(); i++) {
        if ((*out)[i].level != LintLevel::Error) sorted.push_back((*out)[i]);
    }
    out->swap(sorted);
}

int CountLintErrors(const std::vector<LintIssue>& list) {
    int n = 0;
    for (size_t i = 0; i < list.size(); i++) if (list[i].level == LintLevel::Error) n++;
    return n;
}

} // namespace w2k
} // namespace nashi
