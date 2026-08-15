// なしスタジオ - ゴースト書き出し（フォルダ / .nar）
#pragma once

#include "json.h"

#include <string>
#include <vector>

namespace nashi {

struct OutFile {
    std::string name;    // ゴーストのルートからの相対パス（/ 区切り）
    std::string data;
    bool shell;          // シェル画像などは既存があれば残す
};

struct ExportResult {
    bool ok = false;
    std::string error;
    std::wstring root;
    std::string folder;
    std::vector<std::string> written;
    std::vector<std::string> skipped;
};

std::string SafeFolderName(const std::string& name, const char* fallback);

// 栞が読む ghost.json の中身（エディタ専用の項目を落としたもの）
std::string RuntimeProgramJson(const JValue& project);

// 仮シェルの立ち絵を1枚だけ描いて PNG で返す（エディタのリアルタイム表示用）。
// surfaceId: 0/1/2 = さくら（通常・笑顔・驚き）、10/11/12 = うにゅう。
// それ以外の id は同じ系統の「通常」に丸める。空文字なら既定色。
std::string RenderSurfacePng(int surfaceId, const std::string& hairHex, const std::string& clothHex);

// プロジェクトでその番号に割り当てた画像ファイルのパス（無ければ空）
std::string ShellImagePath(const JValue& project, int surfaceId);

std::vector<OutFile> BuildGhostFiles(const JValue& project, const std::string& dll,
                                     bool includeShell, std::string* folderOut);

// ネットワーク更新の照合表（updates2.dau）を、そのフォルダの中身から作る
std::string BuildUpdatesDau(const std::wstring& root);

ExportResult ExportToDir(const JValue& project, const std::wstring& outDir, const std::string& dll,
                         bool includeShell, bool overwriteShell);

ExportResult ExportToNar(const JValue& project, const std::wstring& outDir, const std::string& dll,
                         bool includeShell);

} // namespace nashi
