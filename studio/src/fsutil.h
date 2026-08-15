// なしスタジオ - ファイル操作まわり
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

namespace nashi {

std::wstring ExePath();
std::wstring ExeDir();
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
std::wstring ParentDir(const std::wstring& path);
std::wstring FileNameOf(const std::wstring& path);

bool PathExists(const std::wstring& path);
bool IsDirectory(const std::wstring& path);
bool EnsureDir(const std::wstring& path);          // mkdir -p 相当

bool ReadBinaryFile(const std::wstring& path, std::string& out);
bool WriteBinaryFile(const std::wstring& path, const std::string& data);
bool DeleteFileIfExists(const std::wstring& path);

struct FileInfo {
    std::wstring name;
    unsigned long long size;
    FILETIME modified;
};
std::vector<FileInfo> ListFiles(const std::wstring& dir, const std::wstring& pattern);
std::string FileTimeToIso(const FILETIME& ft);

// dir の中のファイルを、下のフォルダもふくめて集める。
// 返るのは dir からの相対パス（区切りは \）。
std::vector<std::wstring> ListFilesDeep(const std::wstring& dir);

// MD5 を小文字の 16 進で返す（ネットワーク更新の照合表に使う）
std::string Md5Hex(const std::string& data);

// PNG をえらぶダイアログを出す（複数可）。キャンセルなら空。
std::vector<std::wstring> PickPngFiles();

// 環境変数の展開（%USERPROFILE% など）
std::wstring ExpandVars(const std::wstring& s);

} // namespace nashi
