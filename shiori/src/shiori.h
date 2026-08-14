// nashi SHIORI - SHIORI/3.0 protocol handling and event dispatch
#pragma once

#include "program.h"
#include "interp.h"

#include <string>
#include <map>
#include <vector>

namespace nashi {

extern const char* kShioriName;
extern const char* kShioriVersion;
extern const char* kShioriCraftman;

struct ShioriRequest {
    std::string method;                              // GET / NOTIFY
    std::string id;                                  // ID header
    std::vector<std::string> refs;                   // Reference0..N
    std::map<std::string, std::string> headers;      // everything, lower-cased keys
    bool utf8 = true;
};

class Shiori {
public:
    bool Load(const std::wstring& moduleDir);
    void Unload();
    // rawRequest is the exact bytes handed over by the baseware.
    std::string Request(const std::string& rawRequest);

private:
    std::string Dispatch(const ShioriRequest& req);
    std::string RunHandlers(const std::string& eventId, const ShioriRequest& req, bool* found);
    std::string PlayRandomTalk(const ShioriRequest& req);
    std::string BuiltinDefault(const std::string& eventId, const ShioriRequest& req);
    void MaybeReload();
    void LoadSave();
    void SaveSave();
    bool Matches(const JValue& script, const ShioriRequest& req) const;
    std::string RunOne(const JValue& script, const ShioriRequest& req);
    int TalkInterval() const;
    int NoRepeatCount(size_t talkCount) const;
    void RememberTalk(const std::string& id);

    std::wstring dir_;
    Program prog_;
    Vars vars_;
    SysInfo sys_;
    bool ready_ = false;
    long secondsSinceTalk_ = 0;
    long lastSaveSec_ = 0;
    unsigned long long progStamp_ = 0;
    unsigned long lastCheckTick_ = 0;
    bool dirty_ = false;

    // 同じイベントで直前に選んだブロック（続けて同じ反応にならないように）
    std::map<std::string, std::string> lastEventPick_;

    // なで判定（OnMouseMove を数えて OnNadeNade を作る）
    std::string strokeKey_;
    int strokeCount_ = 0;
    unsigned long strokeTick_ = 0;
};

// exposed for the test host
bool ParseRequest(const std::string& utf8, ShioriRequest& out);

} // namespace nashi
