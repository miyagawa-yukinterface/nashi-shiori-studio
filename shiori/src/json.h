// nashi SHIORI - a tiny dependency-free JSON reader/writer (UTF-8 in, UTF-8 out)
#pragma once

#include <string>
#include <vector>
#include <utility>

namespace nashi {

enum class JType { Null, Bool, Num, Str, Arr, Obj };

class JValue {
public:
    JType type = JType::Null;
    bool  b = false;
    double num = 0.0;
    std::string str;
    std::vector<JValue> arr;
    std::vector<std::pair<std::string, JValue> > obj;

    static const JValue& Null();

    bool isNull() const { return type == JType::Null; }
    bool isArr()  const { return type == JType::Arr; }
    bool isObj()  const { return type == JType::Obj; }

    // object lookup; returns Null() when missing
    const JValue& operator[](const char* key) const;
    // array index; returns Null() when out of range
    const JValue& at(size_t i) const;
    size_t size() const { return type == JType::Arr ? arr.size() : (type == JType::Obj ? obj.size() : 0); }
    bool has(const char* key) const;

    std::string  asStr(const char* def = "") const;
    double       asNum(double def = 0.0) const;
    int          asInt(int def = 0) const;
    bool         asBool(bool def = false) const;

    // writer
    void set(const std::string& key, const JValue& v);
    static JValue makeObj();
    static JValue makeArr();
    static JValue makeStr(const std::string& s);
    static JValue makeNum(double v);
    static JValue makeBool(bool v);
    std::string dump(int indent = 0) const;
};

// returns false and fills err on syntax error
bool JsonParse(const std::string& utf8, JValue& out, std::string& err);

} // namespace nashi
