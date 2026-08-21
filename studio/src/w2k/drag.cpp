#include "drag.h"

#include <cmath>
#include <cstdio>
#include <map>

namespace nashi {
namespace w2k {

namespace {

const int SNAP_STACK = 46;   // つながる先をさがす広さ（px）
const int SNAP_SLOT  = 34;   // 欄にはまる広さ（px）
const int MARKER_MIN_W = 80; // 目印のいちばん細い幅

/** そのブロックの定義（無ければ NULL）。 */
const BlockDef* DefOf(const JValue& block) {
    if (!block.isObj()) return NULL;
    return FindBlockFor(block["type"].asStr(), block["op"].asStr());
}

bool IsCap(const JValue& block) {
    const BlockDef* d = DefOf(block);
    return d && d->shape == Shape::Cap;
}

/** ghost.json の場所 → たどりかた、の対応表を作る。 */
void MapPaths(const JValue& v, const JPath& here,
              std::map<const JValue*, JPath>* out) {
    (*out)[&v] = here;
    if (v.isObj()) {
        for (size_t i = 0; i < v.obj.size(); i++) {
            MapPaths(v.obj[i].second, here.Then(JStep::Key(v.obj[i].first)), out);
        }
    } else if (v.isArr()) {
        for (size_t i = 0; i < v.arr.size(); i++) {
            MapPaths(v.arr[i], here.Then(JStep::Index((int)i)), out);
        }
    }
}

/** つまんでいるものが、その欄に入れるか。 */
bool SlotAccepts(const Piece& slot, DragShape shape) {
    if (slot.childCount > 0) return false;              // もう何か入っている
    if (slot.argKind != ArgKind::Input) return false;   // えらぶ欄には入れない
    if (slot.boolSlot) return shape == DragShape::Boolean;
    return shape == DragShape::Reporter;
}

/**
 * ならび 1 つぶんの置き場所を足す。
 *   listPath  そのならび（配列）の場所
 *   list      中身
 *   pieceOf   ブロックの場所 → かけらの番号
 *   fx, fy    中身が空のときの、目印の場所
 *   width     目印の幅
 */
void AddStackTargets(const JPath& listPath, const JValue& list,
                     const std::map<const JValue*, int>& pieceOf, const Layout& lay,
                     int fx, int fy, int width, bool endsWithCap,
                     std::vector<DropTarget>* out) {
    const int n = (int)list.size();
    for (int i = 0; i <= n; i++) {
        if (i > 0 && IsCap(list.at(i - 1))) continue;   // おわりのブロックの後ろにはつなげない
        if (endsWithCap && i < n) continue;             // おわりのブロックの後ろは残せない

        int x = fx, y = fy, w = width;
        if (i < n) {
            std::map<const JValue*, int>::const_iterator it = pieceOf.find(&list.at(i));
            if (it == pieceOf.end()) continue;
            const Piece& p = lay.pieces[it->second];
            x = p.x; y = p.y;
            if (p.w > w) w = p.w;
        } else if (n > 0) {
            std::map<const JValue*, int>::const_iterator it = pieceOf.find(&list.at(n - 1));
            if (it == pieceOf.end()) continue;
            const Piece& p = lay.pieces[it->second];
            x = p.x; y = p.y + p.h - 6;
            if (p.w > w) w = p.w;
        }

        DropTarget t;
        t.kind = DropKind::Stack;
        t.owner = listPath;
        t.index = i;
        t.x = x;
        t.y = y;
        t.w = (w < MARKER_MIN_W) ? MARKER_MIN_W : w;
        t.h = 6;
        out->push_back(t);
    }
}

} // namespace

// ------------------------------------------------------------------ 道しるべ
std::string JPath::ToString() const {
    std::string s;
    for (size_t i = 0; i < steps.size(); i++) {
        if (steps[i].isIndex) {
            char buf[24];
            sprintf(buf, "[%d]", steps[i].index);
            s += buf;
        } else {
            if (!s.empty()) s += ".";
            s += steps[i].key;
        }
    }
    return s;
}

JValue* JResolve(JValue& root, const JPath& path) {
    JValue* cur = &root;
    for (size_t i = 0; i < path.steps.size(); i++) {
        const JStep& st = path.steps[i];
        if (st.isIndex) {
            if (!cur->isArr() || st.index < 0 || st.index >= (int)cur->arr.size()) return NULL;
            cur = &cur->arr[(size_t)st.index];
        } else {
            if (!cur->isObj()) return NULL;
            JValue* next = NULL;
            for (size_t k = 0; k < cur->obj.size(); k++) {
                if (cur->obj[k].first == st.key) { next = &cur->obj[k].second; break; }
            }
            if (!next) return NULL;
            cur = next;
        }
    }
    return cur;
}

const JValue* JResolve(const JValue& root, const JPath& path) {
    return JResolve(const_cast<JValue&>(root), path);
}

// -------------------------------------------------------------- 置ける場所
void CollectDropTargets(const Layout& lay, const JValue& script, const JPath& scriptPath,
                        const Metrics& m, DragShape shape, bool endsWithCap,
                        std::vector<DropTarget>* out) {
    out->clear();

    std::map<const JValue*, JPath> pathOf;
    MapPaths(script, scriptPath, &pathOf);

    // かけらのうち、ブロックのものを引けるようにしておく
    std::map<const JValue*, int> pieceOf;
    for (size_t i = 0; i < lay.pieces.size(); i++) {
        const Piece& p = lay.pieces[i];
        if (p.kind == PieceKind::Block && p.node) pieceOf[p.node] = (int)i;
    }

    if (shape == DragShape::Stack) {
        // いちばん外がわのならび。空のときは、帽子のすぐ下に置きます。
        int fx = 0, fy = 0, fw = MARKER_MIN_W;
        for (size_t i = 0; i < lay.pieces.size(); i++) {
            const Piece& p = lay.pieces[i];
            if (p.kind == PieceKind::Block && p.def && p.def->hat) {
                fx = p.x; fy = p.y + p.h; fw = p.w;
                break;
            }
        }
        AddStackTargets(scriptPath.Then(JStep::Key("blocks")), script["blocks"],
                        pieceOf, lay, fx, fy, fw, endsWithCap, out);

        // C ブロックの中（腕）
        for (size_t i = 0; i < lay.pieces.size(); i++) {
            const Piece& p = lay.pieces[i];
            if (p.kind != PieceKind::Arm || !p.node) continue;
            std::map<const JValue*, JPath>::const_iterator it = pathOf.find(p.node);
            if (it == pathOf.end()) continue;
            AddStackTargets(it->second.Then(JStep::Key(p.subKey)), (*p.node)[p.subKey.c_str()],
                            pieceOf, lay, p.x, p.y, p.w, endsWithCap, out);
        }
        return;
    }

    // 丸いもの・六角のもの。空いている欄をさがします。
    for (size_t i = 0; i < lay.pieces.size(); i++) {
        const Piece& p = lay.pieces[i];
        if (p.kind != PieceKind::Slot || !p.node) continue;
        if (!SlotAccepts(p, shape)) continue;
        std::map<const JValue*, JPath>::const_iterator it = pathOf.find(p.node);
        if (it == pathOf.end()) continue;

        DropTarget t;
        t.kind = DropKind::Slot;
        t.owner = it->second;
        t.argName = p.argName;
        t.boolSlot = p.boolSlot;
        t.x = p.x;
        t.y = p.y;
        t.w = p.w;
        t.h = p.h;
        out->push_back(t);
    }
    (void)m;
}

int NearestDropTarget(const std::vector<DropTarget>& targets, DragShape shape, int x, int y) {
    int best = -1;
    double bestDist = 0.0;
    for (size_t i = 0; i < targets.size(); i++) {
        const DropTarget& t = targets[i];
        double dist, limit;
        if (t.kind == DropKind::Stack) {
            double dx = x - t.x, dy = y - t.y;
            // 縦のずれを重く見ます（横は多少ちがっても、つなぎたいはずなので）
            dist = std::sqrt(dx * dx * 0.55 + dy * dy);
            limit = SNAP_STACK;
        } else {
            // 欄は、左のはしのあたりでくらべます
            double cx = t.x + (t.w < 40 ? t.w / 2.0 : 20.0);
            double cy = t.y + t.h / 2.0;
            double dx = x - cx, dy = y - cy;
            dist = std::sqrt(dx * dx + dy * dy);
            limit = SNAP_SLOT;
        }
        if (dist >= limit) continue;
        if (best < 0 || dist < bestDist) { best = (int)i; bestDist = dist; }
    }
    (void)shape;
    return best;
}

// ------------------------------------------------------------ つまむ・はなす
JValue EmptySlotValue(const ArgDef* arg) {
    if (!arg || arg->mode == ArgMode::Bool) return JValue();          // null
    return JValue::makeStr(arg->defValue ? arg->defValue : "");
}

JValue MakeBlock(const BlockDef& def) {
    JValue b = JValue::makeObj();
    b.set("type", JValue::makeStr(def.type ? def.type : ""));

    // 決めうちの値（"arith#+" なら op に "+" が入る、など）
    int nf = 0;
    const FixedDef* fixed = BlockFixed(def, &nf);
    for (int i = 0; i < nf; i++) b.set(fixed[i].key, JValue::makeStr(fixed[i].value));

    // 引数の既定値
    int na = 0;
    const ArgDef* args = BlockArgs(def, &na);
    for (int i = 0; i < na; i++) {
        b.set(args[i].name, EmptySlotValue(&args[i]));
    }

    // 中に入る腕（からっぽのならび）
    int ns = 0;
    const SubDef* subs = BlockSubs(def, &ns);
    for (int i = 0; i < ns; i++) b.set(subs[i].key, JValue::makeArr());

    // 「つぎのどれかを」は、はじめから 2 とおり用意しておきます
    if (def.dynamic && def.dynamic[0]) {
        JValue two = JValue::makeArr();
        two.arr.push_back(JValue::makeArr());
        two.arr.push_back(JValue::makeArr());
        b.set(def.dynamic, two);
    }
    return b;
}

JValue MakeScript(const BlockDef& hat) {
    JValue s = MakeBlock(hat);
    // かたまりの帽子は ghost.json に "type" を持ちません（kind で見分けます）
    for (size_t i = 0; i < s.obj.size(); i++) {
        if (s.obj[i].first == "type") { s.obj.erase(s.obj.begin() + i); break; }
    }
    s.set("kind", JValue::makeStr(hat.scriptKind ? hat.scriptKind : "event"));
    s.set("blocks", JValue::makeArr());
    if (hat.scriptKind && std::string(hat.scriptKind) != "event") {
        s.set("name", JValue::makeStr("あたらしいトーク"));
        s.set("weight", JValue::makeNum(1));
    }
    return s;
}

bool PickUpStack(JValue& root, const JPath& listPath, int index, JValue* outBlocks) {
    JValue* list = JResolve(root, listPath);
    if (!list || !list->isArr()) return false;
    if (index < 0 || index >= (int)list->arr.size()) return false;

    JValue taken = JValue::makeArr();
    for (size_t i = (size_t)index; i < list->arr.size(); i++) taken.arr.push_back(list->arr[i]);
    list->arr.resize((size_t)index);
    if (outBlocks) *outBlocks = taken;
    return true;
}

bool PickUpSlot(JValue& root, const JPath& ownerPath, const std::string& argName,
                const ArgDef* arg, JValue* outBlock) {
    JValue* owner = JResolve(root, ownerPath);
    if (!owner || !owner->isObj()) return false;
    for (size_t i = 0; i < owner->obj.size(); i++) {
        if (owner->obj[i].first != argName) continue;
        JValue& slot = owner->obj[i].second;
        if (!slot.isObj() || slot["type"].asStr().empty()) return false;
        if (outBlock) *outBlock = slot;
        slot = EmptySlotValue(arg);
        return true;
    }
    return false;
}

bool DropAt(JValue& root, const DropTarget& target, const JValue& payload) {
    if (target.kind == DropKind::Stack) {
        JValue* list = JResolve(root, target.owner);
        if (!list || !list->isArr()) return false;
        int at = target.index;
        if (at < 0) at = 0;
        if (at > (int)list->arr.size()) at = (int)list->arr.size();

        if (payload.isArr()) {
            list->arr.insert(list->arr.begin() + at, payload.arr.begin(), payload.arr.end());
        } else if (payload.isObj()) {
            list->arr.insert(list->arr.begin() + at, payload);
        } else {
            return false;
        }
        return true;
    }

    // 欄にはめる
    JValue* owner = JResolve(root, target.owner);
    if (!owner || !owner->isObj()) return false;
    const JValue& block = payload.isArr() ? (payload.arr.empty() ? JValue::Null() : payload.arr[0])
                                          : payload;
    if (!block.isObj()) return false;
    owner->set(target.argName, block);
    return true;
}

} // namespace w2k
} // namespace nashi
