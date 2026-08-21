#include "layout.h"

namespace nashi {
namespace w2k {

namespace {

// 並べている途中の持ちもの
struct Ctx {
    const Metrics* m;
    const TextMeasurer* tm;
    Layout* out;
};

int AddPiece(Ctx& c, PieceKind kind, int depth) {
    Piece p;
    p.kind = kind;
    p.depth = depth;
    c.out->pieces.push_back(p);
    return (int)c.out->pieces.size() - 1;
}

bool IsFilledSlot(const JValue& v) {
    return v.isObj() && !v["type"].asStr().empty();
}

int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 前に宣言（ブロックの中にブロックが入るので、たがいに呼びます）
int LayoutBlock(Ctx& c, const JValue& block, int x, int y, int depth,
                const BlockDef* forceDef = NULL);
int LayoutStackInto(Ctx& c, const JValue& blocks, int x, int y, int depth,
                    int* heightOut, int* widthOut = NULL);

/** 入力欄・えらぶ欄を 1 つ置く。返すのは pieces の位置。 */
int LayoutSlot(Ctx& c, const JValue& owner, const ArgDef& arg, int x, int y, int depth) {
    const Metrics& m = *c.m;
    const JValue& value = owner[arg.name];

    int idx = AddPiece(c, PieceKind::Slot, depth);
    c.out->pieces[idx].argName = arg.name;
    c.out->pieces[idx].argKind = arg.kind;
    c.out->pieces[idx].argMode = arg.mode;
    c.out->pieces[idx].boolSlot = (arg.mode == ArgMode::Bool);
    c.out->pieces[idx].node = &owner;

    // 中にブロックがはまっている
    if (IsFilledSlot(value)) {
        int child = LayoutBlock(c, value, x, y, depth + 1);
        Piece& slot = c.out->pieces[idx];
        slot.x = x;
        slot.y = y;
        slot.w = c.out->pieces[child].w;
        slot.h = c.out->pieces[child].h;
        slot.firstChild = child;
        slot.childCount = 1;
        return idx;
    }

    // 空の六角スロット
    if (arg.mode == ArgMode::Bool) {
        Piece& slot = c.out->pieces[idx];
        slot.x = x;
        slot.y = y;
        slot.w = m.slotMinW + 8;
        slot.h = m.slotHeight;
        return idx;
    }

    std::string text = SlotText(owner, arg);
    int minW = arg.wide ? m.slotWideMinW : m.slotMinW;
    int w = Clamp(c.tm->Width(text) + m.slotPadX * 2, minW, m.slotMaxW);
    // えらぶ欄は、右に▼のぶんを足す
    if (arg.kind != ArgKind::Input) w += 12;

    Piece& slot = c.out->pieces[idx];
    slot.x = x;
    slot.y = y;
    slot.w = w;
    slot.h = m.slotHeight;
    slot.text = text;
    return idx;
}

/** ブロック 1 つ。返すのは pieces の位置。x, y はその左上。
 *  帽子は ghost.json に "type" を持たない（kind と event で決まる）ので、
 *  そういうときは forceDef で定義を渡します。 */
int LayoutBlock(Ctx& c, const JValue& block, int x, int y, int depth,
                const BlockDef* forceDef) {
    const Metrics& m = *c.m;

    std::string type = block["type"].asStr();
    std::string op = block["op"].asStr();
    const BlockDef* def = forceDef ? forceDef : FindBlockFor(type, op);

    int idx = AddPiece(c, PieceKind::Block, depth);
    c.out->pieces[idx].def = def;
    c.out->pieces[idx].node = &block;
    c.out->pieces[idx].firstChild = (int)c.out->pieces.size();

    if (!def) {
        // 知らないブロック。名前だけ出して、場所は取っておく。
        int lbl = AddPiece(c, PieceKind::Label, depth + 1);
        c.out->pieces[lbl].text = type.empty() ? "?" : type;
        c.out->pieces[lbl].x = x + m.padX;
        c.out->pieces[lbl].y = y + m.padY;
        c.out->pieces[lbl].w = c.tm->Width(c.out->pieces[lbl].text);
        c.out->pieces[lbl].h = m.textHeight;

        Piece& b = c.out->pieces[idx];
        b.x = x;
        b.y = y;
        b.w = c.out->pieces[lbl].w + m.padX * 2;
        b.h = m.minHeight;
        b.childCount = 1;
        return idx;
    }

    const bool isHat = def->hat;
    const int headTop = y + (isHat ? m.hatHeight : 0);

    // 丸いブロックと六角のブロックは、左右がとがっているぶん余白を広めに取ります
    // （そうしないと、中の欄がとがったところにはみ出します）。
    const bool pointy = (def->shape == Shape::Reporter || def->shape == Shape::Boolean);
    const int padX = pointy ? m.padX + 6 : m.padX;

    // ---- 見出しの行（文字と欄をよこに並べる）
    int cursor = x + padX;
    int rowH = m.textHeight;
    int partCount = 0;
    const PartDef* parts = BlockParts(*def, &partCount);
    std::vector<int> rowPieces;

    for (int i = 0; i < partCount; i++) {
        if (parts[i].isArg) {
            const ArgDef* arg = FindArg(*def, parts[i].text);
            if (!arg) continue;
            int s = LayoutSlot(c, block, *arg, cursor, headTop, depth + 1);
            rowPieces.push_back(s);
            cursor += c.out->pieces[s].w + m.gap;
            if (c.out->pieces[s].h > rowH) rowH = c.out->pieces[s].h;
        } else {
            std::string text = parts[i].text;
            if (text.empty()) continue;
            int lbl = AddPiece(c, PieceKind::Label, depth + 1);
            c.out->pieces[lbl].text = text;
            c.out->pieces[lbl].x = cursor;
            c.out->pieces[lbl].y = headTop;
            c.out->pieces[lbl].w = c.tm->Width(text);
            c.out->pieces[lbl].h = m.textHeight;
            rowPieces.push_back(lbl);
            cursor += c.out->pieces[lbl].w + m.gap;
        }
    }
    if (!rowPieces.empty()) cursor -= m.gap;   // 最後の余白は要らない

    const int headH = rowH + m.padY * 2;
    // 行の中で、背の低いものを縦まんなかにそろえる
    for (size_t i = 0; i < rowPieces.size(); i++) {
        Piece& p = c.out->pieces[rowPieces[i]];
        p.y = headTop + m.padY + (rowH - p.h) / 2;
    }

    int width = (cursor - x) + padX;
    if (width < m.minHeight * 2) width = m.minHeight * 2;
    int height = (isHat ? m.hatHeight : 0) + (headH < m.minHeight ? m.minHeight : headH);

    // ---- 中に入る腕（もし〜なら、など）
    int subCount = 0;
    const SubDef* subs = BlockSubs(*def, &subCount);
    std::vector<int> armPieces;
    for (int i = 0; i < subCount; i++) {
        int armTop = y + height;
        int arm = AddPiece(c, PieceKind::Arm, depth + 1);
        c.out->pieces[arm].subKey = subs[i].key;
        c.out->pieces[arm].node = &block;

        int innerH = 0, innerW = 0;
        int first = LayoutStackInto(c, block[subs[i].key], x + m.armIndent,
                                    armTop, depth + 2, &innerH, &innerW);
        if (innerH < m.armMinHeight) innerH = m.armMinHeight;
        // 中身が広ければ、外側もそれに合わせて広げる（CSS がやっていたぶん）
        if (m.armIndent + innerW > width) width = m.armIndent + innerW;

        Piece& a = c.out->pieces[arm];
        a.x = x + m.armIndent;
        a.y = armTop;
        a.w = width - m.armIndent;    // width はこのあと広がることがあるので、下でそろえます
        a.h = innerH;
        armPieces.push_back(arm);
        a.firstChild = first;
        a.childCount = (first >= 0) ? 1 : 0;

        height += innerH;

        // 腕のあいだ・下の桟。「そうでなければ」は文字も出す。
        if (subs[i].label && subs[i].label[0]) {
            int lbl = AddPiece(c, PieceKind::Label, depth + 1);
            c.out->pieces[lbl].text = subs[i].label;
            c.out->pieces[lbl].x = x + m.padX;
            c.out->pieces[lbl].y = y + height + (m.armFooter - m.textHeight) / 2;
            c.out->pieces[lbl].w = c.tm->Width(subs[i].label);
            c.out->pieces[lbl].h = m.textHeight;
            height += m.textHeight + m.padY;
        } else {
            height += m.armFooter;
        }
    }
    // 腕の幅を、最後に決まった外側の幅にそろえる
    for (size_t i = 0; i < armPieces.size(); i++) {
        c.out->pieces[armPieces[i]].w = width - m.armIndent;
    }

    Piece& b = c.out->pieces[idx];
    b.x = x;
    b.y = y;
    b.w = width;
    b.h = height;
    b.childCount = (int)c.out->pieces.size() - b.firstChild;
    return idx;
}

/** ブロックの列を下へ並べる。いちばん上の pieces の位置を返す（空なら -1）。 */
int LayoutStackInto(Ctx& c, const JValue& blocks, int x, int y, int depth,
                    int* heightOut, int* widthOut) {
    int first = -1;
    int cy = y, w = 0;
    for (size_t i = 0; i < blocks.size(); i++) {
        const JValue& b = blocks.at(i);
        if (!b.isObj()) continue;
        int idx = LayoutBlock(c, b, x, cy, depth);
        if (first < 0) first = idx;
        cy += c.out->pieces[idx].h;
        if (c.out->pieces[idx].w > w) w = c.out->pieces[idx].w;
    }
    if (heightOut) *heightOut = cy - y;
    if (widthOut) *widthOut = w;
    return first;
}

void Finish(Layout* out, const Metrics& m) {
    // 下の出っぱりは箱より下に出るので、その分を見こんでおきます
    // （そうしないと、いちばん下のブロックの足が切れます）。
    int w = 0, h = m.notchH;
    for (size_t i = 0; i < out->pieces.size(); i++) {
        const Piece& p = out->pieces[i];
        if (p.x + p.w > w) w = p.x + p.w;
        if (p.y + p.h > h) h = p.y + p.h;
    }
    out->width = w;
    out->height = h + m.notchH;
}

} // namespace

// --------------------------------------------------------------------- 公開

std::string SlotText(const JValue& owner, const ArgDef& arg) {
    const JValue& v = owner[arg.name];
    std::string raw;
    if (v.isNull()) raw = arg.defValue ? arg.defValue : "";
    else if (v.type == JType::Str) raw = v.str;
    else if (v.type == JType::Num) raw = v.asStr();
    else if (v.type == JType::Bool) raw = v.b ? "1" : "0";

    // えらぶ欄は、値ではなく見出しを出す
    if (arg.kind == ArgKind::Dropdown) {
        int n = 0;
        const OptionDef* opts = ArgOptions(arg, &n);
        for (int i = 0; i < n; i++) {
            if (raw == opts[i].value) return opts[i].label;
        }
    }
    return raw;
}

void LayoutStack(const JValue& blocks, int x, int y,
                 const Metrics& m, const TextMeasurer& tm, Layout* out) {
    out->pieces.clear();
    Ctx c = { &m, &tm, out };
    LayoutStackInto(c, blocks, x, y, 0, NULL);
    Finish(out, m);
}

void LayoutScript(const JValue& script, int x, int y,
                  const Metrics& m, const TextMeasurer& tm, Layout* out) {
    out->pieces.clear();
    Ctx c = { &m, &tm, out };

    // 帽子。kind に合う帽子の定義をえらぶ。
    std::string kind = script["kind"].asStr("event");
    std::string key = "@event";
    if (kind == "talk") key = "@talk";
    else if (kind == "function") key = "@function";
    else {
        // マウス系や「◯秒ごと」は、形の変わる帽子を使う
        if (script.has("everySec")) key = "@event.every";
        else if (script.has("filter")) {
            const JValue& f = script["filter"];
            if (f.has("from") || f.has("contains")) key = "@event.comm";
            else key = "@event.touch";
        }
    }
    const BlockDef* hat = FindBlock(key);

    int cy = y;
    if (hat) {
        // 帽子の引数（event 名・えらばれやすさ など）は script の直下にあるので、
        // 持ちものとして script をそのまま渡します。
        int idx = LayoutBlock(c, script, x, cy, 0, hat);
        cy = c.out->pieces[idx].y + c.out->pieces[idx].h;
    }
    LayoutStackInto(c, script["blocks"], x, cy, 0, NULL);
    Finish(out, m);
}

void LayoutOne(const JValue& block, int x, int y,
               const Metrics& m, const TextMeasurer& tm, Layout* out) {
    out->pieces.clear();
    Ctx c = { &m, &tm, out };
    LayoutBlock(c, block, x, y, 0);
    Finish(out, m);
}

// -------------------------------------------------------------------- 当たり
int Layout::HitTest(int x, int y) const {
    int best = -1;
    int bestDepth = -1;
    for (size_t i = 0; i < pieces.size(); i++) {
        const Piece& p = pieces[i];
        if (x < p.x || x >= p.x + p.w || y < p.y || y >= p.y + p.h) continue;
        if (p.depth > bestDepth) { bestDepth = p.depth; best = (int)i; }
    }
    return best;
}

int Layout::BlockAt(int piece) const {
    if (piece < 0 || piece >= (int)pieces.size()) return -1;
    if (pieces[piece].kind == PieceKind::Block) return piece;
    // 自分を含んでいる、いちばん内側のブロックを探す
    const Piece& t = pieces[piece];
    int best = -1, bestDepth = -1;
    for (size_t i = 0; i < pieces.size(); i++) {
        const Piece& p = pieces[i];
        if (p.kind != PieceKind::Block) continue;
        if (t.x < p.x || t.x >= p.x + p.w || t.y < p.y || t.y >= p.y + p.h) continue;
        if (p.depth > bestDepth) { bestDepth = p.depth; best = (int)i; }
    }
    return best;
}

} // namespace w2k
} // namespace nashi
