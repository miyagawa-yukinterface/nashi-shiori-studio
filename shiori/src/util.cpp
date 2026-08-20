#include "util.h"

#include <cstdio>
#include <cmath>
#include <cstring>

namespace nashi {

// ---------------------------------------------------------------- encoding

std::wstring MbToWide(const std::string& s, UINT cp) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0) return std::wstring();
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(cp, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

std::string WideToMb(const std::wstring& s, UINT cp) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(cp, 0, s.c_str(), (int)s.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return std::string();
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(cp, 0, s.c_str(), (int)s.size(), &out[0], n, NULL, NULL);
    return out;
}

bool LooksUtf8(const std::string& s) {
    const unsigned char* p = (const unsigned char*)s.c_str();
    size_t len = s.size(), i = 0;
    while (i < len) {
        unsigned char c = p[i];
        size_t extra;
        if (c < 0x80)              { i++; continue; }
        else if ((c & 0xE0) == 0xC0) { extra = 1; if (c < 0xC2) return false; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; if (c > 0xF4) return false; }
        else return false;
        if (i + extra >= len) return false;
        for (size_t k = 1; k <= extra; k++) {
            if ((p[i + k] & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

std::string ToUtf8(const std::string& raw) {
    std::string s = raw;
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF) {
        s = s.substr(3);
    }
    if (LooksUtf8(s)) return s;
    return WideToUtf8(MbToWide(s, 932)); // Shift_JIS fallback
}

// ------------------------------------------------------------------- files

bool ReadTextFile(const std::wstring& path, std::string& outUtf8) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > (1 << 24)) { CloseHandle(h); return false; }
    std::string buf((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    if (!buf.empty() && !ReadFile(h, &buf[0], (DWORD)buf.size(), &read, NULL)) {
        CloseHandle(h);
        return false;
    }
    CloseHandle(h);
    buf.resize(read);
    outUtf8 = ToUtf8(buf);
    return true;
}

bool WriteTextFile(const std::wstring& path, const std::string& utf8) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = TRUE;
    if (!utf8.empty()) ok = WriteFile(h, utf8.c_str(), (DWORD)utf8.size(), &written, NULL);
    CloseHandle(h);
    return ok != FALSE;
}

bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// ----------------------------------------------------------------- strings

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (unsigned char)s[b] <= ' ') b++;
    while (e > b && (unsigned char)s[e - 1] <= ' ') e--;
    return s.substr(b, e - b);
}

bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string NumToStr(double v) {
    if (!(v == v)) return "0";                       // NaN
    if (v == 0) return "0";                          // -0 も「0」と書く
    // 整数なら小数点を出さない。
    // ここで long long を通さないのは、小数と 64bit 整数の行き来だけは
    // Windows に入っている msvcrt.dll に無く、自前で書くはめになるためです
    // （shiori/src/tinycrt.cpp のコメントを見てください）。%.0f なら同じ字が出ます。
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        char buf[32];
        sprintf_s(buf, "%.0f", v);
        return buf;
    }
    char buf[64];
    sprintf_s(buf, "%.10g", v);
    return buf;
}

double StrToNum(const std::string& s) {
    std::string t = Trim(s);
    if (t.empty()) return 0.0;
    char* end = NULL;
    double v = strtod(t.c_str(), &end);
    if (end == t.c_str()) return 0.0;
    return v;
}

// ------------------------------------------------------------------ random
//
// std::mt19937 は使いません。<random> は 64bit 整数と小数の行き来を含んでいて、
// そこが Visual Studio の CRT にしか無い関数を呼ぶためです。栞は古い Windows でも
// 読みこめるように CRT を外しているので（shiori/src/tinycrt.cpp）、
// 小さな乱数を自分で持ちます。トークの抽選に使うだけなので、これで十分です。
// xorshift128 — 種が同じなら同じ並びが出ます。

static unsigned g_rng[4] = { 0x9e3779b9u, 0x243f6a88u, 0xb7e15162u, 0xdeadbeefu };

static unsigned NextRand() {
    unsigned t = g_rng[3];
    unsigned s = g_rng[0];
    g_rng[3] = g_rng[2];
    g_rng[2] = g_rng[1];
    g_rng[1] = s;
    t ^= t << 11;
    t ^= t >> 8;
    return g_rng[0] = t ^ s ^ (s >> 19);
}

void SeedRandom() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    unsigned seed = (unsigned)(li.LowPart ^ GetCurrentProcessId() ^ GetTickCount());
    if (!seed) seed = 1;
    g_rng[0] = seed;
    g_rng[1] = seed ^ 0x9e3779b9u;
    g_rng[2] = seed * 1664525u + 1013904223u;
    g_rng[3] = seed ^ 0x85ebca6bu;
    for (int i = 0; i < 16; i++) NextRand();      // 立ちあがりのかたよりをならす
}

int RandInt(int lo, int hi) {
    if (hi < lo) { int t = lo; lo = hi; hi = t; }
    if (lo == hi) return lo;
    unsigned span = (unsigned)(hi - lo) + 1u;
    if (span == 0) return (int)NextRand();        // lo..hi が int の全域のとき
    // 端数のぶんだけ引き直して、どの目も同じ出やすさにする
    unsigned reject = (0xFFFFFFFFu / span) * span;
    unsigned r;
    do { r = NextRand(); } while (r >= reject);
    return lo + (int)(r % span);
}

double RandUnit() {
    // 上位 24 ビットだけ使う（int を経由して、64bit 整数の変換を通さない）
    return (double)(int)(NextRand() >> 8) / 16777216.0;   // 0.0 以上 1.0 未満
}

// ----------------------------------------------------------------- logging

static std::wstring g_logPath;
static bool g_logEnabled = false;

void SetLogPath(const std::wstring& dir) {
    g_logPath = dir + L"nashi_debug.txt";
    // logging only happens when the file already exists (opt-in by the author)
    g_logEnabled = FileExists(g_logPath);
}

void Log(const std::string& msg) {
    if (!g_logEnabled || g_logPath.empty()) return;
    HANDLE h = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char head[64];
    sprintf_s(head, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    DWORD w = 0;
    SetFilePointer(h, 0, NULL, FILE_END);
    WriteFile(h, head, (DWORD)strlen(head), &w, NULL);
    WriteFile(h, msg.c_str(), (DWORD)msg.size(), &w, NULL);
    WriteFile(h, "\r\n", 2, &w, NULL);
    CloseHandle(h);
}

} // namespace nashi
