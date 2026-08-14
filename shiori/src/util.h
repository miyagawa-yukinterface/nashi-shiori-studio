// nashi SHIORI - utility helpers
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

namespace nashi {

// ---- encoding -------------------------------------------------------------
std::wstring MbToWide(const std::string& s, UINT cp);
std::string  WideToMb(const std::wstring& s, UINT cp);

inline std::wstring Utf8ToWide(const std::string& s) { return MbToWide(s, CP_UTF8); }
inline std::string  WideToUtf8(const std::wstring& s) { return WideToMb(s, CP_UTF8); }
inline std::wstring AcpToWide(const std::string& s) { return MbToWide(s, CP_ACP); }

// true when the byte sequence is well-formed UTF-8
bool LooksUtf8(const std::string& s);
// normalise arbitrary bytes (UTF-8 / CP932) into UTF-8, BOM stripped
std::string ToUtf8(const std::string& raw);

// ---- files ----------------------------------------------------------------
bool ReadTextFile(const std::wstring& path, std::string& outUtf8);
bool WriteTextFile(const std::wstring& path, const std::string& utf8);
bool FileExists(const std::wstring& path);

// ---- strings --------------------------------------------------------------
std::string Trim(const std::string& s);
bool IEquals(const std::string& a, const std::string& b);
bool StartsWith(const std::string& s, const std::string& prefix);
std::string NumToStr(double v);
double StrToNum(const std::string& s);

// ---- random ---------------------------------------------------------------
void SeedRandom();
int  RandInt(int lo, int hi);          // inclusive
double RandUnit();                     // [0,1)

// ---- logging (debug builds / nashi_debug.txt beside the dll) --------------
void SetLogPath(const std::wstring& dir);
void Log(const std::string& msg);

} // namespace nashi
