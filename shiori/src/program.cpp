#include "program.h"
#include "util.h"

namespace nashi {

// ---------------------------------------------------------------- Value

double Value::asNum() const { return isNum ? num : StrToNum(str); }

std::string Value::asStr() const { return isNum ? NumToStr(num) : str; }

bool Value::asBool() const {
    if (isNum) return num != 0.0;
    return !str.empty() && str != "0" && !IEquals(str, "false");
}

// ---------------------------------------------------------------- Vars

Value Vars::get(const std::string& name) const {
    for (size_t i = 0; i < items_.size(); i++) {
        if (items_[i].first == name) return items_[i].second;
    }
    return Value::Num(0);
}

void Vars::set(const std::string& name, const Value& v) {
    for (size_t i = 0; i < items_.size(); i++) {
        if (items_[i].first == name) { items_[i].second = v; return; }
    }
    if (items_.size() < 4096) items_.push_back(std::make_pair(name, v));
}

bool Vars::exists(const std::string& name) const {
    for (size_t i = 0; i < items_.size(); i++) {
        if (items_[i].first == name) return true;
    }
    return false;
}

static Value ValueFromJson(const JValue& v) {
    if (v.type == JType::Str) return Value::Str(v.str);
    if (v.type == JType::Bool) return Value::Num(v.b ? 1 : 0);
    return Value::Num(v.asNum(0));
}

void Vars::fromJson(const JValue& v) {
    if (v.isArr()) {
        // [{ "name": "x", "value": 0 }, ...]
        for (size_t i = 0; i < v.size(); i++) {
            const JValue& e = v.at(i);
            std::string name = e["name"].asStr();
            if (name.empty()) continue;
            set(name, ValueFromJson(e["value"]));
        }
    } else if (v.isObj()) {
        // { "x": 0, ... }
        for (size_t i = 0; i < v.obj.size(); i++) {
            set(v.obj[i].first, ValueFromJson(v.obj[i].second));
        }
    }
}

JValue Vars::toJson() const {
    JValue o = JValue::makeObj();
    for (size_t i = 0; i < items_.size(); i++) {
        const Value& v = items_[i].second;
        o.set(items_[i].first, v.isNum ? JValue::makeNum(v.num) : JValue::makeStr(v.str));
    }
    return o;
}

// ---------------------------------------------------------------- Program

bool Program::Load(const std::wstring& dir) {
    loaded_ = false;
    error_.clear();
    root_ = JValue();

    const wchar_t* names[] = { L"ghost.json", L"nashi.json" };
    std::string text;
    bool found = false;
    for (int i = 0; i < 2; i++) {
        if (ReadTextFile(dir + names[i], text)) { found = true; break; }
    }
    if (!found) {
        error_ = "ghost.json not found";
        return false;
    }
    std::string err;
    if (!JsonParse(text, root_, err)) {
        error_ = "ghost.json parse error: " + err;
        return false;
    }
    if (!root_.isObj()) {
        error_ = "ghost.json must contain an object";
        return false;
    }
    loaded_ = true;
    return true;
}

std::vector<const JValue*> Program::eventScripts(const std::string& eventName) const {
    std::vector<const JValue*> out;
    const JValue& scripts = root_["scripts"];
    for (size_t i = 0; i < scripts.size(); i++) {
        const JValue& s = scripts.at(i);
        if (s["kind"].asStr("event") != "event") continue;
        if (s["disabled"].asBool(false)) continue;
        if (s["event"].asStr() == eventName) out.push_back(&scripts.arr[i]);
    }
    return out;
}

bool Program::hasEvent(const std::string& eventName) const {
    return !eventScripts(eventName).empty();
}

std::vector<const JValue*> Program::talkScripts() const {
    std::vector<const JValue*> out;
    const JValue& scripts = root_["scripts"];
    for (size_t i = 0; i < scripts.size(); i++) {
        const JValue& s = scripts.at(i);
        if (s["kind"].asStr() != "talk") continue;
        if (s["disabled"].asBool(false)) continue;
        out.push_back(&scripts.arr[i]);
    }
    return out;
}

std::vector<const JValue*> Program::talksInGroup(const std::string& group) const {
    std::vector<const JValue*> out;
    if (group.empty()) return out;
    const JValue& scripts = root_["scripts"];
    for (size_t i = 0; i < scripts.size(); i++) {
        const JValue& s = scripts.at(i);
        if (s["kind"].asStr() != "talk") continue;
        if (s["disabled"].asBool(false)) continue;
        if (s["group"].asStr() != group) continue;
        out.push_back(&scripts.arr[i]);
    }
    return out;
}

const JValue* Program::functionByName(const std::string& name) const {
    if (name.empty()) return NULL;
    const JValue& scripts = root_["scripts"];
    for (size_t i = 0; i < scripts.size(); i++) {
        const JValue& s = scripts.at(i);
        const std::string kind = s["kind"].asStr();
        if (kind != "function" && kind != "talk") continue;
        if (s["name"].asStr() == name) return &scripts.arr[i];
    }
    return NULL;
}

const JValue* Program::scriptById(const std::string& id) const {
    if (id.empty()) return NULL;
    const JValue& scripts = root_["scripts"];
    for (size_t i = 0; i < scripts.size(); i++) {
        if (scripts.at(i)["id"].asStr() == id) return &scripts.arr[i];
    }
    return NULL;
}

} // namespace nashi
