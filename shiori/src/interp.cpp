#include "interp.h"
#include "util.h"

#include <ctime>
#include <cmath>
#include <cstdio>

namespace nashi {

static const int kMaxSteps = 40000;
static const int kMaxDepth = 48;
static const int kMaxLoop  = 5000;
static const size_t kMaxOut = 32000;
static const size_t kMaxValue = 32000;   // 変数 1 つぶんの長さの上限

// ---------------------------------------------------------------- helpers

// 長すぎる文字は切ります（UTF-8 の途中では切りません）。
// 上限が無いと「V を『V と V をつなげる』にする」を 30 回まわすだけで 1GB になり、
// 32bit の栞はメモリを使いきって、SSP ごと落ちてしまいます。
// ui/js/sim.js の capText と同じ規則にしてください。
std::string CapText(const std::string& s) {
    if (s.size() <= kMaxValue) return s;
    size_t n = kMaxValue;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;   // 続きバイトの手前まで戻す
    return s.substr(0, n);
}

static Value CapValue(const Value& v) {
    if (v.isNum || v.str.size() <= kMaxValue) return v;
    return Value::Str(CapText(v.str));
}

std::string EscapeText(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (size_t i = 0; i < text.size(); i++) {
        char c = text[i];
        if (c == '\\') { out += "\\\\"; }
        else if (c == '\r') { /* skip, handled by \n */ }
        else if (c == '\n') { out += "\\n"; }
        else { out += c; }
    }
    return out;
}

static bool IsNumericStr(const std::string& s) {
    std::string t = Trim(s);
    if (t.empty()) return false;
    char* end = NULL;
    strtod(t.c_str(), &end);
    return end && *end == '\0';
}

// -1 / 0 / +1
static int CompareValues(const Value& a, const Value& b) {
    bool an = a.isNum || IsNumericStr(a.str);
    bool bn = b.isNum || IsNumericStr(b.str);
    if (an && bn) {
        double x = a.asNum(), y = b.asNum();
        if (x < y) return -1;
        if (x > y) return 1;
        return 0;
    }
    std::string x = a.asStr(), y = b.asStr();
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

static void Emit(RunCtx& ctx, const std::string& s) {
    if (ctx.out.size() + s.size() > kMaxOut) return;
    ctx.out += s;
}

static void EmitScope(RunCtx& ctx, int who) {
    if (who < 0) return;
    if (ctx.scope == who) return;
    char buf[24];
    if (who == 0) strcpy_s(buf, "\\0");
    else if (who == 1) strcpy_s(buf, "\\1");
    else sprintf_s(buf, "\\p[%d]", who);
    Emit(ctx, buf);
    ctx.scope = who;
}

static std::string BlockType(const JValue& b) {
    if (!b.isObj()) return std::string();
    std::string t = b["type"].asStr();
    if (t.empty()) t = b["op"].asStr();
    return t;
}

// ---------------------------------------------------------------- expressions

// [ … ] の中に入れる文字を安全にする。
//
// さくらスクリプトのタグは ] で終わるので、中身に ] が混じると、その先が
// 「命令」として読まれます。ここへ入る文字は、他のゴーストから来た言葉
// （OnCommunicate の Reference）や、使う人が入力ボックスに書いたものかも
// しれないので、必ず通してから出します。
// dropComma を立てると、区切りに使う , も落とします（\q の行き先など）。
//
// ui/js/sim.js の safeTag と同じ規則にしてください（一致テストが見ています）。
static std::string TagArg(const std::string& s, bool dropComma) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '[' || c == ']' || c == '\r' || c == '\n') continue;
        if (dropComma && c == ',') continue;
        out += c;
    }
    return out;
}

static Value SysValue(const std::string& key, RunCtx& ctx) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (key == "hour")    return Value::Num(st.wHour);
    if (key == "minute")  return Value::Num(st.wMinute);
    if (key == "second")  return Value::Num(st.wSecond);
    if (key == "year")    return Value::Num(st.wYear);
    if (key == "month")   return Value::Num(st.wMonth);
    if (key == "day")     return Value::Num(st.wDay);
    if (key == "weekday") return Value::Num(st.wDayOfWeek);   // 0=Sunday
    // 話のとっかかりに使う「いまごろ」。ui/js/sim.js と同じ区切りにしてください。
    if (key == "daypart") {
        int h = st.wHour;
        if (h < 5)  return Value::Str("夜");
        if (h < 11) return Value::Str("朝");
        if (h < 17) return Value::Str("昼");
        if (h < 22) return Value::Str("夕方");
        return Value::Str("夜");
    }
    if (key == "season") {
        int m = st.wMonth;
        if (m >= 3 && m <= 5)  return Value::Str("春");
        if (m >= 6 && m <= 8)  return Value::Str("夏");
        if (m >= 9 && m <= 11) return Value::Str("秋");
        return Value::Str("冬");
    }
    if (key == "weekdayname") {
        static const char* kDays[] = { "日", "月", "火", "水", "木", "金", "土" };
        int d = st.wDayOfWeek;
        if (d < 0 || d > 6) d = 0;
        return Value::Str(kDays[d]);
    }
    // ゴースト間通信。OnCommunicate の Reference0 が相手の名前、Reference1 が言われたこと。
    if (key == "commfrom") return Value::Str(ctx.refs.size() > 0 ? ctx.refs[0] : std::string());
    if (key == "commtext") return Value::Str(ctx.refs.size() > 1 ? ctx.refs[1] : std::string());
    if (!ctx.sys) return Value::Num(0);
    if (key == "uptime")    return Value::Num((double)ctx.sys->uptimeSec);
    if (key == "uptimemin") return Value::Num((double)(ctx.sys->uptimeSec / 60));
    if (key == "boots")     return Value::Num(ctx.sys->bootCount);
    if (key == "talks")     return Value::Num(ctx.sys->talkCount);
    if (key == "ghostname") return Value::Str(ctx.sys->ghostName);
    if (key == "shellname") return Value::Str(ctx.sys->shellName);
    if (key == "lasttalk")  return Value::Str(ctx.sys->lastTalk);
    return Value::Num(0);
}

Value EvalExpr(const JValue& node, RunCtx& ctx) {
    if (ctx.steps++ > kMaxSteps) return Value::Num(0);
    switch (node.type) {
        case JType::Null: return Value::Str("");
        case JType::Num:  return Value::Num(node.num);
        case JType::Bool: return Value::Bool(node.b);
        case JType::Str:  return Value::Str(node.str);
        case JType::Arr:  return Value::Str("");
        default: break;
    }

    const std::string type = BlockType(node);
    if (type.empty()) return Value::Str("");

    if (type == "num")  return Value::Num(node["value"].asNum(0));
    if (type == "text") return Value::Str(node["value"].asStr());
    if (type == "bool") return Value::Bool(node["value"].asBool(false));

    if (type == "var") {
        if (!ctx.vars) return Value::Num(0);
        return ctx.vars->get(node["name"].asStr());
    }

    if (type == "ref") {
        int idx = node["index"].asInt(0);
        if (idx >= 0 && (size_t)idx < ctx.refs.size()) return Value::Str(ctx.refs[(size_t)idx]);
        return Value::Str("");
    }

    if (type == "sys") return SysValue(node["key"].asStr(), ctx);

    if (type == "random") {
        int lo = (int)std::floor(EvalExpr(node["min"], ctx).asNum());
        int hi = (int)std::floor(EvalExpr(node["max"], ctx).asNum());
        return Value::Num(RandInt(lo, hi));
    }

    if (type == "arith") {
        double a = EvalExpr(node["a"], ctx).asNum();
        double b = EvalExpr(node["b"], ctx).asNum();
        std::string op = node["op"].asStr("+");
        if (op == "+") return Value::Num(a + b);
        if (op == "-") return Value::Num(a - b);
        if (op == "*") return Value::Num(a * b);
        if (op == "/") return Value::Num(b == 0 ? 0 : a / b);
        if (op == "%") {
            if (b == 0) return Value::Num(0);
            double r = std::fmod(a, b);
            if (r != 0 && ((r < 0) != (b < 0))) r += b;   // Scratch-style modulo
            return Value::Num(r);
        }
        if (op == "min") return Value::Num(a < b ? a : b);
        if (op == "max") return Value::Num(a > b ? a : b);
        return Value::Num(0);
    }

    if (type == "round") {
        double a = EvalExpr(node["a"], ctx).asNum();
        std::string op = node["op"].asStr("round");
        if (op == "floor") return Value::Num(std::floor(a));
        if (op == "ceil")  return Value::Num(std::ceil(a));
        if (op == "abs")   return Value::Num(std::fabs(a));
        return Value::Num(std::floor(a + 0.5));
    }

    if (type == "compare") {
        Value a = EvalExpr(node["a"], ctx);
        Value b = EvalExpr(node["b"], ctx);
        int c = CompareValues(a, b);
        std::string op = node["op"].asStr("=");
        if (op == "=" || op == "==") return Value::Bool(c == 0);
        if (op == "!=" || op == "<>") return Value::Bool(c != 0);
        if (op == "<")  return Value::Bool(c < 0);
        if (op == ">")  return Value::Bool(c > 0);
        if (op == "<=") return Value::Bool(c <= 0);
        if (op == ">=") return Value::Bool(c >= 0);
        return Value::Bool(false);
    }

    if (type == "logic") {
        std::string op = node["op"].asStr("and");
        bool a = EvalExpr(node["a"], ctx).asBool();
        if (op == "and") {
            if (!a) return Value::Bool(false);
            return Value::Bool(EvalExpr(node["b"], ctx).asBool());
        }
        if (a) return Value::Bool(true);
        return Value::Bool(EvalExpr(node["b"], ctx).asBool());
    }

    if (type == "not") return Value::Bool(!EvalExpr(node["a"], ctx).asBool());

    if (type == "join") {
        std::string a = EvalExpr(node["a"], ctx).asStr();
        std::string b = EvalExpr(node["b"], ctx).asStr();
        // 上限までで切る（つなげ続けてメモリを食いつぶさないように）。
        // 先に b を縮めたりせず、つないでから CapText に任せること。
        // 途中で切ると UTF-8 の文字が半分だけ残り、プレビューと答えがズレます。
        return Value::Str(CapText(a + b));
    }

    if (type == "contains") {
        std::string a = EvalExpr(node["a"], ctx).asStr();
        std::string b = EvalExpr(node["b"], ctx).asStr();
        if (b.empty()) return Value::Bool(true);
        return Value::Bool(a.find(b) != std::string::npos);
    }

    if (type == "length") {
        // counts characters, not bytes (UTF-8 lead bytes)
        std::string a = EvalExpr(node["a"], ctx).asStr();
        int n = 0;
        for (size_t i = 0; i < a.size(); i++) {
            if (((unsigned char)a[i] & 0xC0) != 0x80) n++;
        }
        return Value::Num(n);
    }

    // 外部モジュール（SAORI）を呼んで、返ってきた値を使う
    if (type == "saori") {
        if (!ctx.saori) return Value::Str("");
        std::string file = Trim(EvalExpr(node["file"], ctx).asStr());
        if (file.empty()) return Value::Str("");
        std::vector<std::string> args;
        const char* keys[] = { "a", "b", "c" };
        for (int i = 0; i < 3; i++) {
            if (!node.has(keys[i]) || node[keys[i]].isNull()) continue;
            args.push_back(EvalExpr(node[keys[i]], ctx).asStr());
        }
        SaoriResult r = ctx.saori->Execute(file, args);
        if (!r.ok) { Log(r.error); return Value::Str(""); }
        int want = node["value"].asInt(-1);      // -1 なら Result、0 以降なら ValueN
        if (want < 0) return Value::Str(r.result);
        if ((size_t)want < r.values.size()) return Value::Str(r.values[(size_t)want]);
        return Value::Str("");
    }

    if (type == "chance") {   // "N% の確率で"
        double p = EvalExpr(node["a"], ctx).asNum();
        return Value::Bool(RandUnit() * 100.0 < p);
    }

    return Value::Str("");
}

static std::string EvalStr(const JValue& node, RunCtx& ctx) {
    return EvalExpr(node, ctx).asStr();
}

static int EvalInt(const JValue& node, RunCtx& ctx, int def) {
    if (node.isNull()) return def;
    double v = EvalExpr(node, ctx).asNum();
    return (int)(v < 0 ? v - 0.5 : v + 0.5);
}

// ---------------------------------------------------------------- statements

static void RunBlock(const JValue& b, RunCtx& ctx);

void RunBlocks(const JValue& blocks, RunCtx& ctx) {
    if (!blocks.isArr()) return;
    if (ctx.depth > kMaxDepth) return;
    ctx.depth++;
    for (size_t i = 0; i < blocks.size(); i++) {
        if (ctx.stopped || ctx.steps > kMaxSteps) break;
        RunBlock(blocks.at(i), ctx);
    }
    ctx.depth--;
}

void RunScript(const JValue& script, RunCtx& ctx) {
    RunBlocks(script["blocks"], ctx);
}

std::string CloseScript(const std::string& out, bool isCloseEvent) {
    if (out.empty()) return out;
    std::string s = out;
    if (isCloseEvent) {
        if (s.find("\\-") == std::string::npos) s += "\\-";
        return s;
    }
    if (s.find("\\e") == std::string::npos && s.find("\\-") == std::string::npos) s += "\\e";
    return s;
}

static void RunBlock(const JValue& b, RunCtx& ctx) {
    if (!b.isObj()) return;
    if (ctx.steps++ > kMaxSteps) { ctx.stopped = true; return; }
    if (b["disabled"].asBool(false)) return;

    const std::string type = BlockType(b);
    if (type.empty()) return;

    // ---- speech ----------------------------------------------------------
    if (type == "say") {
        int who = EvalInt(b["who"], ctx, 0);
        EmitScope(ctx, who);
        if (b.has("surface") && !b["surface"].isNull()) {
            char buf[32];
            sprintf_s(buf, "\\s[%d]", EvalInt(b["surface"], ctx, 0));
            Emit(ctx, buf);
        }
        Emit(ctx, EscapeText(EvalStr(b["text"], ctx)));
        if (b["nl"].asBool(true)) Emit(ctx, "\\n");
        return;
    }

    if (type == "surface") {
        int who = EvalInt(b["who"], ctx, 0);
        EmitScope(ctx, who);
        char buf[32];
        sprintf_s(buf, "\\s[%d]", EvalInt(b["id"], ctx, 0));
        Emit(ctx, buf);
        return;
    }

    // 3 人目以降のキャラに切りかえる（\p[n]）。0 と 1 は \0 \1 になる。
    if (type == "chara") {
        int id = EvalInt(b["id"], ctx, 0);
        if (id < 0) id = 0;
        EmitScope(ctx, id);
        return;
    }

    if (type == "newline") {
        int n = EvalInt(b["count"], ctx, 1);
        if (n < 1) n = 1;
        if (n > 32) n = 32;
        for (int i = 0; i < n; i++) Emit(ctx, "\\n");
        return;
    }

    if (type == "wait") {
        int ms = EvalInt(b["ms"], ctx, 500);
        if (ms < 0) ms = 0;
        if (ms > 60000) ms = 60000;
        char buf[32];
        sprintf_s(buf, "\\_w[%d]", ms);
        Emit(ctx, buf);
        return;
    }

    if (type == "click_wait") { Emit(ctx, "\\x"); return; }
    if (type == "clear")      { Emit(ctx, "\\c"); return; }
    if (type == "raw")        { Emit(ctx, EvalStr(b["text"], ctx)); return; }

    // SERIKO のアニメーションを再生する（\i[n]）。
    // どんな動きかは shell/master/surfaces.txt に書き出してある。
    if (type == "anim") {
        int id = EvalInt(b["id"], ctx, 0);
        if (id < 0) id = 0;
        char buf[32];
        sprintf_s(buf, "\\i[%d]", id);
        Emit(ctx, buf);
        return;
    }

    if (type == "balloon") {
        char buf[32];
        sprintf_s(buf, "\\b[%d]", EvalInt(b["id"], ctx, 0));
        Emit(ctx, buf);
        return;
    }

    if (type == "sound") {
        std::string f = TagArg(EvalStr(b["file"], ctx), false);
        if (!f.empty()) Emit(ctx, "\\_v[" + f + "]");
        return;
    }

    if (type == "link") {
        std::string url = TagArg(EvalStr(b["url"], ctx), false);
        std::string label = EvalStr(b["label"], ctx);
        if (label.empty()) label = url;
        if (!url.empty()) {
            Emit(ctx, "\\_a[" + url + "]" + TagArg(EscapeText(label), false) + "\\_a");
        }
        return;
    }

    if (type == "choice") {
        std::string label = TagArg(EscapeText(EvalStr(b["label"], ctx)), false);
        std::string target = TagArg(EvalStr(b["target"], ctx), true);
        if (label.empty()) return;
        Emit(ctx, "\\q[" + label + "," + target + "]");
        return;
    }

    // 他のゴーストに話しかける。
    // 画面には普通のセリフとして出て、同時に相手の OnCommunicate に届く。
    // 届け先はレスポンスの Reference0 で指定するので、ここでは覚えておくだけ。
    if (type == "communicate") {
        int who = EvalInt(b["who"], ctx, 0);
        EmitScope(ctx, who);
        Emit(ctx, EscapeText(EvalStr(b["text"], ctx)));
        std::string to = Trim(EvalStr(b["to"], ctx));
        if (!to.empty()) ctx.commTo = to;
        return;
    }

    // さくらスクリプトの中に置ける文字にする（1 行・カンマと括弧なし）
    // \![…] は「,」で区切って「]」で終わるので、そのまま入れると命令が壊れます。
    if (type == "ask" || type == "change_ghost" || type == "open_browser" || type == "raise") {
        // 命令を壊す文字（, [ ] 改行）を落としてから使う
        struct Safe {
            static std::string One(const std::string& s) { return Trim(TagArg(s, true)); }
        };

        // たずねて、答えを変数に入れる。
        // ID を "nashi:変数名" にしておくと、返ってきた OnUserInput で栞が入れられる。
        if (type == "ask") {
            std::string into = Safe::One(b["into"].asStr());
            if (into.empty()) return;
            std::string initial = Safe::One(EvalStr(b["initial"], ctx));
            Emit(ctx, "\\![open,inputbox,nashi:" + into + ",-1," + initial + "]");
            return;
        }
        if (type == "change_ghost") {
            std::string name = Safe::One(EvalStr(b["name"], ctx));
            if (name.empty()) return;
            Emit(ctx, "\\![change,ghost," + name + "]");
            ctx.stopped = true;              // 交代したら、あとのブロックは動かない
            return;
        }
        if (type == "open_browser") {
            std::string url = Safe::One(EvalStr(b["url"], ctx));
            if (url.empty()) return;
            Emit(ctx, "\\![open,browser," + url + "]");
            return;
        }
        if (type == "raise") {
            std::string ev = Safe::One(EvalStr(b["event"], ctx));
            if (ev.empty()) return;
            std::string a = Safe::One(EvalStr(b["a"], ctx));
            std::string line = "\\![raise," + ev;
            if (!a.empty()) line += "," + a;
            Emit(ctx, line + "]");
            return;
        }
    }

    // N 秒後に、名前を付けたトークをよぶ。
    // ここでは覚えるだけで、動かすのは栞（つぎの「ずっとくりかえす」で時間を見る）。
    if (type == "later") {
        std::string name = Trim(EvalStr(b["name"], ctx));
        if (name.empty()) return;
        int sec = EvalInt(b["sec"], ctx, 10);
        if (sec < 1) sec = 1;
        if (sec > 86400) sec = 86400;               // 1 日より先は受けない
        if (ctx.later.size() >= 32) return;         // ためすぎない
        LaterCall one;
        one.afterSec = sec;
        one.name = name;
        ctx.later.push_back(one);
        return;
    }

    // 外部モジュール（SAORI）を、答えを待たずに呼ぶ。
    // 時間のかかる SAORI をそのまま呼ぶと SSP ごと止まるので、こちらは別のスレッドで動かす。
    // 答えが届くと変数に入り、つぎの「ずっとくりかえす」で OnSaoriDone が起きる。
    if (type == "saori_call") {
        if (!ctx.saori) return;
        std::string file = Trim(EvalStr(b["file"], ctx));
        if (file.empty()) return;
        std::vector<std::string> args;
        const char* keys[] = { "a", "b", "c" };
        for (int i = 0; i < 3; i++) {
            if (!b.has(keys[i]) || b[keys[i]].isNull()) continue;
            args.push_back(EvalStr(b[keys[i]], ctx));
        }
        ctx.saori->ExecuteAsync(file, args, Trim(b["into"].asStr()), b["value"].asInt(-1));
        return;
    }

    // ネットワーク更新をはじめる。あとは SSP がやって、
    // 結果は OnUpdateComplete / OnUpdateFailure で返ってくる。
    if (type == "update") { Emit(ctx, "\\![updatebymyself]"); return; }

    if (type == "end")   { Emit(ctx, "\\e"); ctx.stopped = true; return; }
    if (type == "stop")  { ctx.stopped = true; return; }
    if (type == "close") { Emit(ctx, "\\-"); ctx.closed = true; ctx.stopped = true; return; }

    // ---- variables -------------------------------------------------------
    if (type == "set") {
        if (!ctx.vars) return;
        std::string name = b["name"].asStr();
        if (!name.empty()) ctx.vars->set(name, CapValue(EvalExpr(b["value"], ctx)));
        return;
    }

    if (type == "change") {
        if (!ctx.vars) return;
        std::string name = b["name"].asStr();
        if (name.empty()) return;
        double cur = ctx.vars->get(name).asNum();
        ctx.vars->set(name, Value::Num(cur + EvalExpr(b["value"], ctx).asNum()));
        return;
    }

    if (type == "talk_interval") {
        if (!ctx.vars) return;
        int sec = EvalInt(b["sec"], ctx, 180);
        if (sec < 0) sec = 0;
        ctx.vars->set("@talkInterval", Value::Num(sec));
        return;
    }

    // ---- control ---------------------------------------------------------
    if (type == "if") {
        if (EvalExpr(b["cond"], ctx).asBool()) RunBlocks(b["then"], ctx);
        return;
    }

    if (type == "if_else") {
        if (EvalExpr(b["cond"], ctx).asBool()) RunBlocks(b["then"], ctx);
        else RunBlocks(b["else"], ctx);
        return;
    }

    if (type == "repeat") {
        int n = EvalInt(b["count"], ctx, 1);
        if (n > kMaxLoop) n = kMaxLoop;
        for (int i = 0; i < n && !ctx.stopped; i++) {
            if (ctx.steps > kMaxSteps) break;
            RunBlocks(b["body"], ctx);
        }
        return;
    }

    if (type == "while") {
        int guard = 0;
        while (!ctx.stopped && guard++ < kMaxLoop) {
            if (ctx.steps > kMaxSteps) break;
            if (!EvalExpr(b["cond"], ctx).asBool()) break;
            RunBlocks(b["body"], ctx);
        }
        return;
    }

    if (type == "random_one") {
        const JValue& branches = b["branches"];
        if (!branches.isArr() || branches.size() == 0) return;
        size_t pick = (size_t)RandInt(0, (int)branches.size() - 1);
        RunBlocks(branches.at(pick), ctx);
        return;
    }

    // 同じ「まとまり」に入れたランダムトークから、1 つえらんでよぶ。
    // 「朝のトーク」「季節のトーク」のように分けておけるようにするためのものです。
    if (type == "call_group") {
        if (!ctx.prog) return;
        std::string group = Trim(EvalStr(b["group"], ctx));
        std::vector<const JValue*> list = ctx.prog->talksInGroup(group);
        if (list.empty()) return;

        // 直前に出したものは、ほかに候補があるなら避ける
        if (list.size() > 1 && ctx.sys && !ctx.sys->lastTalk.empty()) {
            for (size_t i = 0; i < list.size(); i++) {
                if ((*list[i])["id"].asStr() == ctx.sys->lastTalk) {
                    list.erase(list.begin() + (long)i);
                    break;
                }
            }
        }
        // えらばれやすさ（weight）ぶんのくじを引く
        double total = 0;
        for (size_t i = 0; i < list.size(); i++) {
            double w = (*list[i])["weight"].asNum(1);
            if (w > 0) total += w;
        }
        const JValue* pick = NULL;
        if (total <= 0) {
            pick = list[(size_t)RandInt(0, (int)list.size() - 1)];
        } else {
            double r = RandUnit() * total;
            for (size_t i = 0; i < list.size(); i++) {
                double w = (*list[i])["weight"].asNum(1);
                if (w <= 0) continue;
                r -= w;
                if (r <= 0) { pick = list[i]; break; }
            }
            if (!pick) pick = list[list.size() - 1];
        }

        for (size_t i = 0; i < ctx.stack.size(); i++) {
            if (ctx.stack[i] == pick) return;            // すでに動いている
        }
        if (ctx.stack.size() > 16) return;
        ctx.stack.push_back(pick);
        RunBlocks((*pick)["blocks"], ctx);
        ctx.stack.pop_back();
        return;
    }

    if (type == "call") {
        if (!ctx.prog) return;
        std::string name = b["name"].asStr();
        if (name.empty()) name = EvalStr(b["target"], ctx);
        const JValue* fn = ctx.prog->functionByName(name);
        if (!fn) fn = ctx.prog->scriptById(name);
        if (!fn) return;
        for (size_t i = 0; i < ctx.stack.size(); i++) {
            if (ctx.stack[i] == fn) return;             // already running: stop recursion
        }
        if (ctx.stack.size() > 16) return;
        ctx.stack.push_back(fn);
        RunBlocks((*fn)["blocks"], ctx);
        ctx.stack.pop_back();
        return;
    }
}

} // namespace nashi
