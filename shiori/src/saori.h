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

// 答えを待たない呼び出しの結果。主スレッドが TakeDone で受け取ります。
struct SaoriDone {
    std::string file;     // よんだファイル
    std::string into;     // 答えを入れる変数の名前（空なら入れない）
    std::string value;    // 答え（呼べなかったときは空）
};

// 読み込んだ SAORI を覚えておき、終了時にまとめて解放する。
// 同じ DLL を何度呼んでも、読み込みは 1 回だけ。
//
// SAORI の DLL は「同時に 2 か所から呼ばれる」ことを想定していないものが多いので、
// Execute は（待たない呼び出しもふくめて）ひとつずつ順番に行います。
class Saori {
public:
    Saori();
    ~Saori();

    // baseDir はゴーストのフォルダ（DLL の相対パスの起点）
    void SetBaseDir(const std::wstring& dir) { dir_ = dir; }

    // file は "kawari.dll" のような相対パス（.. は使えません）
    SaoriResult Execute(const std::string& file, const std::vector<std::string>& args);

    // 答えを待たずに呼ぶ。別のスレッドで動くので、SSP は止まりません。
    // valueIndex は -1 なら Result、0 以降なら ValueN を取り出します。
    // 走らせられなかった（同時に走りすぎ・名前が空）ときは false。
    bool ExecuteAsync(const std::string& file, const std::vector<std::string>& args,
                      const std::string& into, int valueIndex);

    // 届いた答えを取り出す（主スレッドから。取り出したぶんは消えます）
    void TakeDone(std::vector<SaoriDone>& out);
    // まだ走っている本数
    int Running();

    void UnloadAll();

    static const int kMaxJobs = 4;        // 同時に走らせる上限

private:
    struct Module {
        HMODULE dll = NULL;
        void* request = NULL;
        void* unload = NULL;
        bool ready = false;
    };
    struct Job {
        Saori* self;
        std::string file, into;
        std::vector<std::string> args;
        int valueIndex;
    };
    Module* Get(const std::string& file, std::string& err);
    static DWORD WINAPI ThreadMain(void* param);
    bool WaitAll(DWORD ms);               // 走っているものが終わるまで待つ

    std::wstring dir_;
    std::map<std::string, Module> mods_;

    CRITICAL_SECTION execCs_;             // DLL の呼び出しをひとつずつにする
    CRITICAL_SECTION doneCs_;             // 届いた答えと、走っている本数
    std::vector<SaoriDone> done_;
    std::vector<HANDLE> threads_;
    int running_;
};

} // namespace nashi
