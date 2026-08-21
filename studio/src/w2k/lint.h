// なしスタジオ（ネイティブ版）- あぶないところ探し（チェック）
//
// **ここにも GDI が出てきません。** ghost.json を見て、気になるところを
// ならべて返すだけです。出しかたは panel.cpp と window.cpp が受けもちます。
//
// 中身は ui\js\lint.js を移したものです。WebView2 版を外すまでのあいだ、
// studio\test\lint.js が「同じ ghost.json で、同じことを言うか」を見張ります。
#pragma once

#include "../../../shiori/src/json.h"

#include <string>
#include <vector>

namespace nashi {
namespace w2k {

enum class LintLevel {
    Warn,      // 気になる
    Error,     // 動かない・たぶん間違い
};

struct LintIssue {
    LintLevel level = LintLevel::Warn;
    std::string message;
    std::string hint;
    int scriptIndex = -1;          // どのかたまりか（-1 ならプロジェクト全体のこと）
    const JValue* block = NULL;    // どのブロックか（NULL ならかたまり全体）
};

/** ぜんぶ見て、気になるところをならべます。エラーが先、あとは見つけた順。 */
void LintProject(const JValue& project, std::vector<LintIssue>* out);

/** その中の「まちがい」の数。 */
int CountLintErrors(const std::vector<LintIssue>& list);

} // namespace w2k
} // namespace nashi
