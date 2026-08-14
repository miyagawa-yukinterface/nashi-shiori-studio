// nashi SHIORI - the block program (ghost.json) and its runtime state
#pragma once

#include "json.h"

#include <string>
#include <vector>

namespace nashi {

// ---------------------------------------------------------------- Value
// A dynamically typed value; everything is either a number or a string.
struct Value {
    bool isNum;
    double num;
    std::string str;

    Value() : isNum(true), num(0.0) {}
    static Value Num(double v) { Value x; x.isNum = true; x.num = v; return x; }
    static Value Str(const std::string& s) { Value x; x.isNum = false; x.str = s; return x; }
    static Value Bool(bool b) { return Num(b ? 1.0 : 0.0); }

    double asNum() const;
    std::string asStr() const;
    bool asBool() const;
};

// ---------------------------------------------------------------- Vars
class Vars {
public:
    Value get(const std::string& name) const;
    void  set(const std::string& name, const Value& v);
    bool  exists(const std::string& name) const;
    void  clear() { items_.clear(); }

    void   fromJson(const JValue& v);   // { "name": value, ... } or [ {name,value}, ... ]
    JValue toJson() const;

    size_t size() const { return items_.size(); }
    const std::pair<std::string, Value>& at(size_t i) const { return items_[i]; }

private:
    std::vector<std::pair<std::string, Value> > items_;
};

// ---------------------------------------------------------------- Program
class Program {
public:
    // Loads <dir>ghost.json (falls back to nashi.json). Returns false when absent/broken.
    bool Load(const std::wstring& dir);
    bool loaded() const { return loaded_; }
    const std::string& error() const { return error_; }

    const JValue& root() const { return root_; }
    const JValue& meta() const { return root_["meta"]; }
    const JValue& settings() const { return root_["settings"]; }
    const JValue& initialVars() const { return root_["variables"]; }

    // scripts
    std::vector<const JValue*> eventScripts(const std::string& eventName) const;
    std::vector<const JValue*> talkScripts() const;
    const JValue* functionByName(const std::string& name) const;
    const JValue* scriptById(const std::string& id) const;
    bool hasEvent(const std::string& eventName) const;

private:
    JValue root_;
    bool loaded_ = false;
    std::string error_;
};

} // namespace nashi
