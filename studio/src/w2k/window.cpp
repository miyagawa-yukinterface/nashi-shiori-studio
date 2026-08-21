#include "window.h"

#include <commdlg.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "blockdefs.h"
#include "drag.h"
#include "layout.h"
#include "paint.h"
#include "../image.h"
#include "../../../shiori/src/json.h"
#include "../../../shiori/src/util.h"

namespace nashi {
namespace w2k {

namespace {

const wchar_t* kClass = L"NashiStudioW2kWindow";
const int kPaletteW = 210;      // 左のブロック置き場の幅
const int kPalettePad = 10;
const int kMinCanvas = 320;

// ------------------------------------------------------------------- 持ちもの

/** 左のブロック置き場に、1 つならんでいるもの。 */
struct PaletteItem {
    const BlockDef* def = NULL;
    JValue block;        // 見本（ここから写して持っていきます）
    Layout lay;          // 置き場所の中での場所

    std::string key() const { return (def && def->key) ? def->key : std::string(); }
};

struct Editor {
    HWND hwnd = NULL;          // 窓を出さずに描くときは NULL
    HINSTANCE inst = NULL;
    RECT client = { 0, 0, 1100, 760 };   // 窓の中の大きさ

    std::wstring path;               // いま開いている ghost.json
    JValue project;
    bool dirty = false;

    Metrics metrics;
    PaintTools tools;
    PaintStyle style;

    std::vector<Layout> layouts;     // project["scripts"] と同じならび
    std::vector<PaletteItem> palette;
    int paletteHeight = 0;

    int scrollX = 0, scrollY = 0;    // 編集する面のずらし
    int paletteScroll = 0;
    int canvasW = 0, canvasH = 0;    // 中身ぜんぶが入る大きさ

    // ---- つまんでいるあいだ
    bool dragging = false;
    JValue payload;                  // ならび（配列）か、1 つのブロック
    DragShape dragShape = DragShape::Stack;
    Layout dragLay;                  // つまんでいるものの絵（0,0 起点）
    int grabX = 0, grabY = 0;        // つかんだところ（左上からの差）
    POINT mouse = { 0, 0 };
    std::vector<DropTarget> targets;
    int best = -1;
    bool movingScript = false;
    int moveIndex = -1;              // movingScript のとき、どのかたまりか
    bool overPalette = false;        // 置き場所の上（＝すてる）

    JValue undo;                     // ひとつ前の姿（つまむ直前に取っておく）
    bool hasUndo = false;

    // ---- 欄に打ちこんでいるあいだ
    HWND edit = NULL;                // 欄に重ねて出す、打ちこみ用の小さな窓
    JPath editOwner;                 // どのブロックの
    std::string editArg;             // どの欄か
    bool editNumber = false;         // 数だけの欄か
    bool editing = false;
};

Editor g;

// ------------------------------------------------------------------ こまごま

int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/** 窓の中の大きさを取りなおす。 */
void SyncClient() {
    if (g.hwnd) GetClientRect(g.hwnd, &g.client);
}

RECT CanvasRect() {
    RECT rc = g.client;
    rc.left += kPaletteW;
    if (rc.left > rc.right) rc.left = rc.right;
    return rc;
}

/** 画面の座標 → 編集する面の座標。 */
POINT ToCanvas(int sx, int sy) {
    POINT p;
    p.x = sx - kPaletteW + g.scrollX;
    p.y = sy + g.scrollY;
    return p;
}

bool InPalette(int sx, int sy) {
    return sx >= 0 && sx < kPaletteW && sy >= 0 && sy < g.client.bottom;
}

void UpdateTitle() {
    if (!g.hwnd) return;
    std::wstring title = L"なしスタジオ（ためし版）";
    if (!g.path.empty()) title = g.path + L" - " + title;
    if (g.dirty) title = L"* " + title;
    SetWindowTextW(g.hwnd, title.c_str());
}

void MarkDirty() {
    if (g.dirty) return;
    g.dirty = true;
    UpdateTitle();
}

/** ひとつ前の姿を取っておく（さわる直前に呼びます）。 */
void PushUndo() {
    g.undo = g.project;
    g.hasUndo = true;
}

// ------------------------------------------------------------------ 並べなおし

void RelayoutPalette(HDC dc) {
    GdiMeasurer tm(dc, g.tools.blockFont);
    g.palette.clear();

    int count = 0;
    const PaletteRow* rows = Palette(&count);
    int y = kPalettePad;
    std::string lastCat;

    for (int i = 0; i < count; i++) {
        const BlockDef* def = FindBlock(rows[i].key);
        if (!def) continue;
        if (lastCat != rows[i].category) {
            lastCat = rows[i].category;
            y += (i == 0) ? 0 : 10;   // カテゴリの切れ目はすこし空ける
        }

        PaletteItem item;
        item.def = def;
        item.block = def->hat ? MakeScript(*def) : MakeBlock(*def);
        if (def->hat) LayoutScript(item.block, kPalettePad, y, g.metrics, tm, &item.lay);
        else LayoutOne(item.block, kPalettePad, y, g.metrics, tm, &item.lay);
        y = item.lay.height + kPalettePad;   // lay.height は下端（通し座標）です
        g.palette.push_back(item);
    }
    g.paletteHeight = y;
}

void RelayoutScripts(HDC dc) {
    GdiMeasurer tm(dc, g.tools.blockFont);
    const JValue& scripts = g.project["scripts"];
    g.layouts.assign(scripts.size(), Layout());

    int w = 0, h = 0;
    for (size_t i = 0; i < scripts.size(); i++) {
        const JValue& s = scripts.at(i);
        const int x = s["x"].asInt(60);
        const int y = s["y"].asInt(40 + (int)i * 180);
        LayoutScript(s, x, y, g.metrics, tm, &g.layouts[i]);
        if (g.layouts[i].width > w) w = g.layouts[i].width;
        if (g.layouts[i].height > h) h = g.layouts[i].height;
    }
    g.canvasW = w + 80;
    g.canvasH = h + 80;
}

void Relayout() {
    HDC dc = GetDC(g.hwnd);      // hwnd が NULL なら画面の DC（文字を測るだけなので十分）
    RelayoutPalette(dc);
    RelayoutScripts(dc);
    ReleaseDC(g.hwnd, dc);
}

/** かたまりだけ並べなおす（置き場所はそのまま）。 */
void RelayoutHere() {
    HDC dc = GetDC(g.hwnd);
    RelayoutScripts(dc);
    ReleaseDC(g.hwnd, dc);
}

/** 描きなおしてもらう（窓が無いときは何もしない）。 */
void Repaint() {
    if (g.hwnd) InvalidateRect(g.hwnd, NULL, FALSE);
}

void GrabMouse(bool on) {
    if (!g.hwnd) return;
    if (on) SetCapture(g.hwnd);
    else ReleaseCapture();
}

void Refresh(bool relayout = true) {
    if (relayout) Relayout();
    Repaint();
}

// -------------------------------------------------------------------- 読み書き

/**
 * 読みこんだときの下ごしらえ。
 * 場所の決まっていないかたまりを、たてにならべます（ui\js\model.js と同じ）。
 */
void PlaceScripts(JValue& project) {
    JValue* scripts = JResolve(project, JPath().Then(JStep::Key("scripts")));
    if (!scripts || !scripts->isArr()) return;
    int y = 40;
    for (size_t i = 0; i < scripts->arr.size(); i++) {
        JValue& s = scripts->arr[i];
        if (!s.isObj()) continue;
        if (s["x"].type != JType::Num) s.set("x", JValue::makeNum(60));
        if (s["y"].type != JType::Num) { s.set("y", JValue::makeNum(y)); y += 180; }
        if (!s.has("blocks")) s.set("blocks", JValue::makeArr());
    }
}

bool LoadGhost(const std::wstring& path) {
    std::string text;
    if (!ReadTextFile(path, text)) return false;
    JValue root;
    std::string err;
    if (!JsonParse(text, root, err)) return false;
    if (!root.isObj()) return false;

    PlaceScripts(root);
    g.project = root;
    g.path = path;
    g.dirty = false;
    g.hasUndo = false;
    g.scrollX = g.scrollY = 0;
    Refresh();
    UpdateTitle();
    return true;
}

bool SaveGhost() {
    if (g.path.empty()) return false;
    if (!WriteTextFile(g.path, g.project.dump(2))) return false;
    g.dirty = false;
    UpdateTitle();
    return true;
}

// ---------------------------------------------------------------- つまむ・置く

/** つまんでいるものの、最後が「ここでおわる」ブロックか。 */
bool PayloadEndsWithCap() {
    if (!g.payload.isArr() || g.payload.arr.empty()) return false;
    const JValue& last = g.payload.arr[g.payload.arr.size() - 1];
    const BlockDef* d = FindBlockFor(last["type"].asStr(), last["op"].asStr());
    return d && d->shape == Shape::Cap;
}

/** つまんでいるものの絵を、あらためて並べる。 */
void LayoutPayload() {
    HDC dc = GetDC(g.hwnd);
    GdiMeasurer tm(dc, g.tools.blockFont);
    if (g.payload.isArr()) LayoutStack(g.payload, 0, 0, g.metrics, tm, &g.dragLay);
    else LayoutOne(g.payload, 0, 0, g.metrics, tm, &g.dragLay);
    ReleaseDC(g.hwnd, dc);
}

/** いま置ける場所を集めなおす。 */
void CollectAll() {
    g.targets.clear();
    const bool cap = PayloadEndsWithCap();
    const JValue& scripts = g.project["scripts"];
    for (size_t i = 0; i < scripts.size() && i < g.layouts.size(); i++) {
        std::vector<DropTarget> part;
        JPath sp = JPath().Then(JStep::Key("scripts")).Then(JStep::Index((int)i));
        CollectDropTargets(g.layouts[i], scripts.at(i), sp, g.metrics, g.dragShape, cap, &part);
        for (size_t k = 0; k < part.size(); k++) g.targets.push_back(part[k]);
    }
}

/** マウスの場所から、いちばん近い置き場所をえらびなおす。 */
void UpdateBest() {
    g.overPalette = InPalette(g.mouse.x, g.mouse.y);
    if (g.overPalette || g.movingScript) { g.best = -1; return; }

    POINT c = ToCanvas(g.mouse.x - g.grabX, g.mouse.y - g.grabY);
    if (g.dragShape != DragShape::Stack) {
        // 丸・六角は、左のとがりのあたりでくらべます
        c.x += 12;
        c.y += g.dragLay.height / 2;
    }
    g.best = NearestDropTarget(g.targets, g.dragShape, c.x, c.y);
}

/** つまみはじめる。 */
void BeginDrag(const JValue& payload, DragShape shape, int grabX, int grabY) {
    g.dragging = true;
    g.payload = payload;
    g.dragShape = shape;
    g.grabX = grabX;
    g.grabY = grabY;
    g.movingScript = false;
    LayoutPayload();
    RelayoutHere();          // 切りはなした後の姿で並べなおす
    CollectAll();
    UpdateBest();
    GrabMouse(true);
    Repaint();
}

/** かけらから、そのブロックの ghost.json の場所を割り出す。 */
bool FindPath(int scriptIndex, const JValue* node, JPath* out) {
    if (!node) return false;
    const JValue& script = g.project["scripts"].at((size_t)scriptIndex);
    JPath sp = JPath().Then(JStep::Key("scripts")).Then(JStep::Index(scriptIndex));

    // 場所（ポインタ）から、たどりかたを探します。数が少ないので、じかに歩きます。
    struct Walker {
        const JValue* want;
        bool Find(const JValue& v, const JPath& here, JPath* out) {
            if (&v == want) { *out = here; return true; }
            if (v.isObj()) {
                for (size_t i = 0; i < v.obj.size(); i++) {
                    if (Find(v.obj[i].second, here.Then(JStep::Key(v.obj[i].first)), out)) return true;
                }
            } else if (v.isArr()) {
                for (size_t i = 0; i < v.arr.size(); i++) {
                    if (Find(v.arr[i], here.Then(JStep::Index((int)i)), out)) return true;
                }
            }
            return false;
        }
    };
    Walker w;
    w.want = node;
    return w.Find(script, sp, out);
}

/** その場所の親のならびと、何番目かを返す。 */
bool SplitLast(const JPath& p, JPath* listPath, int* index) {
    if (p.steps.empty() || !p.steps[p.steps.size() - 1].isIndex) return false;
    JPath q;
    for (size_t i = 0; i + 1 < p.steps.size(); i++) q.steps.push_back(p.steps[i]);
    *listPath = q;
    *index = p.steps[p.steps.size() - 1].index;
    return true;
}

// -------------------------------------------------------------- 欄をさわる

const int kEditId = 1001;
const int kMenuBase = 2000;

/**
 * 打ちこまれた文字を、その欄に入れる値にします。
 * 数の欄で、ちゃんと数になっていれば数として入れます（blocks.js と同じ）。
 * 「1.5あ」のように途中までしか数でないものは、文字のままにします。
 */
JValue ValueForField(bool isNumber, const std::string& text) {
    if (isNumber && !text.empty()) {
        char* end = NULL;
        const double num = strtod(text.c_str(), &end);
        if (end && *end == 0) return JValue::makeNum(num);
    }
    return JValue::makeStr(text);
}

/** いま打ちこんでいる中身を、ghost.json に書きこむ。 */
void CommitEdit(bool keep) {
    if (!g.editing) return;
    g.editing = false;

    HWND edit = g.edit;
    g.edit = NULL;
    if (!edit) return;

    if (keep) {
        wchar_t buf[1024];
        const int n = GetWindowTextW(edit, buf, 1024);
        buf[(n >= 0 && n < 1024) ? n : 0] = 0;
        const std::string text = WideToUtf8(buf);

        JValue* owner = JResolve(g.project, g.editOwner);
        if (owner && owner->isObj()) {
            owner->set(g.editArg, ValueForField(g.editNumber, text));
            MarkDirty();
        }
    }
    DestroyWindow(edit);
    Refresh();
}

/** その欄の上に、打ちこみ用の小さな窓を出す。 */
void BeginEdit(const JPath& ownerPath, const ArgDef& arg, const Piece& slot, int ox, int oy) {
    CommitEdit(true);
    if (!g.hwnd) return;

    const JValue* owner = JResolve(g.project, ownerPath);
    if (!owner) return;
    const std::wstring text = Utf8ToWide(SlotText(*owner, arg));

    const int x = slot.x - ox;
    const int y = slot.y - oy;
    g.edit = CreateWindowExW(0, L"EDIT", text.c_str(),
                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                             x, y, slot.w > 40 ? slot.w : 40, slot.h,
                             g.hwnd, (HMENU)(INT_PTR)kEditId, g.inst, NULL);
    if (!g.edit) return;
    SendMessageW(g.edit, WM_SETFONT, (WPARAM)g.tools.slotFont, TRUE);
    SendMessageW(g.edit, EM_SETSEL, 0, -1);
    SetFocus(g.edit);

    g.editOwner = ownerPath;
    g.editArg = arg.name;
    g.editNumber = (arg.mode == ArgMode::Number);
    g.editing = true;
}

/** その欄でえらべるものを集める。値と見出しの組でならべます。 */
void GatherOptions(const JValue& owner, const ArgDef& arg,
                   std::vector<std::pair<std::string, std::string> >* out) {
    out->clear();
    int n = 0;

    if (arg.kind == ArgKind::Dropdown) {
        const OptionDef* opts = ArgOptions(arg, &n);
        for (int i = 0; i < n; i++) out->push_back(std::make_pair(opts[i].label, opts[i].value));
        return;
    }

    if (arg.kind == ArgKind::EventName) {
        const OptionDef* ev = AllEvents(&n);
        for (int i = 0; i < n; i++) out->push_back(std::make_pair(ev[i].label, ev[i].value));
        return;
    }

    if (arg.kind == ArgKind::AreaName) {
        const OptionDef* opts = ArgOptions(arg, &n);
        for (int i = 0; i < n; i++) out->push_back(std::make_pair(opts[i].label, opts[i].value));
        // うごきに書いてある当たり判定の名前も足す
        const JValue& anims = g.project["animations"];
        for (size_t i = 0; i < anims.size(); i++) {
            const JValue& cols = anims.at(i)["collisions"];
            for (size_t k = 0; k < cols.size(); k++) {
                const std::string name = cols.at(k)["name"].asStr();
                if (name.empty()) continue;
                bool seen = false;
                for (size_t j = 0; j < out->size(); j++) if ((*out)[j].second == name) seen = true;
                if (!seen) out->push_back(std::make_pair(name, name));
            }
        }
        return;
    }

    if (arg.kind == ArgKind::VarName) {
        const JValue& vars = g.project["variables"];
        for (size_t i = 0; i < vars.size(); i++) {
            const std::string name = vars.at(i)["name"].asStr();
            if (!name.empty()) out->push_back(std::make_pair(name, name));
        }
        if (out->empty()) out->push_back(std::make_pair("（変数がありません）", ""));
        return;
    }

    if (arg.kind == ArgKind::FuncName) {
        const JValue& scripts = g.project["scripts"];
        for (size_t i = 0; i < scripts.size(); i++) {
            const std::string kind = scripts.at(i)["kind"].asStr();
            if (kind != "talk" && kind != "function") continue;
            const std::string name = scripts.at(i)["name"].asStr();
            if (name.empty()) continue;
            bool seen = false;
            for (size_t j = 0; j < out->size(); j++) if ((*out)[j].second == name) seen = true;
            if (!seen) out->push_back(std::make_pair(name, name));
        }
        if (out->empty()) out->push_back(std::make_pair("（トークがありません）", ""));
        return;
    }
    (void)owner;
}

/** えらぶ欄を押されたときに出す、小さなならび。 */
void ShowChoices(const JPath& ownerPath, const ArgDef& arg, const Piece& slot, int ox, int oy) {
    const JValue* owner = JResolve(g.project, ownerPath);
    if (!owner || !g.hwnd) return;

    std::vector<std::pair<std::string, std::string> > opts;
    GatherOptions(*owner, arg, &opts);
    if (opts.empty()) return;

    const std::string now = (*owner)[arg.name].asStr();
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (size_t i = 0; i < opts.size(); i++) {
        const std::wstring label = Utf8ToWide(opts[i].first);
        UINT flags = MF_STRING;
        if (opts[i].second == now) flags |= MF_CHECKED;
        AppendMenuW(menu, flags, kMenuBase + (UINT)i, label.c_str());
    }

    POINT pt;
    pt.x = slot.x - ox;
    pt.y = slot.y - oy + slot.h;
    ClientToScreen(g.hwnd, &pt);
    const int picked = (int)TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                                           pt.x, pt.y, 0, g.hwnd, NULL);
    DestroyMenu(menu);
    if (picked < kMenuBase) return;

    const size_t idx = (size_t)(picked - kMenuBase);
    if (idx >= opts.size()) return;

    PushUndo();
    JValue* target = JResolve(g.project, ownerPath);
    if (!target) return;

    if (opts[idx].second == "__custom__") {
        // 「その他（自分で書く）」は、打ちこみに切りかえます
        target->set(arg.name, JValue::makeStr(""));
        MarkDirty();
        Refresh();
        BeginEdit(ownerPath, arg, slot, ox, oy);
        return;
    }
    target->set(arg.name, JValue::makeStr(opts[idx].second));
    MarkDirty();
    Refresh();
}

bool ResolveSlot(int scriptIndex, int piece, JPath* ownerPath, const ArgDef** argOut) {
    const Layout& lay = g.layouts[(size_t)scriptIndex];
    const Piece& p = lay.pieces[(size_t)piece];
    if (p.kind != PieceKind::Slot || p.childCount > 0 || !p.node) return false;
    if (!FindPath(scriptIndex, p.node, ownerPath)) return false;

    const JValue* owner = JResolve(g.project, *ownerPath);
    if (!owner) return false;

    // 帽子の欄は、そのかたまり自身が持ちものです（type を持たないので定義を選びなおします）
    const BlockDef* def = FindBlockFor((*owner)["type"].asStr(), (*owner)["op"].asStr());
    if (!def) {
        const int blk = lay.BlockAt(piece);
        if (blk >= 0) def = lay.pieces[(size_t)blk].def;
    }
    if (!def) return false;
    const ArgDef* arg = FindArg(*def, p.argName);
    if (!arg) return false;
    if (argOut) *argOut = arg;
    return true;
}

/**
 * 押されたのが欄なら、そこをさわって true。
 * scriptIndex は、その欄がどのかたまりの中にあるか。
 */
bool TouchSlot(int scriptIndex, int piece, int ox, int oy) {
    const Layout& lay = g.layouts[(size_t)scriptIndex];
    const Piece& p = lay.pieces[(size_t)piece];
    JPath ownerPath;
    const ArgDef* arg = NULL;
    if (!ResolveSlot(scriptIndex, piece, &ownerPath, &arg)) return false;
    const JValue* owner = JResolve(g.project, ownerPath);
    if (!owner) return false;

    if (arg->kind == ArgKind::Input) {
        if (arg->mode == ArgMode::Bool) return false;   // 空の六角の欄は打ちこめません
        PushUndo();
        BeginEdit(ownerPath, *arg, p, ox, oy);
        return true;
    }

    // 「その他」を書きこんであるイベント名は、そのまま打ちなおせるようにします
    if (arg->kind == ArgKind::EventName) {
        const std::string now = (*owner)[arg->name].asStr();
        int n = 0;
        const OptionDef* ev = AllEvents(&n);
        bool known = false;
        for (int i = 0; i < n; i++) if (now == ev[i].value) known = true;
        if (!known && !now.empty()) {
            PushUndo();
            BeginEdit(ownerPath, *arg, p, ox, oy);
            return true;
        }
    }

    ShowChoices(ownerPath, *arg, p, ox, oy);
    return true;
}

// ------------------------------------------------------------------ マウス

/** 押されたところにあるものを見て、つまむ。 */
void OnLeftDown(int sx, int sy) {
    CommitEdit(true);           // 別のところを押したら、打ちこみは決まったことにします
    if (g.hwnd) SetFocus(g.hwnd);

    // ---- 左のブロック置き場から
    if (InPalette(sx, sy)) {
        const int py = sy + g.paletteScroll;
        for (size_t i = 0; i < g.palette.size(); i++) {
            PaletteItem& it = g.palette[i];
            int piece = it.lay.HitTest(sx, py);
            if (piece < 0) continue;
            int blk = it.lay.BlockAt(piece);
            if (blk < 0) continue;

            const Piece& p = it.lay.pieces[blk];
            PushUndo();
            if (it.def->hat) {
                // あたらしいかたまりを作って、そのまま動かす
                JValue s = MakeScript(*it.def);
                POINT c = ToCanvas(sx, sy);
                s.set("x", JValue::makeNum(c.x - (sx - p.x)));
                s.set("y", JValue::makeNum(c.y - (py - p.y)));
                JValue* scripts = JResolve(g.project, JPath().Then(JStep::Key("scripts")));
                if (!scripts || !scripts->isArr()) return;
                scripts->arr.push_back(s);
                MarkDirty();
                Refresh();

                g.dragging = true;
                g.movingScript = true;
                g.moveIndex = (int)scripts->arr.size() - 1;
                g.grabX = sx - p.x;
                g.grabY = py - p.y;
                g.mouse.x = sx; g.mouse.y = sy;
                GrabMouse(true);
                return;
            }

            JValue block = it.block;
            DragShape shape = DragShape::Stack;
            if (it.def->shape == Shape::Reporter) shape = DragShape::Reporter;
            else if (it.def->shape == Shape::Boolean) shape = DragShape::Boolean;

            JValue payload = block;
            if (shape == DragShape::Stack) {
                payload = JValue::makeArr();
                payload.arr.push_back(block);
            }
            g.mouse.x = sx; g.mouse.y = sy;
            BeginDrag(payload, shape, sx - p.x, py - p.y);
            return;
        }
        return;
    }

    // ---- 編集する面から
    POINT c = ToCanvas(sx, sy);
    for (size_t i = 0; i < g.layouts.size(); i++) {
        int piece = g.layouts[i].HitTest(c.x, c.y);
        if (piece < 0) continue;

        // 欄を押したときは、動かさずに打ちこみ・えらびに入ります
        if (TouchSlot((int)i, piece, g.scrollX - kPaletteW, g.scrollY)) return;

        int blk = g.layouts[i].BlockAt(piece);
        if (blk < 0) continue;

        const Piece& p = g.layouts[i].pieces[blk];
        g.mouse.x = sx; g.mouse.y = sy;

        // 帽子をつかんだら、かたまりごと動かす
        if (p.def && p.def->hat) {
            PushUndo();
            g.dragging = true;
            g.movingScript = true;
            g.moveIndex = (int)i;
            g.grabX = c.x - p.x;
            g.grabY = c.y - p.y;
            GrabMouse(true);
            Repaint();
            return;
        }

        JPath path;
        if (!FindPath((int)i, p.node, &path)) return;
        PushUndo();

        const bool pointy = p.def && (p.def->shape == Shape::Reporter
                                      || p.def->shape == Shape::Boolean);
        if (pointy) {
            // 欄にはまっているか、直に置いてあるか
            JPath ownerPath;
            int idx = 0;
            JValue taken;
            if (!path.steps.empty() && !path.steps[path.steps.size() - 1].isIndex) {
                const std::string argName = path.steps[path.steps.size() - 1].key;
                JPath owner;
                for (size_t k = 0; k + 1 < path.steps.size(); k++) owner.steps.push_back(path.steps[k]);
                const JValue* ownerVal = JResolve(g.project, owner);
                const BlockDef* od = ownerVal
                    ? FindBlockFor((*ownerVal)["type"].asStr(), (*ownerVal)["op"].asStr()) : NULL;
                const ArgDef* arg = od ? FindArg(*od, argName) : NULL;
                if (!PickUpSlot(g.project, owner, argName, arg, &taken)) return;
            } else if (SplitLast(path, &ownerPath, &idx)) {
                JValue arr;
                if (!PickUpStack(g.project, ownerPath, idx, &arr)) return;
                if (arr.arr.empty()) return;
                taken = arr.arr[0];
                // 2 つめ以降は元にもどす（丸いブロックは 1 つずつ動かします）
                JValue* list = JResolve(g.project, ownerPath);
                for (size_t k = 1; k < arr.arr.size(); k++) list->arr.push_back(arr.arr[k]);
            } else {
                return;
            }
            MarkDirty();
            g.mouse.x = sx; g.mouse.y = sy;
            BeginDrag(taken,
                      p.def->shape == Shape::Boolean ? DragShape::Boolean : DragShape::Reporter,
                      c.x - p.x, c.y - p.y);
            return;
        }

        JPath listPath;
        int idx = 0;
        if (!SplitLast(path, &listPath, &idx)) return;
        JValue taken;
        if (!PickUpStack(g.project, listPath, idx, &taken)) return;
        MarkDirty();
        g.mouse.x = sx; g.mouse.y = sy;
        BeginDrag(taken, DragShape::Stack, c.x - p.x, c.y - p.y);
        return;
    }
}

void OnMouseMove(int sx, int sy) {
    if (!g.dragging) return;
    g.mouse.x = sx;
    g.mouse.y = sy;

    if (g.movingScript) {
        POINT c = ToCanvas(sx, sy);
        JValue* scripts = JResolve(g.project, JPath().Then(JStep::Key("scripts")));
        if (scripts && g.moveIndex >= 0 && g.moveIndex < (int)scripts->arr.size()) {
            JValue& s = scripts->arr[(size_t)g.moveIndex];
            int nx = c.x - g.grabX;
            int ny = c.y - g.grabY;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            s.set("x", JValue::makeNum(nx));
            s.set("y", JValue::makeNum(ny));
            MarkDirty();
            RelayoutHere();
        }
        Repaint();
        return;
    }

    UpdateBest();
    Repaint();
}

void OnLeftUp(int sx, int sy) {
    if (!g.dragging) return;
    GrabMouse(false);
    g.mouse.x = sx;
    g.mouse.y = sy;

    if (g.movingScript) {
        g.dragging = false;
        g.movingScript = false;
        Refresh();
        return;
    }

    UpdateBest();
    if (g.overPalette) {
        // 置き場所へもどしたら、すてる（つまんだときに外してあるので、置かなければ消える）
        MarkDirty();
    } else if (g.best >= 0) {
        DropAt(g.project, g.targets[(size_t)g.best], g.payload);
        MarkDirty();
    } else {
        // どこにもつながらなければ、あたらしいかたまりとして置く
        POINT c = ToCanvas(sx - g.grabX, sy - g.grabY);
        JValue* scripts = JResolve(g.project, JPath().Then(JStep::Key("scripts")));
        if (scripts && scripts->isArr() && g.dragShape == DragShape::Stack) {
            JValue s = JValue::makeObj();
            s.set("kind", JValue::makeStr("event"));
            s.set("event", JValue::makeStr("OnBoot"));
            s.set("x", JValue::makeNum(c.x < 0 ? 0 : c.x));
            s.set("y", JValue::makeNum(c.y < 0 ? 0 : c.y));
            s.set("blocks", g.payload);
            scripts->arr.push_back(s);
            MarkDirty();
        }
    }

    g.dragging = false;
    g.payload = JValue();
    g.targets.clear();
    g.best = -1;
    Refresh();
}

// ------------------------------------------------------------------ 描く

void PaintPalette(HDC dc, const RECT& client) {
    RECT rc = client;
    rc.right = kPaletteW;

    HBRUSH bg = CreateSolidBrush(RGB(0xea, 0xed, 0xf4));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    // 広いブロックがあるので、はみ出したところは切っておきます
    HRGN clip = CreateRectRgn(rc.left, rc.top, kPaletteW - 1, rc.bottom);
    SelectClipRgn(dc, clip);
    PaintStyle st = g.style;
    st.gridStep = 0;
    for (size_t i = 0; i < g.palette.size(); i++) {
        PaintLayout(dc, g.palette[i].lay, g.tools, st, 0, g.paletteScroll);
    }
    SelectClipRgn(dc, NULL);
    DeleteObject(clip);

    // 右のふち
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xc8, 0xcf, 0xdc));
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, kPaletteW - 1, rc.top, NULL);
    LineTo(dc, kPaletteW - 1, rc.bottom);
    SelectObject(dc, op);
    DeleteObject(pen);
}

void PaintDropMark(HDC dc) {
    if (g.best < 0 || g.best >= (int)g.targets.size()) return;
    const DropTarget& t = g.targets[(size_t)g.best];
    const int ox = g.scrollX - kPaletteW;
    const int oy = g.scrollY;

    if (t.kind == DropKind::Stack) {
        RECT r;
        r.left = t.x - ox;
        r.top = t.y - oy - 3;
        r.right = r.left + t.w;
        r.bottom = r.top + 6;
        HBRUSH b = CreateSolidBrush(RGB(0xff, 0xd5, 0x4f));
        FillRect(dc, &r, b);
        DeleteObject(b);
    } else {
        RECT r;
        r.left = t.x - ox - 2;
        r.top = t.y - oy - 2;
        r.right = r.left + t.w + 4;
        r.bottom = r.top + t.h + 4;
        HBRUSH b = CreateSolidBrush(RGB(0xff, 0xd5, 0x4f));
        FrameRect(dc, &r, b);
        InflateRect(&r, -1, -1);
        FrameRect(dc, &r, b);
        DeleteObject(b);
    }
}

/** いまの画面ぜんぶを dc に描きます（窓があってもなくても同じ絵）。 */
void PaintEditor(HDC dc) {
    const RECT client = g.client;

    // ---- 編集する面
    RECT canvas = CanvasRect();
    const int ox = g.scrollX - kPaletteW;
    const int oy = g.scrollY;
    PaintBackground(dc, canvas, g.style, ox, oy);

    HRGN clip = CreateRectRgn(canvas.left, canvas.top, canvas.right, canvas.bottom);
    SelectClipRgn(dc, clip);
    for (size_t i = 0; i < g.layouts.size(); i++) {
        PaintLayout(dc, g.layouts[i], g.tools, g.style, ox, oy);
    }
    PaintDropMark(dc);

    // ---- つまんでいるもの（マウスのところに出す）
    if (g.dragging && !g.movingScript) {
        PaintLayout(dc, g.dragLay, g.tools, g.style,
                    -(g.mouse.x - g.grabX), -(g.mouse.y - g.grabY));
    }
    SelectClipRgn(dc, NULL);
    DeleteObject(clip);

    // ---- 左のブロック置き場
    PaintPalette(dc, client);
}

void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(hwnd, &ps);
    SyncClient();
    const int w = g.client.right, h = g.client.bottom;

    // ちらつかないように、いったん記憶の中の絵へ描いてから写します
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    PaintEditor(dc);
    BitBlt(screen, 0, 0, w, h, dc, 0, 0, SRCCOPY);

    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

// ------------------------------------------------------------------ 窓の仕事

void OnScroll(int dx, int dy) {
    RECT canvas = CanvasRect();
    const int viewW = canvas.right - canvas.left;
    const int viewH = canvas.bottom - canvas.top;
    g.scrollX = ClampI(g.scrollX + dx, 0, g.canvasW > viewW ? g.canvasW - viewW : 0);
    g.scrollY = ClampI(g.scrollY + dy, 0, g.canvasH > viewH ? g.canvasH - viewH : 0);
    Repaint();
}

void DoOpen() {
    wchar_t buf[MAX_PATH];
    buf[0] = 0;
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hwnd;
    ofn.lpstrFilter = L"ゴーストの設計図 (ghost.json)\0*.json\0すべて\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    if (!LoadGhost(buf)) {
        MessageBoxW(g.hwnd, L"読めませんでした。", L"なしスタジオ", MB_ICONWARNING);
    }
}

void DoUndo() {
    if (!g.hasUndo) return;
    JValue now = g.project;
    g.project = g.undo;
    g.undo = now;    // もう一度押したら、やりなおし
    MarkDirty();
    Refresh();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g.hwnd = hwnd;
            return 0;

        case WM_SIZE:
            SyncClient();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_ERASEBKGND:
            return 1;   // WM_PAINT で全部塗るので、ここでは何もしません

        case WM_PAINT:
            OnPaint(hwnd);
            return 0;

        case WM_LBUTTONDOWN:
            OnLeftDown((short)LOWORD(lp), (short)HIWORD(lp));
            return 0;

        case WM_MOUSEMOVE:
            OnMouseMove((short)LOWORD(lp), (short)HIWORD(lp));
            return 0;

        case WM_LBUTTONUP:
            OnLeftUp((short)LOWORD(lp), (short)HIWORD(lp));
            return 0;

        case WM_MOUSEWHEEL: {
            const int delta = (short)HIWORD(wp);
            POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
            ScreenToClient(hwnd, &pt);
            if (InPalette(pt.x, pt.y)) {
                g.paletteScroll = ClampI(g.paletteScroll - delta / 2, 0,
                                         g.paletteHeight > 100 ? g.paletteHeight - 100 : 0);
                InvalidateRect(hwnd, NULL, FALSE);
            } else {
                OnScroll((GetKeyState(VK_SHIFT) < 0) ? -delta / 2 : 0,
                         (GetKeyState(VK_SHIFT) < 0) ? 0 : -delta / 2);
            }
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wp) == kEditId && HIWORD(wp) == EN_KILLFOCUS) CommitEdit(true);
            return 0;

        case WM_KEYDOWN:
            if (wp == 'S' && GetKeyState(VK_CONTROL) < 0) {
                if (!SaveGhost()) {
                    MessageBoxW(hwnd, L"保存できませんでした。", L"なしスタジオ", MB_ICONWARNING);
                }
            } else if (wp == 'O' && GetKeyState(VK_CONTROL) < 0) {
                DoOpen();
            } else if (wp == 'Z' && GetKeyState(VK_CONTROL) < 0) {
                DoUndo();
            } else if (wp == VK_ESCAPE && g.editing) {
                CommitEdit(false);
            } else if (wp == VK_ESCAPE && g.dragging) {
                DoUndo();
                GrabMouse(false);
                g.dragging = false;
                g.movingScript = false;
                Refresh();
            } else if (wp == VK_LEFT) OnScroll(-40, 0);
            else if (wp == VK_RIGHT) OnScroll(40, 0);
            else if (wp == VK_UP) OnScroll(0, -40);
            else if (wp == VK_DOWN) OnScroll(0, 40);
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = (MINMAXINFO*)lp;
            mm->ptMinTrackSize.x = kPaletteW + kMinCanvas;
            mm->ptMinTrackSize.y = 400;
            return 0;
        }

        case WM_CLOSE:
            if (g.dirty) {
                int a = MessageBoxW(hwnd, L"保存していない変更があります。\n保存して閉じますか？",
                                    L"なしスタジオ", MB_YESNOCANCEL | MB_ICONWARNING);
                if (a == IDCANCEL) return 0;
                if (a == IDYES && !SaveGhost()) return 0;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

// ---------------------------------------------------------------------- 公開

namespace {

/** 窓を出さずに、いまの姿を PNG にする（下ごしらえは済んでいるものとします）。 */
bool ShootToPng(int width, int height, std::string* png) {
    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!dc) return false;

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) { DeleteDC(dc); return false; }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    PaintEditor(dc);
    GdiFlush();

    std::vector<unsigned char> rgba((size_t)width * height * 4);
    const unsigned char* src = (const unsigned char*)bits;
    for (int i = 0; i < width * height; i++) {
        rgba[i * 4 + 0] = src[i * 4 + 2];   // B G R X -> R G B A
        rgba[i * 4 + 1] = src[i * 4 + 1];
        rgba[i * 4 + 2] = src[i * 4 + 0];
        rgba[i * 4 + 3] = 255;
    }
    if (png) *png = EncodePng(width, height, rgba);

    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    return png ? !png->empty() : true;
}

/** 窓を出さずに、ghost.json を読んで並べるところまで。 */
bool SetUpHeadless(const std::wstring& ghostPath, int width, int height,
                   int scrollX, int scrollY) {
    g.hwnd = NULL;
    g.style.gridStep = 24;
    g.client.left = g.client.top = 0;
    g.client.right = width;
    g.client.bottom = height;
    g.dragging = false;
    g.movingScript = false;
    g.dirty = false;
    if (!g.tools.Create()) return false;

    g.project = JValue::makeObj();
    g.project.set("scripts", JValue::makeArr());
    if (!ghostPath.empty()) {
        std::string text;
        JValue root;
        std::string err;
        if (!ReadTextFile(ghostPath, text) || !JsonParse(text, root, err) || !root.isObj()) {
            g.tools.Free();
            return false;
        }
        PlaceScripts(root);
        g.project = root;
        g.path = ghostPath;
    }
    g.scrollX = scrollX;
    g.scrollY = scrollY;
    Relayout();
    return true;
}

} // namespace

bool ProbeEditor(const EditorProbe& probe, std::string* png, std::string* json) {
    if (probe.width < 200 || probe.height < 200
        || probe.width > 4000 || probe.height > 4000) return false;
    if (!SetUpHeadless(probe.ghostPath, probe.width, probe.height,
                       probe.scrollX, probe.scrollY)) return false;

    int fromX = probe.fromX, fromY = probe.fromY;
    if (!probe.grabPalette.empty()) {
        // 名前で言われたときは、その置き場所の左上あたりをつかみます
        bool found = false;
        for (size_t i = 0; i < g.palette.size(); i++) {
            if (!g.palette[i].def || g.palette[i].key() != probe.grabPalette) continue;
            const Layout& lay = g.palette[i].lay;
            if (lay.pieces.empty()) break;
            fromX = lay.pieces[0].x + 8;
            fromY = lay.pieces[0].y + lay.pieces[0].h / 2 - g.paletteScroll;
            found = true;
            break;
        }
        if (!found) { g.tools.Free(); return false; }
    }

    if (probe.drag) {
        OnLeftDown(fromX, fromY);
        OnMouseMove(probe.toX, probe.toY);
        if (probe.release) OnLeftUp(probe.toX, probe.toY);
    }
    bool ok = true;
    if (png) ok = ShootToPng(probe.width, probe.height, png);
    if (json) *json = g.project.dump(2);
    g.tools.Free();
    return ok;
}

bool ProbeField(const FieldProbe& probe, std::string* info, std::string* json) {
    if (!SetUpHeadless(probe.ghostPath, probe.width, probe.height, 0, 0)) return false;

    const POINT c = ToCanvas(probe.x, probe.y);
    int si = -1, piece = -1;
    for (size_t i = 0; i < g.layouts.size(); i++) {
        const int hit = g.layouts[i].HitTest(c.x, c.y);
        if (hit < 0) continue;
        si = (int)i;
        piece = hit;
        break;
    }

    std::string out;
    if (si < 0) {
        out = "そこには何もありません\n";
    } else {
        JPath ownerPath;
        const ArgDef* arg = NULL;
        if (!ResolveSlot(si, piece, &ownerPath, &arg)) {
            out = "そこは欄ではありません\n";
        } else {
            const JValue* owner = JResolve(g.project, ownerPath);
            const char* kindName = "input";
            switch (arg->kind) {
                case ArgKind::Dropdown:  kindName = "dropdown"; break;
                case ArgKind::EventName: kindName = "eventname"; break;
                case ArgKind::AreaName:  kindName = "areaname"; break;
                case ArgKind::FuncName:  kindName = "funcname"; break;
                case ArgKind::VarName:   kindName = "varname"; break;
                default: break;
            }
            out += std::string("欄 ") + arg->name + "\n";
            out += std::string("やりかた ") + kindName + "\n";
            out += std::string("見出し ") + SlotText(*owner, *arg) + "\n";
            out += std::string("いま ") + (*owner)[arg->name].asStr() + "\n";
            out += std::string("どこ ") + ownerPath.ToString() + "\n";

            if (arg->kind != ArgKind::Input) {
                std::vector<std::pair<std::string, std::string> > opts;
                GatherOptions(*owner, *arg, &opts);
                for (size_t i = 0; i < opts.size(); i++) {
                    out += "えらべる " + opts[i].first + " = " + opts[i].second + "\n";
                }
            }

            if (probe.set) {
                JValue* target = JResolve(g.project, ownerPath);
                if (target) {
                    target->set(arg->name,
                                ValueForField(arg->mode == ArgMode::Number, probe.value));
                }
            }
        }
    }
    if (info) *info = out;
    if (json) *json = g.project.dump(2);
    g.tools.Free();
    return true;
}

bool EditorFieldSpots(const std::wstring& ghostPath, int width, int height,
                      std::vector<FieldSpot>* out) {
    if (!out) return false;
    if (!SetUpHeadless(ghostPath, width, height, 0, 0)) return false;
    out->clear();

    const int ox = g.scrollX - kPaletteW;
    const int oy = g.scrollY;
    for (size_t i = 0; i < g.layouts.size(); i++) {
        const Layout& lay = g.layouts[i];
        for (size_t k = 0; k < lay.pieces.size(); k++) {
            if (lay.pieces[k].kind != PieceKind::Slot) continue;
            JPath ownerPath;
            const ArgDef* arg = NULL;
            if (!ResolveSlot((int)i, (int)k, &ownerPath, &arg)) continue;

            FieldSpot sp;
            sp.owner = ownerPath.ToString();
            sp.arg = arg->name;
            switch (arg->kind) {
                case ArgKind::Dropdown:  sp.kind = "dropdown"; break;
                case ArgKind::EventName: sp.kind = "eventname"; break;
                case ArgKind::AreaName:  sp.kind = "areaname"; break;
                case ArgKind::FuncName:  sp.kind = "funcname"; break;
                case ArgKind::VarName:   sp.kind = "varname"; break;
                default:                 sp.kind = "input"; break;
            }
            sp.x = lay.pieces[k].x - ox;
            sp.y = lay.pieces[k].y - oy;
            sp.w = lay.pieces[k].w;
            sp.h = lay.pieces[k].h;
            out->push_back(sp);
        }
    }
    g.tools.Free();
    return true;
}

bool EditorPaletteSpots(int width, int height, std::vector<PaletteSpot>* out) {
    if (!out) return false;
    if (!SetUpHeadless(L"", width, height, 0, 0)) return false;
    out->clear();
    for (size_t i = 0; i < g.palette.size(); i++) {
        const Layout& lay = g.palette[i].lay;
        if (lay.pieces.empty()) continue;
        PaletteSpot sp;
        sp.key = g.palette[i].key();
        sp.x = lay.pieces[0].x;
        sp.y = lay.pieces[0].y;
        sp.w = lay.pieces[0].w;
        sp.h = lay.pieces[0].h;
        out->push_back(sp);
    }
    g.tools.Free();
    return true;
}

bool RenderEditor(const std::wstring& ghostPath, int width, int height,
                  int scrollX, int scrollY, std::string* png) {
    EditorProbe probe;
    probe.ghostPath = ghostPath;
    probe.width = width;
    probe.height = height;
    probe.scrollX = scrollX;
    probe.scrollY = scrollY;
    return ProbeEditor(probe, png, NULL);
}

int RunEditor(HINSTANCE hInstance, const std::wstring& ghostPath) {
    g.inst = hInstance;
    g.style.gridStep = 24;

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, kClass, L"なしスタジオ（ためし版）", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760,
                                NULL, NULL, hInstance, NULL);
    if (!hwnd) return 1;
    g.hwnd = hwnd;

    if (!g.tools.Create()) {
        MessageBoxW(hwnd, L"文字の用意ができませんでした。", L"なしスタジオ", MB_ICONERROR);
        return 1;
    }

    if (g.project.isNull()) {
        g.project = JValue::makeObj();
        g.project.set("scripts", JValue::makeArr());
    }
    if (!ghostPath.empty() && !LoadGhost(ghostPath)) {
        MessageBoxW(hwnd, L"ghost.json を読めませんでした。", L"なしスタジオ", MB_ICONWARNING);
    }
    Relayout();
    UpdateTitle();

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        // 打ちこみの小さな窓は、Enter で決める・Esc でやめる
        if (msg.message == WM_KEYDOWN && g.editing && msg.hwnd == g.edit) {
            if (msg.wParam == VK_RETURN) { CommitEdit(true); continue; }
            if (msg.wParam == VK_ESCAPE) { CommitEdit(false); continue; }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g.tools.Free();
    return 0;
}

} // namespace w2k
} // namespace nashi
