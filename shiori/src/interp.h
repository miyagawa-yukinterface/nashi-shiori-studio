// nashi SHIORI - block interpreter (blocks -> SakuraScript)
#pragma once

#include "program.h"
#include "saori.h"

#include <string>
#include <vector>

namespace nashi {

struct SysInfo {
    int bootCount = 0;      // how many times this ghost has started
    int talkCount = 0;      // random talks played in this session
    long uptimeSec = 0;     // seconds since this session started
    std::string ghostName;
    std::string shellName;
    std::string lastTalk;   // id of the talk played last
    std::vector<std::string> recentTalks;  // newest first; kept to avoid repeats
};

// 「N 秒後によぶ」の予約。栞がためておき、時間が来たら動かします。
struct LaterCall {
    int afterSec;
    std::string name;
};

struct RunCtx {
    Program* prog = NULL;
    Vars* vars = NULL;
    SysInfo* sys = NULL;
    Saori* saori = NULL;               // 外部モジュールの呼び出し口
    std::vector<std::string> refs;   // Reference0..N of the current event

    std::string out;
    std::string commTo;              // 話しかける相手（レスポンスの Reference0 になる）
    std::vector<LaterCall> later;    // 「N 秒後によぶ」の予約
    int scope = -1;                  // last character scope written into out
    int steps = 0;
    int depth = 0;
    bool stopped = false;            // \e or "stop" block reached
    bool closed = false;             // \- emitted
    std::vector<const JValue*> stack; // guards recursive "call"
};

// Escapes plain text for SakuraScript (\\ and newlines -> \n tag).
std::string EscapeText(const std::string& text);

// Runs a stack of blocks, appending SakuraScript to ctx.out.
void RunBlocks(const JValue& blocks, RunCtx& ctx);

// Runs one whole script object ({..., "blocks":[...]}).
void RunScript(const JValue& script, RunCtx& ctx);

// Evaluates an expression node (literal or reporter block).
Value EvalExpr(const JValue& node, RunCtx& ctx);

} // namespace nashi
