#include "webreq.h"

namespace nashi {

static int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string UrlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') { out += ' '; continue; }
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = HexVal(s[i + 1]), lo = HexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += (char)(unsigned char)(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

void ParseTarget(const std::string& target, HttpRequest& req) {
    size_t q = target.find('?');
    req.path = UrlDecode(q == std::string::npos ? target : target.substr(0, q));
    if (q == std::string::npos) return;

    std::string qs = target.substr(q + 1);
    size_t pos = 0;
    while (pos < qs.size()) {
        size_t amp = qs.find('&', pos);
        std::string pair = qs.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            req.query[UrlDecode(pair.substr(0, eq))] = UrlDecode(pair.substr(eq + 1));
        } else if (!pair.empty()) {
            req.query[UrlDecode(pair)] = "";
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
}

const char* StatusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

} // namespace nashi
