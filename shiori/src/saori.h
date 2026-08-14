// nashi SHIORI - SAORI/1.0 クライアント（外部モジュールの呼び出し）
//
// SAORI は伺かの「部品 DLL」です。ゴーストのフォルダに置いた .dll を読み込んで、
// 引数を渡して結果を受け取ります。栞と同じ 32bit である必要があります。
#pragma once

#include <string>
#include <vector>
#include <map>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nashi {

struct SaoriResult {
    bool ok = false;
    int status = 0;                     // 200 / 204 / 400 など
    std::string result;                 // Result: の中身
    std::vector<std::string> values;    // Value0: 以降
    std::string error;                  // 読み込めなかったときの説明
};

// 読み込んだ SAORI を覚えておき、終了時にまとめて解放する。
// 同じ DLL を何度呼んでも、読み込みは 1 回だけ。
class Saori {
public:
    // baseDir はゴーストのフォルダ（DLL の相対パスの起点）
    void SetBaseDir(const std::wstring& dir) { dir_ = dir; }

    // file は "kawari.dll" のような相対パス（.. は使えません）
    SaoriResult Execute(const std::string& file, const std::vector<std::string>& args);

    void UnloadAll();
    ~Saori() { UnloadAll(); }

private:
    struct Module {
        HMODULE dll = NULL;
        void* request = NULL;
        void* unload = NULL;
        bool ready = false;
    };
    Module* Get(const std::string& file, std::string& err);

    std::wstring dir_;
    std::map<std::string, Module> mods_;
};

} // namespace nashi
