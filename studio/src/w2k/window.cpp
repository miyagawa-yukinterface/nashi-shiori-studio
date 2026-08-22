#include "window.h"

#include <commdlg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <shlobj.h>
#include <string>
#include <vector>

#include "blockdefs.h"
#include "drag.h"
#include "layout.h"
#include "paint.h"
#include "panel.h"
#include "../exporter.h"
#include "../preview.h"
#include "../image.h"
#include "../../../shiori/src/json.h"
#include "../../../shiori/src/util.h"
#include "../fsutil.h"
#include "../image.h"

namespace nashi {
namespace w2k {

namespace {

const wchar_t* kClass = L"NashiStudioW2kWindow";
const int kPaletteW = 210;      // 左のブロック置き場の幅
const int kPalettePad = 10;
const int kMinCanvas = 320;
const int kPanelW = 320;        // 右の作業だなの幅
const int kTabH = 26;           // たなの見出しの高さ
const int kTabRows = 2;         // 見出しは 2 段にならべます（7 つあるので）

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
    bool editToQuery = false;        // 「さがす言葉」を打ちこんでいるか
    std::string choiceItem;          // えらぶ欄を、窓なしで打ちこんでいるとき

    std::string shioriDll;           // 書き出しに使う栞（exe に入れてあるもの）
    EditorState lastPlacement;       // 閉じたときの窓の場所

    // ---- 右の作業だな
    PanelState panel;
    std::vector<PanelItem> panelItems;
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
    rc.right -= kPanelW;
    if (rc.right < rc.left) rc.right = rc.left;
    return rc;
}

/** 右の作業だな（見出しもふくめた全体）。 */
RECT PanelRect() {
    RECT rc = g.client;
    rc.left = rc.right - kPanelW;
    if (rc.left < 0) rc.left = 0;
    return rc;
}

bool InPanel(int sx, int sy) {
    const RECT rc = PanelRect();
    return sx >= rc.left && sx < rc.right && sy >= rc.top && sy < rc.bottom;
}

/** 見出しの中の、i 番目のたなの場所。 */
RECT TabRect(int i) {
    const RECT panel = PanelRect();
    const int perRow = (kTabCount + kTabRows - 1) / kTabRows;   // 4 つと 3 つ
    const int row = i / perRow;
    const int col = i % perRow;
    const int w = (panel.right - panel.left) / perRow;
    RECT rc;
    rc.left = panel.left + col * w;
    rc.right = rc.left + w;
    rc.top = panel.top + row * kTabH;
    rc.bottom = rc.top + kTabH;
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
    std::wstring title = L"なしスタジオ";
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

void RelayoutPanel(HDC dc) {
    GdiMeasurer tm(dc, g.tools.slotFont);
    const RECT rc = PanelRect();
    BuildPanel(g.project, g.panel, tm, rc.left, rc.right - rc.left, &g.panelItems);
    // 見出しのぶんだけ下げます
    for (size_t i = 0; i < g.panelItems.size(); i++) {
        g.panelItems[i].y += kTabH * kTabRows;
    }
}

void Relayout() {
    HDC dc = GetDC(g.hwnd);      // hwnd が NULL なら画面の DC（文字を測るだけなので十分）
    RelayoutPalette(dc);
    RelayoutScripts(dc);
    RelayoutPanel(dc);
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

bool LoadGhost(const std::wstring& path) {
    std::string text;
    if (!ReadTextFile(path, text)) return false;
    JValue root;
    std::string err;
    if (!JsonParse(text, root, err)) return false;
    if (!root.isObj()) return false;

    NormalizeProject(root);
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

void ForgetShellPics();   // 下で書いています（立ち絵の見本を作りなおさせる）
void SetPanelValue(const std::string& id, const std::string& value);
JValue* AnimList();
JValue* AnimSub(JValue& anim, const char* key);
JValue MakePattern();
JValue MakeCollision(int n);
JValue MakeAnimation(int id);

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

/**
 * 打ちこんだ字を、いまの打ちこみ先に入れます。
 * 窓を出しているときも、テストから直に入れるときも、ここを通します。
 */
void ApplyEditText(const std::string& text) {
    ForgetShellPics();      // 色を打ちかえたときのために

    if (g.editToQuery) {
        g.panel.query = text;      // 「さがす言葉」は ghost.json には入れません
        return;
    }
    if (!g.choiceItem.empty()) {
        SetPanelValue(g.choiceItem, text);
        MarkDirty();
        return;
    }
    JValue* owner = JResolve(g.project, g.editOwner);
    if (owner && owner->isObj()) {
        owner->set(g.editArg, ValueForField(g.editNumber, text));
        MarkDirty();
    }
}

/** いま打ちこんでいる中身を、ghost.json に書きこむ。 */
void CommitEdit(bool keep) {
    if (!g.editing) return;
    g.editing = false;

    HWND edit = g.edit;
    g.edit = NULL;
    if (!edit) {              // 窓を出していないとき（テスト）は、覚えていた先を忘れます
        g.editToQuery = false;
        g.choiceItem.clear();
        g.editArg.clear();
        return;
    }

    if (keep) {
        wchar_t buf[1024];
        const int n = GetWindowTextW(edit, buf, 1024);
        buf[(n >= 0 && n < 1024) ? n : 0] = 0;
        ApplyEditText(WideToUtf8(buf));
    }
    g.editToQuery = false;
    g.choiceItem.clear();
    DestroyWindow(edit);
    Refresh();
}

/** そこに、打ちこみ用の小さな窓を出す（欄でも、作業だなでも使います）。 */
void BeginEditRect(const JPath& ownerPath, const std::string& key, const std::string& text,
                   bool isNumber, int x, int y, int w, int h) {
    CommitEdit(true);

    // 打ちこむ先は、窓が無くても覚えておきます（テストから中身を入れられるように）
    g.editOwner = ownerPath;
    g.editArg = key;
    g.editNumber = isNumber;
    g.editing = true;
    if (!g.hwnd) return;

    const std::wstring wide = Utf8ToWide(text);
    g.edit = CreateWindowExW(0, L"EDIT", wide.c_str(),
                             WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                             x, y, w > 40 ? w : 40, h,
                             g.hwnd, (HMENU)(INT_PTR)kEditId, g.inst, NULL);
    if (!g.edit) return;
    SendMessageW(g.edit, WM_SETFONT, (WPARAM)g.tools.slotFont, TRUE);
    SendMessageW(g.edit, EM_SETSEL, 0, -1);
    SetFocus(g.edit);
}

/** その欄の上に、打ちこみ用の小さな窓を出す。 */
void BeginEdit(const JPath& ownerPath, const ArgDef& arg, const Piece& slot, int ox, int oy) {
    const JValue* owner = JResolve(g.project, ownerPath);
    if (!owner) return;
    BeginEditRect(ownerPath, arg.name, SlotText(*owner, arg),
                  arg.mode == ArgMode::Number,
                  slot.x - ox, slot.y - oy, slot.w, slot.h);
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

// ------------------------------------------------------- 作業だなを押す

/** "anim.0.pattern.2.wait" を、"anim" "0" "pattern" "2" "wait" に切る。 */
void SplitId(const std::string& id, std::vector<std::string>* out) {
    out->clear();
    size_t start = 0;
    for (;;) {
        const size_t dot = id.find('.', start);
        if (dot == std::string::npos) { out->push_back(id.substr(start)); break; }
        out->push_back(id.substr(start, dot - start));
        start = dot + 1;
    }
}

/** その字が数なら、その数。ちがえば -1。 */
int NumOf(const std::string& s) {
    if (s.empty()) return -1;
    int v = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

/** "var.del.3" のような目じるしから、うしろの数を取り出す。無ければ -1。 */
int IdIndex(const std::string& id) {
    const size_t dot = id.rfind('.');
    if (dot == std::string::npos || dot + 1 >= id.size()) return -1;
    int v = 0;
    for (size_t i = dot + 1; i < id.size(); i++) {
        if (id[i] < '0' || id[i] > '9') return -1;
        v = v * 10 + (id[i] - '0');
    }
    return v;
}

bool StartsWith(const std::string& s, const char* head) {
    const size_t n = strlen(head);
    return s.size() >= n && s.compare(0, n, head) == 0;
}

/** うごきのならびを、無ければ作って返す。 */
JValue* AnimList() {
    if (!g.project["animations"].isArr()) {
        g.project.set("animations", JValue::makeArr());
    }
    for (size_t i = 0; i < g.project.obj.size(); i++) {
        if (g.project.obj[i].first == "animations") return &g.project.obj[i].second;
    }
    return NULL;
}

/** うごきの中の「こま」「当たり判定」のならびを、無ければ作って返す。 */
JValue* AnimSub(JValue& anim, const char* key) {
    if (!anim[key].isArr()) anim.set(key, JValue::makeArr());
    for (size_t i = 0; i < anim.obj.size(); i++) {
        if (anim.obj[i].first == key) return &anim.obj[i].second;
    }
    return NULL;
}

JValue MakePattern() {
    JValue p = JValue::makeObj();
    p.set("surface", JValue::makeNum(0));
    p.set("wait", JValue::makeNum(200));
    p.set("method", JValue::makeStr("base"));
    p.set("x", JValue::makeNum(0));
    p.set("y", JValue::makeNum(0));
    return p;
}

JValue MakeCollision(int n) {
    char name[32];
    sprintf(name, "Area%d", n);
    JValue c = JValue::makeObj();
    c.set("name", JValue::makeStr(name));
    c.set("shape", JValue::makeStr("rect"));
    c.set("x1", JValue::makeNum(0));
    c.set("y1", JValue::makeNum(0));
    c.set("x2", JValue::makeNum(0));
    c.set("y2", JValue::makeNum(0));
    return c;
}

JValue MakeAnimation(int id) {
    JValue a = JValue::makeObj();
    a.set("id", JValue::makeNum(id));
    a.set("base", JValue::makeNum(0));
    a.set("interval", JValue::makeStr("never"));
    a.set("every", JValue::makeNum(4));
    a.set("patterns", JValue::makeArr());
    a.set("collisions", JValue::makeArr());
    return a;
}

/** 立ち絵の割りあてを入れかえる（path が空なら、はずす）。 */
void SetShellImage(int surface, const std::string& path) {
    JValue* sh = JResolve(g.project, JPath().Then(JStep::Key("shell")));
    if (!sh || !sh->isObj()) return;
    if (!(*sh)["images"].isArr()) sh->set("images", JValue::makeArr());

    JValue* images = NULL;
    for (size_t i = 0; i < sh->obj.size(); i++) {
        if (sh->obj[i].first == "images") { images = &sh->obj[i].second; break; }
    }
    if (!images) return;

    for (size_t i = 0; i < images->arr.size(); i++) {
        if (images->arr[i]["id"].asInt(-1) != surface) continue;
        if (path.empty()) images->arr.erase(images->arr.begin() + i);
        else images->arr[i].set("path", JValue::makeStr(path));
        return;
    }
    if (path.empty()) return;

    // 名前は、ファイル名のところだけ取ります
    std::string name = path;
    const size_t cut = name.find_last_of("\\/");
    if (cut != std::string::npos) name = name.substr(cut + 1);

    JValue one = JValue::makeObj();
    one.set("id", JValue::makeNum(surface));
    one.set("path", JValue::makeStr(path));
    one.set("name", JValue::makeStr(name));
    images->arr.push_back(one);
}

/** 変数のならびを、無ければ作って返す。 */
JValue* VariablesList() {
    if (!g.project.has("variables")) g.project.set("variables", JValue::makeArr());
    return JResolve(g.project, JPath().Then(JStep::Key("variables")));
}

/** まだ使われていない、あたらしい変数の名前。 */
std::string FreshVarName() {
    const JValue& vars = g.project["variables"];
    for (int n = 1; n < 1000; n++) {
        char buf[32];
        sprintf(buf, "へんすう%d", n);
        bool used = false;
        for (size_t i = 0; i < vars.size(); i++) {
            if (vars.at(i)["name"].asStr() == buf) { used = true; break; }
        }
        if (!used) return buf;
    }
    return "へんすう";
}

// ------------------------------------------------------------ 立ち絵の見本

/** 描いた立ち絵。おなじ絵を何度も作らないよう、覚えておきます。 */
struct ShellPic {
    std::string key;      // どの絵か（番号と色、または読んだファイル）
    int w = 0, h = 0;
    std::vector<unsigned char> bgr;   // 下から上へならぶ 32bit（GDI に渡す形）
};
std::vector<ShellPic> g_shellPics;

void ForgetShellPics() { g_shellPics.clear(); }

/** その番号の立ち絵を、RGBA で用意する。 */
bool MakeShellPic(int surface, const std::string& key, ShellPic* out) {
    const JValue& sh = g.project["shell"];
    std::string png;

    const std::string path = ShellImagePath(g.project, surface);
    if (!path.empty()) {
        // 用意された PNG を読む（pngread.cpp）
        if (!ReadBinaryFile(Utf8ToWide(path), png)) return false;
    } else {
        const bool kero = surface >= 10;
        png = RenderSurfacePng(surface,
                               sh[kero ? "keroColor" : "sakuraColor"].asStr(),
                               sh[kero ? "keroCloth" : "sakuraCloth"].asStr());
    }
    if (png.empty()) return false;

    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
    if (!DecodePng(png, &w, &h, &rgba)) return false;
    if (w <= 0 || h <= 0) return false;

    out->key = key;
    out->w = w;
    out->h = h;
    out->bgr.assign((size_t)w * h * 4, 0);
    // GDI は下から上へならんだ BGRX を待っています。すきとおりは白地に混ぜます。
    for (int y = 0; y < h; y++) {
        const unsigned char* src = &rgba[(size_t)y * w * 4];
        unsigned char* dst = &out->bgr[(size_t)(h - 1 - y) * w * 4];
        for (int x = 0; x < w; x++) {
            const int a = src[x * 4 + 3];
            dst[x * 4 + 0] = (unsigned char)((src[x * 4 + 2] * a + 255 * (255 - a)) / 255);
            dst[x * 4 + 1] = (unsigned char)((src[x * 4 + 1] * a + 255 * (255 - a)) / 255);
            dst[x * 4 + 2] = (unsigned char)((src[x * 4 + 0] * a + 255 * (255 - a)) / 255);
            dst[x * 4 + 3] = 255;
        }
    }
    return true;
}

/** その番号の立ち絵を返す（無ければ作る）。 */
const ShellPic* ShellPicFor(int surface) {
    const JValue& sh = g.project["shell"];
    const std::string path = ShellImagePath(g.project, surface);
    char num[24];
    sprintf(num, "%d|", surface);
    const bool kero = surface >= 10;
    const std::string key = std::string(num) + (path.empty()
        ? (sh[kero ? "keroColor" : "sakuraColor"].asStr() + "|"
           + sh[kero ? "keroCloth" : "sakuraCloth"].asStr())
        : path);

    for (size_t i = 0; i < g_shellPics.size(); i++) {
        if (g_shellPics[i].key == key) return &g_shellPics[i];
    }
    ShellPic pic;
    if (!MakeShellPic(surface, key, &pic)) return NULL;
    if (g_shellPics.size() > 24) g_shellPics.clear();   // 覚えすぎない
    g_shellPics.push_back(pic);
    return &g_shellPics[g_shellPics.size() - 1];
}

/** 字を、たなの幅に合わせて何行かに切る（さくらスクリプトを出すのに使います）。 */
void SplitLines(const std::string& text, size_t width, std::vector<std::string>* out) {
    out->clear();
    std::string line;
    size_t cells = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = (unsigned char)text[i];
        const int len = c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        const size_t w = (len == 1) ? 1 : 2;
        if (c == '\n') {
            out->push_back(line);
            line.clear();
            cells = 0;
            i++;
            continue;
        }
        if (cells + w > width) {
            out->push_back(line);
            line.clear();
            cells = 0;
        }
        line.append(text, i, (size_t)len);
        cells += w;
        i += (size_t)len;
    }
    if (!line.empty()) out->push_back(line);
    if (out->empty()) out->push_back("（何も出ませんでした）");
}

/** そのかたまりを、栞そのもので動かしてみる。 */
void RunOne(int scriptIndex) {
    const JValue& scripts = g.project["scripts"];
    if (scriptIndex < 0 || scriptIndex >= (int)scripts.size()) return;
    const JValue& s = scripts.at((size_t)scriptIndex);

    PreviewRequest req;
    req.project = g.project;
    req.scriptId = s["id"].asStr();
    req.ghostName = g.project["meta"]["name"].asStr();
    req.shellName = "master";
    req.boots = 1;

    const PreviewResult res = RunPreview(req);
    g.panel.runTitle = ScriptTitle(s);
    if (!res.ok) {
        g.panel.runOut.clear();
        g.panel.runOut.push_back("うまく動きませんでした");
        g.panel.runOut.push_back(res.error);
        return;
    }
    SplitLines(res.script, 34, &g.panel.runOut);
}

/** 書き出す先をえらんでもらう。 */
std::wstring AskFolder() {
    if (!g.hwnd) return std::wstring();   // 窓が無いとき（テスト）は、たずねません

    // Windows 2000 から使える、古いほうの選びかた
    wchar_t path[MAX_PATH];
    path[0] = 0;
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = g.hwnd;
    bi.pszDisplayName = path;
    bi.lpszTitle = L"書き出す先のフォルダをえらんでください";
    bi.ulFlags = BIF_RETURNONLYFSDIRS;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (!id) return std::wstring();
    wchar_t full[MAX_PATH];
    const bool ok = SHGetPathFromIDListW(id, full) != FALSE;
    CoTaskMemFree(id);
    return ok ? std::wstring(full) : std::wstring();
}

/** 書き出す。nar なら 1 つのファイルにまとめます。 */
void DoExport(bool nar) {
    if (g.panel.exportDir.empty()) {
        const std::wstring picked = AskFolder();
        if (picked.empty()) return;
        g.panel.exportDir = WideToUtf8(picked);
    }
    const std::wstring dir = Utf8ToWide(g.panel.exportDir);

    // 栞は、となりに置いてあればそれ、無ければ渡されたもの（exe に入れてある分）
    // を使います。ここが exe のリソースを直に見ないのは、テストからも動かせるように
    // しておくためです。
    std::string dllData;
    if (!ReadBinaryFile(PathJoin(ExeDir(), L"nashi.dll"), dllData) || dllData.empty()) {
        dllData = g.shioriDll;
    }
    if (dllData.empty()) {
        g.panel.exportOut.clear();
        g.panel.exportOut.push_back("栞（nashi.dll）が見つかりません");
        return;
    }

    const ExportResult res = nar ? ExportToNar(g.project, dir, dllData, true)
                                 : ExportToDir(g.project, dir, dllData, true, false);
    g.panel.exportOut.clear();
    if (!res.ok) {
        g.panel.exportOut.push_back("書き出せませんでした");
        g.panel.exportOut.push_back(res.error);
        return;
    }
    g.panel.exportOut.push_back("書き出しました");
    g.panel.exportOut.push_back(WideToUtf8(res.root));
    char buf[64];
    sprintf(buf, "%d 個のファイル", (int)res.written.size());
    g.panel.exportOut.push_back(buf);
}

/** えらぶ欄でえらんだ値を、ghost.json に入れる。 */
void SetPanelValue(const std::string& id, const std::string& value) {
    std::vector<std::string> part;
    SplitId(id, &part);
    if (part.empty() || part[0] != "anim") return;

    JValue* anims = AnimList();
    const int ai = (part.size() >= 2) ? NumOf(part[1]) : -1;
    if (!anims || ai < 0 || ai >= (int)anims->arr.size()) return;

    if (part.size() == 3) {
        anims->arr[(size_t)ai].set(part[2], JValue::makeStr(value));
        return;
    }
    if (part.size() == 5) {
        const char* listKey = (part[2] == "pattern") ? "patterns" : "collisions";
        JValue* list = AnimSub(anims->arr[(size_t)ai], listKey);
        const int k = NumOf(part[3]);
        if (!list || k < 0 || k >= (int)list->arr.size()) return;
        list->arr[(size_t)k].set(part[4], JValue::makeStr(value));
    }
}

/** 作業だなの中を押されたとき。押した場所は窓の中の座標。 */
void OnPanelClick(int sx, int sy) {
    // ---- 見出し（たなを切りかえる）
    for (int i = 0; i < kTabCount; i++) {
        const RECT rc = TabRect(i);
        if (sx < rc.left || sx >= rc.right || sy < rc.top || sy >= rc.bottom) continue;
        g.panel.tab = TabAt(i);
        g.panel.scroll = 0;
        Refresh();
        return;
    }

    const int hit = PanelHitTest(g.panelItems, sx, sy);
    if (hit < 0) return;
    const PanelItem it = g.panelItems[(size_t)hit];

    // ---- 変数のたな
    if (it.id == "var.add") {
        PushUndo();
        JValue* vars = VariablesList();
        if (!vars) return;
        JValue v = JValue::makeObj();
        v.set("name", JValue::makeStr(FreshVarName()));
        v.set("value", JValue::makeNum(0));
        vars->arr.push_back(v);
        MarkDirty();
        Refresh();
        return;
    }
    if (StartsWith(it.id, "var.del.")) {
        const int i = IdIndex(it.id);
        JValue* vars = VariablesList();
        if (!vars || i < 0 || i >= (int)vars->arr.size()) return;
        PushUndo();
        vars->arr.erase(vars->arr.begin() + i);
        MarkDirty();
        Refresh();
        return;
    }
    if (StartsWith(it.id, "var.name.") || StartsWith(it.id, "var.value.")) {
        const int i = IdIndex(it.id);
        const JValue& vars = g.project["variables"];
        if (i < 0 || i >= (int)vars.size()) return;
        const bool isName = StartsWith(it.id, "var.name.");
        PushUndo();
        BeginEditRect(JPath().Then(JStep::Key("variables")).Then(JStep::Index(i)),
                      isName ? "name" : "value",
                      vars.at(i)[isName ? "name" : "value"].asStr(),
                      false, it.x, it.y, it.w, it.h);
        return;
    }

    // ---- ためすたな
    if (StartsWith(it.id, "run.go.")) {
        RunOne(IdIndex(it.id));
        Refresh();
        return;
    }

    // ---- ゴーストのたな
    if (StartsWith(it.id, "meta.") || StartsWith(it.id, "settings.")) {
        const size_t dot = it.id.find('.');
        const std::string group = it.id.substr(0, dot);
        const std::string key = it.id.substr(dot + 1);

        if (it.kind == ItemKind::Button) {
            // 「自動でしゃべる」の入り切り
            PushUndo();
            JValue* st = JResolve(g.project, JPath().Then(JStep::Key(group)));
            if (st && st->isObj()) {
                const bool on = !((*st)[key.c_str()].type == JType::Bool
                                  && !(*st)[key.c_str()].b);
                st->set(key, JValue::makeBool(!on));
                MarkDirty();
            }
            Refresh();
            return;
        }
        const JValue* owner = JResolve(g.project, JPath().Then(JStep::Key(group)));
        if (!owner) return;
        const bool isNumber = (key == "randomTalkInterval" || key == "noRepeatCount");
        PushUndo();
        BeginEditRect(JPath().Then(JStep::Key(group)), key,
                      (*owner)[key.c_str()].asStr(), isNumber, it.x, it.y, it.w, it.h);
        return;
    }

    // ---- 決まった中からえらぶ欄
    if (it.kind == ItemKind::Choice) {
        std::string picked;
        if (g.hwnd) {
            HMENU menu = CreatePopupMenu();
            if (!menu) return;
            for (size_t i = 0; i < it.options.size(); i++) {
                UINT flags = MF_STRING;
                if (it.options[i].first == it.value) flags |= MF_CHECKED;
                AppendMenuW(menu, flags, kMenuBase + (UINT)i,
                            Utf8ToWide(it.options[i].first).c_str());
            }
            POINT pt;
            pt.x = it.x;
            pt.y = it.y + it.h;
            ClientToScreen(g.hwnd, &pt);
            const int got = (int)TrackPopupMenu(menu,
                TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, g.hwnd, NULL);
            DestroyMenu(menu);
            if (got < kMenuBase) return;
            const size_t idx = (size_t)(got - kMenuBase);
            if (idx >= it.options.size()) return;
            picked = it.options[idx].second;
        } else {
            // 窓が無いとき（テスト）は、打ちこみと同じ道を通します
            g.choiceItem = it.id;
            BeginEditRect(JPath(), it.id, it.value, false, it.x, it.y, it.w, it.h);
            return;
        }
        PushUndo();
        SetPanelValue(it.id, picked);
        MarkDirty();
        Refresh();
        return;
    }

    // ---- うごきのたな
    if (StartsWith(it.id, "anim.")) {
        std::vector<std::string> part;
        SplitId(it.id, &part);
        JValue* anims = AnimList();
        if (!anims) return;

        if (part.size() == 2 && part[1] == "add") {
            PushUndo();
            anims->arr.push_back(MakeAnimation((int)anims->arr.size()));
            MarkDirty();
            Refresh();
            return;
        }
        const int ai = (part.size() >= 2) ? NumOf(part[1]) : -1;
        if (ai < 0 || ai >= (int)anims->arr.size()) return;
        JValue& anim = anims->arr[(size_t)ai];

        if (part.size() == 3 && part[2] == "del") {
            PushUndo();
            anims->arr.erase(anims->arr.begin() + ai);
            MarkDirty();
            Refresh();
            return;
        }
        if (part.size() == 4 && part[2] == "pattern" && part[3] == "add") {
            PushUndo();
            JValue* list = AnimSub(anim, "patterns");
            if (list) list->arr.push_back(MakePattern());
            MarkDirty();
            Refresh();
            return;
        }
        if (part.size() == 4 && part[2] == "area" && part[3] == "add") {
            PushUndo();
            JValue* list = AnimSub(anim, "collisions");
            if (list) list->arr.push_back(MakeCollision((int)list->arr.size()));
            MarkDirty();
            Refresh();
            return;
        }
        if (part.size() == 5 && part[4] == "del") {
            const char* key = (part[2] == "pattern") ? "patterns" : "collisions";
            JValue* list = AnimSub(anim, key);
            const int k = NumOf(part[3]);
            if (!list || k < 0 || k >= (int)list->arr.size()) return;
            PushUndo();
            list->arr.erase(list->arr.begin() + k);
            MarkDirty();
            Refresh();
            return;
        }

        // ---- 欄を打ちかえる
        PushUndo();
        JPath path = JPath().Then(JStep::Key("animations")).Then(JStep::Index(ai));
        std::string key;
        if (part.size() == 3) {
            key = part[2];
        } else if (part.size() == 5) {
            const int k = NumOf(part[3]);
            const char* listKey = (part[2] == "pattern") ? "patterns" : "collisions";
            path = path.Then(JStep::Key(listKey)).Then(JStep::Index(k));
            key = part[4];
        } else {
            return;
        }
        const JValue* owner = JResolve(g.project, path);
        if (!owner) return;
        // 数の欄かどうか（名前・かたち・きっかけ・かどのならび だけが字です）
        const bool text = (key == "name" || key == "shape" || key == "interval"
                           || key == "points");
        BeginEditRect(path, key, (*owner)[key.c_str()].asStr(), !text,
                      it.x, it.y, it.w, it.h);
        return;
    }

    // ---- 立ち絵のたな
    if (StartsWith(it.id, "shell.")) {
        JValue* sh = JResolve(g.project, JPath().Then(JStep::Key("shell")));
        if (!sh || !sh->isObj()) return;
        const std::string key = it.id.substr(6);

        if (it.kind == ItemKind::Color) {
            PushUndo();
            if (g.hwnd) {
                // 色えらび（Windows 2000 から使える、古いほうの窓）
                COLORREF custom[16];
                for (int i = 0; i < 16; i++) custom[i] = RGB(255, 255, 255);
                CHOOSECOLORW cc;
                ZeroMemory(&cc, sizeof(cc));
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = g.hwnd;
                cc.lpCustColors = custom;
                cc.rgbResult = ColorFromHex((*sh)[key.c_str()].asStr().c_str(),
                                            RGB(255, 255, 255));
                cc.Flags = CC_RGBINIT | CC_FULLOPEN;
                if (!ChooseColorW(&cc)) return;
                char buf[16];
                sprintf(buf, "#%02x%02x%02x", GetRValue(cc.rgbResult),
                        GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
                sh->set(key, JValue::makeStr(buf));
                MarkDirty();
                ForgetShellPics();
                Refresh();
                return;
            }
            // 窓が無いとき（テスト）は、字として打ちこみます
            BeginEditRect(JPath().Then(JStep::Key("shell")), key,
                          (*sh)[key.c_str()].asStr(), false, it.x, it.y, it.w, it.h);
            return;
        }

        if (key == "balloonEnabled") {
            PushUndo();
            sh->set(key, JValue::makeBool(!(*sh)["balloonEnabled"].asBool(false)));
            MarkDirty();
            Refresh();
            return;
        }

        if (StartsWith(key, "pick.")) {
            if (!g.hwnd) return;
            const int surface = IdIndex(it.id);
            wchar_t buf[MAX_PATH];
            buf[0] = 0;
            OPENFILENAMEW ofn;
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = g.hwnd;
            ofn.lpstrFilter = L"立ち絵の画像 (*.png)\0*.png\0すべて\0*.*\0";
            ofn.lpstrFile = buf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (!GetOpenFileNameW(&ofn)) return;
            PushUndo();
            SetShellImage(surface, WideToUtf8(buf));
            MarkDirty();
            ForgetShellPics();
            Refresh();
            return;
        }

        if (StartsWith(key, "clear.")) {
            PushUndo();
            SetShellImage(IdIndex(it.id), std::string());
            MarkDirty();
            ForgetShellPics();
            Refresh();
            return;
        }
        return;
    }

    // ---- 書き出しのたな
    if (it.id == "export.dir") {
        const std::wstring picked = AskFolder();
        if (!picked.empty()) {
            g.panel.exportDir = WideToUtf8(picked);
            Refresh();
        }
        return;
    }
    if (it.id == "export.folder" || it.id == "export.nar") {
        DoExport(it.id == "export.nar");
        Refresh();
        return;
    }

    // ---- さがすたな
    if (it.id == "search.query") {
        BeginEditRect(JPath(), "search.query", g.panel.query, false,
                      it.x, it.y, it.w, it.h);
        g.editToQuery = true;
        return;
    }
    if (StartsWith(it.id, "search.hit.")) {
        const int i = IdIndex(it.id);
        std::vector<SearchHit> hits;
        SearchProject(g.project, g.panel.query, &hits);
        if (i < 0 || i >= (int)hits.size()) return;

        // そのかたまりが見えるところまで動かします
        const int si = hits[(size_t)i].scriptIndex;
        if (si < 0 || si >= (int)g.layouts.size()) return;
        const JValue& script = g.project["scripts"].at((size_t)si);
        const RECT canvas = CanvasRect();
        g.scrollX = script["x"].asInt(0) - 40;
        g.scrollY = script["y"].asInt(0) - 40;
        if (g.scrollX < 0) g.scrollX = 0;
        if (g.scrollY < 0) g.scrollY = 0;
        (void)canvas;
        Repaint();
        return;
    }
}

// ------------------------------------------------------------------ マウス

/** 押されたところにあるものを見て、つまむ。 */
void OnLeftDown(int sx, int sy) {
    CommitEdit(true);           // 別のところを押したら、打ちこみは決まったことにします
    if (g.hwnd) SetFocus(g.hwnd);

    // ---- 右の作業だな
    if (InPanel(sx, sy)) { OnPanelClick(sx, sy); return; }

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

void PaintPanel(HDC dc);   // 下で書いています

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

    // ---- 左のブロック置き場と、右の作業だな
    PaintPalette(dc, client);
    PaintPanel(dc);
}

void PaintPanel(HDC dc) {
    const RECT panel = PanelRect();

    HBRUSH bg = CreateSolidBrush(RGB(0xff, 0xff, 0xff));
    FillRect(dc, &panel, bg);
    DeleteObject(bg);

    // ---- 見出し
    for (int i = 0; i < kTabCount; i++) {
        RECT rc = TabRect(i);
        const bool on = ((int)g.panel.tab == i);
        HBRUSH b = CreateSolidBrush(on ? RGB(0xff, 0xff, 0xff) : RGB(0xe4, 0xe8, 0xf0));
        FillRect(dc, &rc, b);
        DeleteObject(b);

        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xc8, 0xcf, 0xdc));
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, rc.left, rc.bottom - 1, NULL);
        LineTo(dc, rc.right, rc.bottom - 1);
        MoveToEx(dc, rc.right - 1, rc.top, NULL);
        LineTo(dc, rc.right - 1, rc.bottom);
        SelectObject(dc, op);
        DeleteObject(pen);

        HGDIOBJ of = SelectObject(dc, g.tools.slotFont);
        SetTextColor(dc, on ? RGB(0x22, 0x26, 0x33) : RGB(0x5a, 0x62, 0x74));
        SetBkMode(dc, TRANSPARENT);
        std::wstring name = Utf8ToWide(TabName(TabAt(i)));
        DrawTextW(dc, name.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
    }

    // ---- 中身
    RECT body = panel;
    body.top += kTabH * kTabRows;
    HRGN clip = CreateRectRgn(body.left, body.top, body.right, body.bottom);
    SelectClipRgn(dc, clip);
    SetBkMode(dc, TRANSPARENT);

    for (size_t i = 0; i < g.panelItems.size(); i++) {
        const PanelItem& it = g.panelItems[i];
        if (it.y + it.h < body.top || it.y > body.bottom) continue;
        RECT rc;
        rc.left = it.x;
        rc.top = it.y;
        rc.right = it.x + it.w;
        rc.bottom = it.y + it.h;

        switch (it.kind) {
            case ItemKind::Head: {
                HGDIOBJ of = SelectObject(dc, g.tools.blockFont);
                SetTextColor(dc, RGB(0x22, 0x26, 0x33));
                std::wstring t = Utf8ToWide(it.text);
                DrawTextW(dc, t.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dc, of);
                break;
            }
            case ItemKind::Hint:
            case ItemKind::Text: {
                HGDIOBJ of = SelectObject(dc, g.tools.slotFont);
                SetTextColor(dc, it.kind == ItemKind::Hint ? RGB(0x77, 0x7f, 0x90)
                                                           : RGB(0x33, 0x38, 0x45));
                std::wstring t = Utf8ToWide(it.text);
                DrawTextW(dc, t.c_str(), -1, &rc,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(dc, of);
                break;
            }
            case ItemKind::Button: {
                HBRUSH b = CreateSolidBrush(RGB(0xef, 0xf2, 0xf8));
                FillRect(dc, &rc, b);
                DeleteObject(b);
                HBRUSH edge = CreateSolidBrush(RGB(0xc8, 0xcf, 0xdc));
                FrameRect(dc, &rc, edge);
                DeleteObject(edge);
                HGDIOBJ of = SelectObject(dc, g.tools.slotFont);
                SetTextColor(dc, RGB(0x22, 0x26, 0x33));
                std::wstring t = Utf8ToWide(it.text);
                DrawTextW(dc, t.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dc, of);
                break;
            }
            case ItemKind::Choice:
            case ItemKind::Field: {
                RECT label = rc;
                label.right = rc.left + 96;
                RECT box = rc;
                box.left = label.right + 6;

                HGDIOBJ of = SelectObject(dc, g.tools.slotFont);
                SetTextColor(dc, RGB(0x5a, 0x62, 0x74));
                std::wstring t = Utf8ToWide(it.text);
                DrawTextW(dc, t.c_str(), -1, &label,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                HBRUSH b = CreateSolidBrush(RGB(0xff, 0xff, 0xff));
                FillRect(dc, &box, b);
                DeleteObject(b);
                HBRUSH edge = CreateSolidBrush(RGB(0xc8, 0xcf, 0xdc));
                FrameRect(dc, &box, edge);
                DeleteObject(edge);

                RECT inner = box;
                inner.left += 5;
                if (it.kind == ItemKind::Choice) {
                    inner.right -= 14;
                    // 右はしに、えらべる印（▼）
                    const int cx = box.right - 9;
                    const int cy = (box.top + box.bottom) / 2;
                    POINT tri[3] = { { cx - 4, cy - 2 }, { cx + 4, cy - 2 }, { cx, cy + 3 } };
                    HBRUSH t = CreateSolidBrush(RGB(0x5a, 0x62, 0x74));
                    HGDIOBJ ob = SelectObject(dc, t);
                    HGDIOBJ op2 = SelectObject(dc, GetStockObject(NULL_PEN));
                    Polygon(dc, tri, 3);
                    SelectObject(dc, op2);
                    SelectObject(dc, ob);
                    DeleteObject(t);
                }
                SetTextColor(dc, RGB(0x22, 0x26, 0x33));
                std::wstring v = Utf8ToWide(it.value);
                DrawTextW(dc, v.c_str(), -1, &inner,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(dc, of);
                break;
            }
            case ItemKind::Color: {
                RECT label = rc;
                label.right = rc.left + 96;
                RECT box = rc;
                box.left = label.right + 6;

                HGDIOBJ of = SelectObject(dc, g.tools.slotFont);
                SetTextColor(dc, RGB(0x5a, 0x62, 0x74));
                std::wstring t = Utf8ToWide(it.text);
                DrawTextW(dc, t.c_str(), -1, &label,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                HBRUSH b = CreateSolidBrush(RGB(0xff, 0xff, 0xff));
                FillRect(dc, &box, b);
                DeleteObject(b);
                HBRUSH edge = CreateSolidBrush(RGB(0xc8, 0xcf, 0xdc));
                FrameRect(dc, &box, edge);
                DeleteObject(edge);

                // 右はしに、その色の四角
                RECT chip = box;
                chip.left = box.right - 34;
                InflateRect(&chip, -4, -4);
                HBRUSH c = CreateSolidBrush(ColorFromHex(it.value.c_str(),
                                                         RGB(255, 255, 255)));
                FillRect(dc, &chip, c);
                DeleteObject(c);
                FrameRect(dc, &chip, (HBRUSH)GetStockObject(GRAY_BRUSH));

                RECT inner = box;
                inner.left += 5;
                inner.right = chip.left - 4;
                SetTextColor(dc, RGB(0x22, 0x26, 0x33));
                std::wstring v = Utf8ToWide(it.value);
                DrawTextW(dc, v.c_str(), -1, &inner,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(dc, of);
                break;
            }
            case ItemKind::Image: {
                HBRUSH b = CreateSolidBrush(RGB(0xf7, 0xf9, 0xfd));
                FillRect(dc, &rc, b);
                DeleteObject(b);
                HBRUSH edge = CreateSolidBrush(RGB(0xdd, 0xe2, 0xec));
                FrameRect(dc, &rc, edge);
                DeleteObject(edge);

                const ShellPic* pic = ShellPicFor(it.surface);
                if (pic && pic->w > 0) {
                    // 高さに合わせて、はみ出さないように縮めます
                    const int destH = rc.bottom - rc.top - 8;
                    int destW = pic->w * destH / pic->h;
                    if (destW > 80) { destW = 80; }
                    BITMAPINFO bi;
                    ZeroMemory(&bi, sizeof(bi));
                    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bi.bmiHeader.biWidth = pic->w;
                    bi.bmiHeader.biHeight = pic->h;
                    bi.bmiHeader.biPlanes = 1;
                    bi.bmiHeader.biBitCount = 32;
                    bi.bmiHeader.biCompression = BI_RGB;
                    SetStretchBltMode(dc, COLORONCOLOR);
                    StretchDIBits(dc, rc.left + 4, rc.top + 4, destW, destH,
                                  0, 0, pic->w, pic->h, &pic->bgr[0], &bi,
                                  DIB_RGB_COLORS, SRCCOPY);
                }

                HGDIOBJ of = SelectObject(dc, g.tools.slotFont);
                RECT line = rc;
                line.left += 92;
                line.bottom = rc.top + 24;
                SetTextColor(dc, RGB(0x22, 0x26, 0x33));
                std::wstring t = Utf8ToWide(it.text);
                DrawTextW(dc, t.c_str(), -1, &line,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                RECT sub = rc;
                sub.left += 92;
                sub.top = rc.top + 24;
                SetTextColor(dc, RGB(0x88, 0x90, 0xa0));
                std::wstring v = Utf8ToWide(it.value.empty()
                    ? "下の色から作ります。押すと画像をえらべます" : it.value);
                DrawTextW(dc, v.c_str(), -1, &sub,
                          DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
                SelectObject(dc, of);
                break;
            }
            case ItemKind::Row: {
                HBRUSH b = CreateSolidBrush(it.mark == 2 ? RGB(0xff, 0xf0, 0xf0)
                                          : it.mark == 1 ? RGB(0xff, 0xfa, 0xe8)
                                                         : RGB(0xf7, 0xf9, 0xfd));
                FillRect(dc, &rc, b);
                DeleteObject(b);
                HBRUSH edge = CreateSolidBrush(RGB(0xdd, 0xe2, 0xec));
                FrameRect(dc, &rc, edge);
                DeleteObject(edge);

                HGDIOBJ of = SelectObject(dc, g.tools.slotFont);
                RECT line = rc;
                line.left += 6;
                line.bottom = rc.top + 18;
                SetTextColor(dc, RGB(0x22, 0x26, 0x33));
                std::wstring t = Utf8ToWide(it.text);
                DrawTextW(dc, t.c_str(), -1, &line,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                if (!it.sub.empty()) {
                    RECT sub = rc;
                    sub.left += 6;
                    sub.top = rc.top + 17;
                    SetTextColor(dc, RGB(0x88, 0x90, 0xa0));
                    std::wstring u = Utf8ToWide(it.sub);
                    DrawTextW(dc, u.c_str(), -1, &sub,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
                SelectObject(dc, of);
                break;
            }
        }
    }
    SelectClipRgn(dc, NULL);
    DeleteObject(clip);

    // ---- 左のふち
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0xc8, 0xcf, 0xdc));
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, panel.left, panel.top, NULL);
    LineTo(dc, panel.left, panel.bottom);
    SelectObject(dc, op);
    DeleteObject(pen);
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
            if (InPanel(pt.x, pt.y)) {
                g.panel.scroll -= delta / 2;
                if (g.panel.scroll < 0) g.panel.scroll = 0;
                Refresh();
            } else if (InPalette(pt.x, pt.y)) {
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

        case WM_DESTROY: {
            // 次に開いたとき、同じところに出せるように覚えておきます
            WINDOWPLACEMENT wp;
            wp.length = sizeof(wp);
            if (GetWindowPlacement(hwnd, &wp)) {
                const RECT& r = wp.rcNormalPosition;
                g.lastPlacement.x = r.left;
                g.lastPlacement.y = r.top;
                g.lastPlacement.w = r.right - r.left;
                g.lastPlacement.h = r.bottom - r.top;
                g.lastPlacement.maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
            }
            PostQuitMessage(0);
            return 0;
        }
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
    NormalizeProject(g.project);
    if (!ghostPath.empty()) {
        std::string text;
        JValue root;
        std::string err;
        if (!ReadTextFile(ghostPath, text) || !JsonParse(text, root, err) || !root.isObj()) {
            g.tools.Free();
            return false;
        }
        NormalizeProject(root);
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
    if (probe.tab >= 0) {
        g.panel.tab = TabAt(probe.tab);
        Relayout();
    }

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

bool ProbePanel(const PanelProbe& probe, std::string* items, std::string* json) {
    if (!SetUpHeadless(probe.ghostPath, probe.width, probe.height, 0, 0)) return false;

    g.panel.tab = TabAt(probe.tab);
    g.panel.query = probe.query;
    g.panel.exportDir = probe.exportDir;
    g.panel.scroll = 0;
    Relayout();

    if (!probe.clickId.empty()) {
        // その目じるしの部品を、まんなかで押したことにします
        int hit = -1;
        for (size_t i = 0; i < g.panelItems.size(); i++) {
            if (g.panelItems[i].id == probe.clickId) { hit = (int)i; break; }
        }
        if (hit < 0) { g.tools.Free(); return false; }
        const PanelItem it = g.panelItems[(size_t)hit];
        OnPanelClick(it.x + 4, it.y + it.h / 2);

        // 打ちこむ欄なら、窓が無くても中身を入れられるようにします
        // （入れ先の決めかたは、窓を出しているときと同じ ApplyEditText を通ります）
        if (probe.type && (g.editing || g.editToQuery)) {
            ApplyEditText(probe.typeValue);
            g.editing = false;
            g.editToQuery = false;
            g.choiceItem.clear();
            g.editArg.clear();
        }
        Relayout();
    }

    if (items) {
        std::string out;
        for (size_t i = 0; i < g.panelItems.size(); i++) {
            const PanelItem& it = g.panelItems[i];
            const char* kind = "text";
            switch (it.kind) {
                case ItemKind::Head:   kind = "head"; break;
                case ItemKind::Hint:   kind = "hint"; break;
                case ItemKind::Button: kind = "button"; break;
                case ItemKind::Field:  kind = "field"; break;
                case ItemKind::Row:    kind = "row"; break;
                case ItemKind::Color:  kind = "color"; break;
                case ItemKind::Image:  kind = "image"; break;
                case ItemKind::Choice: kind = "choice"; break;
                default: break;
            }
            char buf[64];
            sprintf(buf, " (%4d,%4d) %4dx%-4d ", it.x, it.y, it.w, it.h);
            out += std::string(kind) + " " + (it.id.empty() ? "-" : it.id) + buf
                 + it.text;
            if (!it.value.empty()) out += " = " + it.value;
            if (!it.sub.empty()) out += " / " + it.sub;
            out += "\n";
        }
        // いまえらんでいるたなと、さがす言葉も言っておきます
        out += std::string("たな ") + TabName(g.panel.tab) + "\n";
        out += "さがす言葉 " + g.panel.query + "\n";
        *items = out;
    }
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
                  int scrollX, int scrollY, std::string* png, int tab) {
    EditorProbe probe;
    probe.ghostPath = ghostPath;
    probe.width = width;
    probe.height = height;
    probe.scrollX = scrollX;
    probe.scrollY = scrollY;
    probe.tab = tab;
    return ProbeEditor(probe, png, NULL);
}

void SetShioriDll(const std::string& bytes) { g.shioriDll = bytes; }

const wchar_t* EditorWindowClass() { return kClass; }

int RunEditor(HINSTANCE hInstance, const EditorOptions& options, EditorState* stateOut) {
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
    wc.hIcon = options.icon;
    wc.hIconSm = options.iconSmall;
    if (!RegisterClassExW(&wc)) return 1;

    // 前に覚えていた場所へ。はみ出していたら、まんなかに出しなおします。
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int maxW = work.right - work.left;
    const int maxH = work.bottom - work.top;
    int w = options.w > 0 ? options.w : 1100;
    int h = options.h > 0 ? options.h : 760;
    if (w < kPaletteW + kMinCanvas + kPanelW) w = kPaletteW + kMinCanvas + kPanelW;
    if (h < 400) h = 400;
    if (w > maxW) w = maxW;
    if (h > maxH) h = maxH;
    int x = options.w > 0 ? options.x : work.left + (maxW - w) / 2;
    int y = options.h > 0 ? options.y : work.top + (maxH - h) / 2;
    if (x < work.left - 40 || x > work.right - 80) x = work.left + (maxW - w) / 2;
    if (y < work.top - 10 || y > work.bottom - 80) y = work.top + (maxH - h) / 2;

    HWND hwnd = CreateWindowExW(0, kClass, L"なしスタジオ", WS_OVERLAPPEDWINDOW,
                                x, y, w, h, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 1;
    g.hwnd = hwnd;

    if (!g.tools.Create()) {
        MessageBoxW(hwnd, L"文字の用意ができませんでした。", L"なしスタジオ", MB_ICONERROR);
        return 1;
    }

    if (g.project.isNull()) {
        g.project = JValue::makeObj();
        g.project.set("scripts", JValue::makeArr());
        NormalizeProject(g.project);
    }
    if (!options.ghostPath.empty() && !LoadGhost(options.ghostPath)) {
        MessageBoxW(hwnd, L"ghost.json を読めませんでした。", L"なしスタジオ", MB_ICONWARNING);
    }
    Relayout();
    UpdateTitle();

    ShowWindow(hwnd, options.maximized ? SW_SHOWMAXIMIZED : SW_SHOW);
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
    if (stateOut) *stateOut = g.lastPlacement;
    g.tools.Free();
    return 0;
}

} // namespace w2k
} // namespace nashi
