#include "preview.h"

#include "../../shiori/src/interp.h"
#include "../../shiori/src/program.h"

namespace nashi {

// scripts[] から id で 1 つ選ぶ
static const JValue* FindScript(const Program& prog, const std::string& id) {
    const JValue& scripts = prog.root()["scripts"];
    for (size_t i = 0; i < scripts.size(); i++) {
        if (scripts.at(i)["id"].asStr() == id) return &scripts.at(i);
    }
    return NULL;
}

PreviewResult RunPreview(const PreviewRequest& req) {
    PreviewResult out;

    Program prog;
    if (!prog.Adopt(req.project)) {
        out.error = prog.error();
        return out;
    }

    const JValue* script = FindScript(prog, req.scriptId);
    if (!script) {
        out.error = "そのかたまりが見つかりません: " + req.scriptId;
        return out;
    }

    // 変数は「プロジェクトの初期値」を敷いてから、続きの変数で上書きします。
    // （動かしている途中の値があれば、そちらが勝ちます）
    Vars vars;
    vars.fromJson(prog.initialVars());
    vars.fromJson(req.vars);

    SysInfo sys;
    sys.uptimeSec = req.uptime;
    sys.bootCount = req.boots;
    sys.talkCount = req.talks;
    sys.ghostName = req.ghostName;
    sys.shellName = req.shellName;
    sys.lastTalk = req.lastTalk;

    RunCtx ctx;
    ctx.prog = &prog;
    ctx.vars = &vars;
    ctx.sys = &sys;
    ctx.saori = NULL;          // プレビューでは外部モジュールを呼びません
    ctx.refs = req.refs;

    RunScript(*script, ctx);

    // 終わりの印は、栞（shiori.cpp）と同じ関数に付けてもらいます
    const std::string ev = (*script)["event"].asStr();
    const bool isClose = (*script)["kind"].asStr() == "event"
                         && (ev == "OnClose" || ev == "OnCloseAll");

    out.ok = true;
    out.script = CloseScript(ctx.out, isClose);
    out.commTo = ctx.commTo;
    out.vars = vars.toJson();
    return out;
}

} // namespace nashi
