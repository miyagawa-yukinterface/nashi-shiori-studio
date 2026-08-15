// テスト用のおそい SAORI
//
//   Argument0 … 何ミリ秒ねてから返すか（省略すると 300）
//   Argument1 … 返す言葉（省略すると "おそいこたえ"）
//
// 「待たない呼び出し」の確かめに使います。ゴーストが終わったあとに答えが返る、
// という順番をわざと作れるので、栞が消えたメモリを触らないかを見られます。
// 本物の SAORI と同じ形（load / unload / request）です。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <cstdio>
#include <cstdlib>

static HGLOBAL Dup(const std::string& s, long* len) {
    HGLOBAL h = GlobalAlloc(GMEM_FIXED, s.size() ? s.size() : 1);
    if (!h) { if (len) *len = 0; return NULL; }
    if (s.size()) memcpy(h, s.c_str(), s.size());
    if (len) *len = (long)s.size();
    return h;
}

// "Argument0: 500" のような行から中身を取り出す
static std::string HeaderValue(const std::string& req, const std::string& key) {
    size_t pos = 0;
    while (pos <= req.size()) {
        size_t e = req.find('\n', pos);
        std::string line = (e == std::string::npos) ? req.substr(pos) : req.substr(pos, e - pos);
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        size_t c = line.find(':');
        if (c != std::string::npos && line.substr(0, c) == key) {
            std::string v = line.substr(c + 1);
            while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(0, 1);
            return v;
        }
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    return std::string();
}

extern "C" {

__declspec(dllexport) BOOL __cdecl load(HGLOBAL h, long) {
    if (h) GlobalFree(h);
    return TRUE;
}

__declspec(dllexport) BOOL __cdecl unload() {
    return TRUE;
}

__declspec(dllexport) HGLOBAL __cdecl request(HGLOBAL h, long* len) {
    std::string req;
    if (h) {
        long n = (len && *len > 0) ? *len : 0;
        if (n > 0) req.assign((const char*)h, (size_t)n);
        GlobalFree(h);
    }

    int ms = atoi(HeaderValue(req, "Argument0").c_str());
    if (ms <= 0) ms = 300;
    if (ms > 60000) ms = 60000;
    std::string word = HeaderValue(req, "Argument1");
    if (word.empty()) word = "おそいこたえ";

    Sleep((DWORD)ms);

    std::string res = "SAORI/1.0 200 OK\r\n";
    res += "Charset: UTF-8\r\n";
    res += "Result: " + word + "\r\n";
    res += "Value0: " + word + word + "\r\n";
    res += "\r\n";
    return Dup(res, len);
}

} // extern "C"
