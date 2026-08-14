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

// ---------------------------------------------------------------- helpers

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
        return Value::Str(a + b);
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
        std::string f = EvalStr(b["file"], ctx);
        if (!f.empty()) Emit(ctx, "\\_v[" + f + "]");
        return;
    }

    if (type == "link") {
        std::string url = EvalStr(b["url"], ctx);
        std::string label = EvalStr(b["label"], ctx);
        if (label.empty()) label = url;
        if (!url.empty()) Emit(ctx, "\\_a[" + url + "]" + EscapeText(label) + "\\_a");
        return;
    }

    if (type == "choice") {
        std::string label = EvalStr(b["label"], ctx);
        std::string target = EvalStr(b["target"], ctx);
        if (label.empty()) return;
        Emit(ctx, "\\q[" + EscapeText(label) + "," + target + "]");
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

    if (type == "end")   { Emit(ctx, "\\e"); ctx.stopped = true; return; }
    if (type == "stop")  { ctx.stopped = true; return; }
    if (type == "close") { Emit(ctx, "\\-"); ctx.closed = true; ctx.stopped = true; return; }

    // ---- variables -------------------------------------------------------
    if (type == "set") {
        if (!ctx.vars) return;
        std::string name = b["name"].asStr();
        if (!name.empty()) ctx.vars->set(name, EvalExpr(b["value"], ctx));
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
