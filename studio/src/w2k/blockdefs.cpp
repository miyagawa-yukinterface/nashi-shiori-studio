#include "blockdefs.h"

#include <cstring>

namespace nashi {
namespace w2k {

// 表の中身（tools\gen-blockdefs.js が ui\js\blocks.js から作ります）
#include "blockdefs_gen.h"

namespace {
bool Same(const char* a, const std::string& b) {
    return a && b.size() == std::strlen(a) && std::memcmp(a, b.c_str(), b.size()) == 0;
}
}

const BlockDef* FindBlock(const std::string& key) {
    for (int i = 0; i < kBlockCount; i++) {
        if (Same(kBlocks[i].key, key)) return &kBlocks[i];
    }
    return NULL;
}

const BlockDef* FindBlockFor(const std::string& type, const std::string& op) {
    // 同じ type でも op ちがいで見た目が変わるもの（arith / compare / logic）。
    // ui/js/blocks.js の N.defKey と同じ決めかたにしてあります。
    if (!op.empty() && (type == "arith" || type == "compare" || type == "logic")) {
        const BlockDef* d = FindBlock(type + "#" + op);
        if (d) return d;
    }
    return FindBlock(type);
}

const CategoryDef* FindCategory(const std::string& id) {
    for (int i = 0; i < kCategoryCount; i++) {
        if (Same(kCategories[i].id, id)) return &kCategories[i];
    }
    return NULL;
}

const ArgDef* BlockArgs(const BlockDef& d, int* count) {
    if (count) *count = d.argCount;
    return d.argCount ? &kArgs[d.argStart] : NULL;
}

const PartDef* BlockParts(const BlockDef& d, int* count) {
    if (count) *count = d.partCount;
    return d.partCount ? &kParts[d.partStart] : NULL;
}

const SubDef* BlockSubs(const BlockDef& d, int* count) {
    if (count) *count = d.subCount;
    return d.subCount ? &kSubs[d.subStart] : NULL;
}

const FixedDef* BlockFixed(const BlockDef& d, int* count) {
    if (count) *count = d.fixedCount;
    return d.fixedCount ? &kFixed[d.fixedStart] : NULL;
}

const OptionDef* ArgOptions(const ArgDef& a, int* count) {
    if (count) *count = a.optionCount;
    return a.optionCount ? &kOptions[a.optionStart] : NULL;
}

const ArgDef* FindArg(const BlockDef& d, const std::string& name) {
    for (int i = 0; i < d.argCount; i++) {
        if (Same(kArgs[d.argStart + i].name, name)) return &kArgs[d.argStart + i];
    }
    return NULL;
}

const PaletteRow* Palette(int* count) {
    if (count) *count = kPaletteCount;
    return kPalette;
}

const BlockDef* AllBlocks(int* count) {
    if (count) *count = kBlockCount;
    return kBlocks;
}

const CategoryDef* AllCategories(int* count) {
    if (count) *count = kCategoryCount;
    return kCategories;
}

} // namespace w2k
} // namespace nashi
