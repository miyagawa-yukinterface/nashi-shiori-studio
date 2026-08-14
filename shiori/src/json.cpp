#include "json.h"
#include "util.h"

#include <cstdio>
#include <cmath>

namespace nashi {

const JValue& JValue::Null() {
    static JValue nullValue;
    return nullValue;
}

const JValue& JValue::operator[](const char* key) const {
    if (type != JType::Obj) return Null();
    for (size_t i = 0; i < obj.size(); i++) {
        if (obj[i].first == key) return obj[i].second;
    }
    return Null();
}

const JValue& JValue::at(size_t i) const {
    if (type != JType::Arr || i >= arr.size()) return Null();
    return arr[i];
}

bool JValue::has(const char* key) const {
    if (type != JType::Obj) return false;
    for (size_t i = 0; i < obj.size(); i++) {
        if (obj[i].first == key) return true;
    }
    return false;
}

std::string JValue::asStr(const char* def) const {
    switch (type) {
        case JType::Str:  return str;
        case JType::Num:  return NumToStr(num);
        case JType::Bool: return b ? "1" : "0";
        default:          return def;
    }
}

double JValue::asNum(double def) const {
    switch (type) {
        case JType::Num:  return num;
        case JType::Str:  return StrToNum(str);
        case JType::Bool: return b ? 1.0 : 0.0;
        default:          return def;
    }
}

int JValue::asInt(int def) const {
    if (type == JType::Null) return def;
    double v = asNum((double)def);
    return (int)(v < 0 ? v - 0.5 : v + 0.5);
}

bool JValue::asBool(bool def) const {
    switch (type) {
        case JType::Bool: return b;
        case JType::Num:  return num != 0.0;
        case JType::Str:  return !str.empty() && str != "0" && !IEquals(str, "false");
        default:          return def;
    }
}

void JValue::set(const std::string& key, const JValue& v) {
    if (type != JType::Obj) { type = JType::Obj; obj.clear(); }
    for (size_t i = 0; i < obj.size(); i++) {
        if (obj[i].first == key) { obj[i].second = v; return; }
    }
    obj.push_back(std::make_pair(key, v));
}

JValue JValue::makeObj()  { JValue v; v.type = JType::Obj;  return v; }
JValue JValue::makeArr()  { JValue v; v.type = JType::Arr;  return v; }
JValue JValue::makeStr(const std::string& s) { JValue v; v.type = JType::Str; v.str = s; return v; }
JValue JValue::makeNum(double n) { JValue v; v.type = JType::Num; v.num = n; return v; }
JValue JValue::makeBool(bool bb)  { JValue v; v.type = JType::Bool; v.b = bb; return v; }

// ------------------------------------------------------------------ writer

static void DumpString(const std::string& s, std::string& out) {
    out += '"';
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    sprintf_s(buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    out += '"';
}

static void DumpValue(const JValue& v, std::string& out, int indent, int level) {
    std::string pad, padIn;
    if (indent > 0) {
        pad.assign((size_t)(indent * level), ' ');
        padIn.assign((size_t)(indent * (level + 1)), ' ');
    }
    const char* nl = indent > 0 ? "\n" : "";
    switch (v.type) {
        case JType::Null: out += "null"; break;
        case JType::Bool: out += v.b ? "true" : "false"; break;
        case JType::Num:  out += NumToStr(v.num); break;
        case JType::Str:  DumpString(v.str, out); break;
        case JType::Arr:
            if (v.arr.empty()) { out += "[]"; break; }
            out += "["; out += nl;
            for (size_t i = 0; i < v.arr.size(); i++) {
                out += padIn;
                DumpValue(v.arr[i], out, indent, level + 1);
                if (i + 1 < v.arr.size()) out += ",";
                out += nl;
            }
            out += pad; out += "]";
            break;
        case JType::Obj:
            if (v.obj.empty()) { out += "{}"; break; }
            out += "{"; out += nl;
            for (size_t i = 0; i < v.obj.size(); i++) {
                out += padIn;
                DumpString(v.obj[i].first, out);
                out += indent > 0 ? ": " : ":";
                DumpValue(v.obj[i].second, out, indent, level + 1);
                if (i + 1 < v.obj.size()) out += ",";
                out += nl;
            }
            out += pad; out += "}";
            break;
    }
}

std::string JValue::dump(int indent) const {
    std::string out;
    DumpValue(*this, out, indent, 0);
    return out;
}

// ------------------------------------------------------------------ parser

namespace {

struct Parser {
    const std::string& s;
    size_t i;
    std::string err;

    explicit Parser(const std::string& src) : s(src), i(0) {}

    void skipWs() {
        while (i < s.size()) {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { i++; continue; }
            // tolerate // and /* */ comments so hand-edited files still load
            if (c == '/' && i + 1 < s.size()) {
                if (s[i + 1] == '/') {
                    i += 2;
                    while (i < s.size() && s[i] != '\n') i++;
                    continue;
                }
                if (s[i + 1] == '*') {
                    i += 2;
                    while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) i++;
                    i = (i + 1 < s.size()) ? i + 2 : s.size();
                    continue;
                }
            }
            break;
        }
    }

    bool fail(const char* msg) {
        if (err.empty()) {
            char buf[128];
            sprintf_s(buf, "%s at offset %u", msg, (unsigned)i);
            err = buf;
        }
        return false;
    }

    static void AppendUtf8(unsigned cp, std::string& out) {
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    bool parseHex4(unsigned& out) {
        if (i + 4 > s.size()) return fail("truncated \\u escape");
        out = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i++];
            unsigned d;
            if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
            else return fail("bad hex digit");
            out = out * 16 + d;
        }
        return true;
    }

    bool parseString(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("expected string");
        i++;
        out.clear();
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!parseHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() &&
                        s[i] == '\\' && s[i + 1] == 'u') {
                        size_t save = i;
                        i += 2;
                        unsigned lo = 0;
                        if (parseHex4(lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            i = save;
                        }
                    }
                    AppendUtf8(cp, out);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseValue(JValue& v, int depth) {
        if (depth > 64) return fail("too deep");
        skipWs();
        if (i >= s.size()) return fail("unexpected end");
        char c = s[i];
        if (c == '{') {
            i++;
            v.type = JType::Obj;
            v.obj.clear();
            skipWs();
            if (i < s.size() && s[i] == '}') { i++; return true; }
            for (;;) {
                skipWs();
                std::string key;
                if (!parseString(key)) return false;
                skipWs();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                i++;
                JValue child;
                if (!parseValue(child, depth + 1)) return false;
                v.obj.push_back(std::make_pair(key, child));
                skipWs();
                if (i < s.size() && s[i] == ',') { i++; skipWs(); if (i < s.size() && s[i] == '}') { i++; return true; } continue; }
                if (i < s.size() && s[i] == '}') { i++; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            i++;
            v.type = JType::Arr;
            v.arr.clear();
            skipWs();
            if (i < s.size() && s[i] == ']') { i++; return true; }
            for (;;) {
                JValue child;
                if (!parseValue(child, depth + 1)) return false;
                v.arr.push_back(child);
                skipWs();
                if (i < s.size() && s[i] == ',') { i++; skipWs(); if (i < s.size() && s[i] == ']') { i++; return true; } continue; }
                if (i < s.size() && s[i] == ']') { i++; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            v.type = JType::Str;
            return parseString(v.str);
        }
        if (!s.compare(i, 4, "true"))  { i += 4; v.type = JType::Bool; v.b = true;  return true; }
        if (!s.compare(i, 5, "false")) { i += 5; v.type = JType::Bool; v.b = false; return true; }
        if (!s.compare(i, 4, "null"))  { i += 4; v.type = JType::Null; return true; }
        if (c == '-' || c == '+' || (c >= '0' && c <= '9')) {
            size_t start = i;
            if (s[i] == '-' || s[i] == '+') i++;
            while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' ||
                                    s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+')) i++;
            v.type = JType::Num;
            v.num = StrToNum(s.substr(start, i - start));
            return true;
        }
        return fail("unexpected character");
    }
};

} // namespace

bool JsonParse(const std::string& utf8, JValue& out, std::string& err) {
    Parser p(utf8);
    out = JValue();
    if (!p.parseValue(out, 0)) { err = p.err; return false; }
    p.skipWs();
    return true;
}

} // namespace nashi
