#include "exporter.h"
#include "image.h"
#include "zip.h"
#include "fsutil.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>

namespace nashi {

// ------------------------------------------------------------------ 名前

std::string SafeFolderName(const std::string& name, const char* fallback) {
    std::string out;
    for (size_t i = 0; i < name.size(); i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20) continue;
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') continue;
        out += (char)c;
    }
    while (!out.empty() && (out[0] == '.' || out[0] == ' ')) out.erase(0, 1);
    while (!out.empty() && (out[out.size() - 1] == '.' || out[out.size() - 1] == ' ')) out.erase(out.size() - 1);
    return out.empty() ? std::string(fallback) : out;
}

static std::string Line(const char* key, const std::string& value) {
    std::string v = value;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\r' || v[i] == '\n') v[i] = ' ';
    }
    return std::string(key) + "," + v + "\r\n";
}

// -------------------------------------------------------- 仮シェルの生成

static const Rgb kSkin = { 255, 232, 216 };
static const Rgb kLine = { 78, 62, 70 };
static const Rgb kWhite = { 255, 255, 255 };
static const Rgb kBlush = { 255, 150, 160 };
static const Rgb kMouth = { 190, 90, 110 };

// face: 0=ふつう 1=わらい 2=おどろき
static std::string DrawCharacter(int w, int h, const std::string& hairHex,
                                 const std::string& clothHex, int face) {
    Canvas c(w, h);
    Rgb hair = RgbFromHex(hairHex, Rgb{ 240, 140, 170 });
    Rgb cloth = RgbFromHex(clothHex, Rgb{ 110, 130, 200 });
    double cx = w / 2.0;
    double headR = w * 0.30;
    double headY = h * 0.33;
    double bodyTop = headY + headR * 0.85;

    std::vector<std::pair<double, double> > dress;
    dress.push_back(std::make_pair(cx - w * 0.16, bodyTop));
    dress.push_back(std::make_pair(cx + w * 0.16, bodyTop));
    dress.push_back(std::make_pair(cx + w * 0.34, h - 2.0));
    dress.push_back(std::make_pair(cx - w * 0.34, h - 2.0));
    c.Polygon(dress, cloth);
    c.RoundRect(cx - w * 0.17, bodyTop - 4, w * 0.34, h * 0.12, 8, Shade(cloth, 0.12));

    c.Ellipse(cx - w * 0.30, bodyTop + h * 0.12, w * 0.07, h * 0.10, Shade(cloth, -0.08));
    c.Ellipse(cx + w * 0.30, bodyTop + h * 0.12, w * 0.07, h * 0.10, Shade(cloth, -0.08));
    c.Ellipse(cx - w * 0.31, bodyTop + h * 0.20, w * 0.055, w * 0.055, kSkin);
    c.Ellipse(cx + w * 0.31, bodyTop + h * 0.20, w * 0.055, w * 0.055, kSkin);

    c.Ellipse(cx, headY + 4, headR * 1.14, headR * 1.12, hair);
    c.Ellipse(cx, headY, headR, headR * 0.98, kSkin);
    c.Ellipse(cx, headY - headR * 0.34, headR * 1.06, headR * 0.72, hair);
    c.Ellipse(cx - headR * 0.92, headY + headR * 0.30, headR * 0.26, headR * 0.72, hair);
    c.Ellipse(cx + headR * 0.92, headY + headR * 0.30, headR * 0.26, headR * 0.72, hair);

    double eyeY = headY + headR * 0.16;
    double eyeDx = headR * 0.42;
    double eyeR = headR * 0.155;

    for (int side = 0; side < 2; side++) {
        double ex = cx + (side == 0 ? -eyeDx : eyeDx);
        if (face == 1) {
            double t = eyeR * 0.36 > 2.0 ? eyeR * 0.36 : 2.0;
            std::vector<std::pair<double, double> > pts;
            pts.push_back(std::make_pair(ex - eyeR, eyeY + eyeR * 0.45));
            pts.push_back(std::make_pair(ex, eyeY - eyeR * 0.5));
            pts.push_back(std::make_pair(ex + eyeR, eyeY + eyeR * 0.45));
            pts.push_back(std::make_pair(ex + eyeR, eyeY + eyeR * 0.45 + t));
            pts.push_back(std::make_pair(ex, eyeY - eyeR * 0.5 + t));
            pts.push_back(std::make_pair(ex - eyeR, eyeY + eyeR * 0.45 + t));
            c.Polygon(pts, kLine);
        } else {
            double s = face == 2 ? 1.35 : 1.0;
            c.Ellipse(ex, eyeY, eyeR * 0.85 * s, eyeR * 1.15 * s, kLine);
            c.Ellipse(ex - eyeR * 0.25, eyeY - eyeR * 0.35, eyeR * 0.28, eyeR * 0.32, kWhite);
        }
    }

    c.Ellipse(cx - headR * 0.62, eyeY + headR * 0.26, headR * 0.2, headR * 0.12, kBlush, 0.55);
    c.Ellipse(cx + headR * 0.62, eyeY + headR * 0.26, headR * 0.2, headR * 0.12, kBlush, 0.55);

    double mouthY = headY + headR * 0.52;
    if (face == 2) {
        c.Ellipse(cx, mouthY, headR * 0.12, headR * 0.17, kMouth);
    } else if (face == 1) {
        std::vector<std::pair<double, double> > m;
        m.push_back(std::make_pair(cx - headR * 0.16, mouthY - headR * 0.06));
        m.push_back(std::make_pair(cx + headR * 0.16, mouthY - headR * 0.06));
        m.push_back(std::make_pair(cx, mouthY + headR * 0.14));
        c.Polygon(m, kMouth);
    } else {
        c.Ellipse(cx, mouthY, headR * 0.09, headR * 0.05, kMouth);
    }
    return c.ToPng();
}

// SERIKO のアニメーション定義。
//
//   animationN.interval,sometimes
//   animationN.patternM,base,1,200,0,0
//
// interval は「いつ動きだすか」、pattern は「どの絵を何ミリ秒出すか」です。
// base は絵をまるごと差しかえる方式で、overlay は上に重ねる方式です。
// プロジェクトの animations に入っているもののうち、その surface のものだけ書きます。
static std::string AnimationLines(const JValue& project, int surfaceId) {
    const JValue& anims = project["animations"];
    if (!anims.isArr()) return std::string();

    std::string out;
    for (size_t i = 0; i < anims.size(); i++) {
        const JValue& a = anims.at(i);
        if (!a.isObj()) continue;
        if (a["base"].asInt(0) != surfaceId) continue;
        if (a["disabled"].asBool(false)) continue;

        int id = a["id"].asInt((int)i);
        if (id < 0 || id > 127) continue;

        std::string interval = a["interval"].asStr("never");
        if (interval.empty()) interval = "never";
        // "random" と "talk" は回数が要る（random,4 のように書く）
        if (interval == "random" || interval == "talk") {
            char n[24];
            sprintf_s(n, ",%d", a["every"].asInt(4) < 1 ? 1 : a["every"].asInt(4));
            interval += n;
        }
        const JValue& pats = a["patterns"];
        std::string body;
        int m = 0;
        for (size_t k = 0; k < pats.size(); k++) {
            const JValue& p = pats.at(k);
            if (!p.isObj()) continue;
            // 重ねかた（SERIKO の描画メソッド）。知らない名前は base に倒す。
            static const char* kMethods[] = {
                "overlay", "overlayfast", "base", "replace",
                "interpolate", "asis", "reduce", "move",
                "blend-multiply", "blend-screen", "blend-overlay", "blend-add",
                "start", "stop", "import",
            };
            std::string method = p["method"].asStr("base");
            bool known = false;
            for (size_t mi = 0; mi < sizeof(kMethods) / sizeof(kMethods[0]); mi++) {
                if (method == kMethods[mi]) { known = true; break; }
            }
            if (!known) method = "base";
            int surf = p["surface"].asInt(0);
            int wait = p["wait"].asInt(200);
            if (wait < 0) wait = 0;
            // こまごとの位置ずらし（基準の絵の左上からの相対）
            int px = p["x"].asInt(0), py = p["y"].asInt(0);
            if (px < -9999) px = -9999; if (px > 9999) px = 9999;
            if (py < -9999) py = -9999; if (py > 9999) py = 9999;
            char buf[320];

            if (method == "start" || method == "stop") {
                // 別のうごきを動かす／止める。番号だけを書く（ウェイトは見られない）
                if (surf < 0 || surf > 127) continue;
                sprintf_s(buf, "animation%d.pattern%d,%s,%d\r\n", id, m++, method.c_str(), surf);
                body += buf;
                continue;
            }
            if (method == "import") {
                // APNG / GIF / WebP をそのまま差しこむ
                std::string file = Trim(p["file"].asStr());
                if (file.empty() || file.size() > 160) continue;
                if (file.find(',') != std::string::npos) continue;      // 1 行が壊れる
                if (file.find("..") != std::string::npos) continue;     // 外へは出さない
                if (file.find('\r') != std::string::npos ||
                    file.find('\n') != std::string::npos) continue;
                sprintf_s(buf, "animation%d.pattern%d,import,%s,%d,%d,%d\r\n",
                          id, m++, file.c_str(), wait, px, py);
                body += buf;
                continue;
            }

            sprintf_s(buf, "animation%d.pattern%d,%s,%d,%d,%d,%d\r\n",
                      id, m++, method.c_str(), surf, wait, px, py);
            body += buf;
        }
        // 絵の指定が 1 つも無ければ、動かしようがないので何も書かない
        if (m == 0) continue;

        // このうごきが動いている間だけ有効な当たり判定。
        // 四角は collision、それ以外の形は collisionex で書きます。
        const JValue& cols = a["collisions"];
        int c = 0, cx = 0;
        for (size_t k = 0; k < cols.size(); k++) {
            const JValue& one = cols.at(k);
            if (!one.isObj()) continue;
            std::string name = Trim(one["name"].asStr());
            if (name.empty()) continue;
            for (size_t n = 0; n < name.size(); n++) {         // 1 行に収める
                if (name[n] == ',' || name[n] == '\r' || name[n] == '\n') name[n] = ' ';
            }
            std::string shape = one["shape"].asStr("rect");
            int x1 = one["x1"].asInt(0), y1 = one["y1"].asInt(0);
            int x2 = one["x2"].asInt(0), y2 = one["y2"].asInt(0);
            char cbuf[320];

            if (shape == "ellipse") {
                sprintf_s(cbuf, "animation%d.collisionex%d,%s,ellipse,%d,%d,%d,%d\r\n",
                          id, cx++, name.c_str(), x1, y1, x2, y2);
                body += cbuf;
            } else if (shape == "circle") {
                if (x2 <= 0) continue;                         // 半径 0 は触れない
                sprintf_s(cbuf, "animation%d.collisionex%d,%s,circle,%d,%d,%d\r\n",
                          id, cx++, name.c_str(), x1, y1, x2);
                body += cbuf;
            } else if (shape == "polygon") {
                // "100,100 200,300 50,200" のように書いてもらったものを数だけ取り出す
                std::vector<int> nums;
                std::string src = one["points"].asStr();
                for (size_t n = 0; n < src.size(); ) {
                    if ((src[n] >= '0' && src[n] <= '9') || src[n] == '-') {
                        size_t e = n + 1;
                        while (e < src.size() && src[e] >= '0' && src[e] <= '9') e++;
                        nums.push_back(atoi(src.substr(n, e - n).c_str()));
                        n = e;
                    } else {
                        n++;
                    }
                }
                if (nums.size() < 6 || nums.size() % 2 != 0) continue;   // かどは 3 つ以上
                if (nums.size() > 64) nums.resize(64);
                std::string line = "animation" + NumToStr(id) + ".collisionex" +
                                   NumToStr(cx++) + "," + name + ",polygon";
                for (size_t n = 0; n < nums.size(); n++) line += "," + NumToStr(nums[n]);
                body += line + "\r\n";
            } else {
                sprintf_s(cbuf, "animation%d.collision%d,%d,%d,%d,%d,%s\r\n",
                          id, c++, x1, y1, x2, y2, name.c_str());
                body += cbuf;
            }
        }

        char head2[96];
        sprintf_s(head2, "animation%d.interval,", id);
        out += head2;
        out += interval;
        out += "\r\n";
        out += body;
    }
    return out;
}

// DrawCharacter の配置に合わせた当たり判定。
// なでなで／クリックのイベントは、ここで付けた名前で絞り込みます。
static std::string CollisionLines(int w, int h) {
    // 頭は中心 y=0.33h・半径 0.30w、体はその下から。
    const double headY = h * 0.33, headR = w * 0.30;
    const double eyeY = headY + headR * 0.16;
    const double bodyTop = headY + headR * 0.85;

    struct Area { const char* name; double x1, y1, x2, y2; };
    Area areas[] = {
        { "Head",  w * 0.18, headY - headR * 1.15, w * 0.82, eyeY },
        { "Face",  w * 0.24, eyeY,                 w * 0.76, bodyTop },
        { "Bust",  w * 0.30, bodyTop,              w * 0.70, h * 0.78 },
        { "Skirt", w * 0.10, h * 0.78,             w * 0.90, (double)h },
        { "Hand",  w * 0.18, bodyTop + h * 0.10,   w * 0.30, bodyTop + h * 0.26 },
        { "Hand",  w * 0.70, bodyTop + h * 0.10,   w * 0.82, bodyTop + h * 0.26 },
    };

    char buf[256];
    std::string out;
    for (int i = 0; i < (int)(sizeof(areas) / sizeof(areas[0])); i++) {
        int y1 = (int)areas[i].y1;
        if (y1 < 0) y1 = 0;
        sprintf_s(buf, "collision%d,%d,%d,%d,%d,%s\r\n", i,
                  (int)areas[i].x1, y1, (int)areas[i].x2, (int)areas[i].y2, areas[i].name);
        out += buf;
    }
    return out;
}

std::string RenderSurfacePng(int surfaceId, const std::string& hairHex, const std::string& clothHex) {
    const int SW = 190, SH = 320, KW = 140, KH = 230;
    bool kero = surfaceId >= 10;
    int face = surfaceId - (kero ? 10 : 0);
    if (face < 0 || face > 2) face = 0;
    std::string hair = hairHex.empty() ? (kero ? "#8fd18a" : "#f08cae") : hairHex;
    std::string cloth = clothHex.empty() ? (kero ? "#e8b45c" : "#6e82c8") : clothHex;
    return DrawCharacter(kero ? KW : SW, kero ? KH : SH, hair, cloth, face);
}

// プロジェクトで指定した画像ファイル。無ければ空。
std::string ShellImagePath(const JValue& project, int surfaceId) {
    const JValue& list = project["shell"]["images"];
    for (size_t i = 0; i < list.size(); i++) {
        const JValue& one = list.at(i);
        if (one["id"].asInt(-1) == surfaceId) return one["path"].asStr();
    }
    return std::string();
}

static void AppendShell(std::vector<OutFile>& files, const JValue& project) {
    const JValue& shell = project["shell"];
    const JValue& meta = project["meta"];
    std::string sakuraHair = shell["sakuraColor"].asStr("#f08cae");
    std::string sakuraCloth = shell["sakuraCloth"].asStr("#6e82c8");
    std::string keroHair = shell["keroColor"].asStr("#8fd18a");
    std::string keroCloth = shell["keroCloth"].asStr("#e8b45c");

    const int SW = 190, SH = 320, KW = 140, KH = 230;
    struct Surface { int id; int w, h; const std::string* hair; const std::string* cloth; int face; };
    std::vector<Surface> list;
    const int stdIds[6] = { 0, 1, 2, 10, 11, 12 };
    for (int i = 0; i < 6; i++) {
        Surface s;
        s.id = stdIds[i];
        bool kero = s.id >= 10;
        s.w = kero ? KW : SW;
        s.h = kero ? KH : SH;
        s.hair = kero ? &keroHair : &sakuraHair;
        s.cloth = kero ? &keroCloth : &sakuraCloth;
        s.face = s.id - (kero ? 10 : 0);
        list.push_back(s);
    }
    // 用意した画像だけにある番号（21番など）も書き出す
    const JValue& images = shell["images"];
    for (size_t i = 0; i < images.size(); i++) {
        int id = images.at(i)["id"].asInt(-1);
        if (id < 0 || images.at(i)["path"].asStr().empty()) continue;
        bool known = false;
        for (size_t k = 0; k < list.size(); k++) {
            if (list[k].id == id) { known = true; break; }
        }
        if (known) continue;
        Surface s;
        s.id = id;
        bool kero = id >= 10;
        s.w = kero ? KW : SW;
        s.h = kero ? KH : SH;
        s.hair = kero ? &keroHair : &sakuraHair;
        s.cloth = kero ? &keroCloth : &sakuraCloth;
        s.face = 0;
        list.push_back(s);
    }

    for (size_t i = 0; i < list.size(); i++) {
        OutFile f;
        char name[32];
        sprintf_s(name, "surface%d.png", list[i].id);
        f.name = std::string("shell/master/") + name;
        f.shell = true;

        // 画像を指定していればそれを使う。読めなければ自動生成にもどす。
        std::string path = ShellImagePath(project, list[i].id);
        std::string data;
        int w = 0, h = 0;
        if (!path.empty() && ReadBinaryFile(Utf8ToWide(path), data) && PngSize(data, &w, &h)) {
            f.data = data;
            f.shell = false;   // 自分でえらんだ絵は、既存があっても書きこむ
            list[i].w = w;
            list[i].h = h;
        } else {
            f.data = DrawCharacter(list[i].w, list[i].h, *list[i].hair, *list[i].cloth, list[i].face);
        }
        files.push_back(f);
    }

    OutFile d;
    d.name = "shell/master/descript.txt";
    d.shell = true;
    d.data = Line("charset", "UTF-8") + Line("type", "shell") + Line("name", "master") +
             Line("craftman", meta["craftman"].asStr("nashi")) +
             Line("seriko.use_self_alpha", "1") +
             Line("seriko.alignmenttodesktop", "bottom") +
             Line("sakura.balloon.offsetx", "80") + Line("sakura.balloon.offsety", "40") +
             Line("kero.balloon.offsetx", "20") + Line("kero.balloon.offsety", "30") +
             Line("sakura.defaultx", "80") + Line("kero.defaultx", "-40");
    files.push_back(d);

    OutFile s;
    s.name = "shell/master/surfaces.txt";
    s.shell = true;
    s.data = "charset,UTF-8\r\n\r\ndescript\r\n{\r\nversion,1\r\n}\r\n\r\n";
    for (size_t i = 0; i < list.size(); i++) {
        char head[32];
        sprintf_s(head, "surface%d\r\n{\r\n", list[i].id);
        s.data += head;
        s.data += CollisionLines(list[i].w, list[i].h);   // 画像を使うときはその大きさで
        s.data += AnimationLines(project, list[i].id);    // SERIKO のアニメーション
        s.data += "}\r\n\r\n";
    }
    files.push_back(s);
}

// ------------------------------------------------------- ghost.json の生成

std::string RuntimeProgramJson(const JValue& project) {
    const JValue& meta = project["meta"];
    const JValue& settings = project["settings"];

    JValue root = JValue::makeObj();
    root.set("format", JValue::makeStr("nashi/1"));

    JValue m = JValue::makeObj();
    m.set("name", JValue::makeStr(meta["name"].asStr("なしゴースト")));
    m.set("sakuraName", JValue::makeStr(meta["sakuraName"].asStr("さくら")));
    m.set("keroName", JValue::makeStr(meta["keroName"].asStr("うにゅう")));
    m.set("craftman", JValue::makeStr(meta["craftman"].asStr("")));
    m.set("craftmanUrl", JValue::makeStr(meta["craftmanUrl"].asStr("")));
    m.set("version", JValue::makeStr(meta["version"].asStr("1.0.0")));
    root.set("meta", m);

    JValue s = JValue::makeObj();
    s.set("randomTalkInterval", JValue::makeNum(settings["randomTalkInterval"].asNum(180)));
    s.set("randomTalkEnabled", JValue::makeBool(settings["randomTalkEnabled"].asBool(true)));
    s.set("noRepeatCount", JValue::makeNum(settings["noRepeatCount"].asNum(0)));
    root.set("settings", s);

    JValue vars = JValue::makeArr();
    const JValue& pv = project["variables"];
    for (size_t i = 0; i < pv.size(); i++) {
        const JValue& v = pv.at(i);
        if (v["name"].asStr().empty()) continue;
        JValue one = JValue::makeObj();
        one.set("name", JValue::makeStr(v["name"].asStr()));
        one.set("value", v["value"]);
        vars.arr.push_back(one);
    }
    root.set("variables", vars);

    JValue scripts = JValue::makeArr();
    const JValue& ps = project["scripts"];
    for (size_t i = 0; i < ps.size(); i++) {
        const JValue& src = ps.at(i);
        std::string kind = src["kind"].asStr();
        if (kind != "event" && kind != "talk" && kind != "function") continue;
        JValue one = JValue::makeObj();
        one.set("id", JValue::makeStr(src["id"].asStr()));
        one.set("kind", JValue::makeStr(kind));
        if (kind == "event") {
            one.set("event", JValue::makeStr(src["event"].asStr()));
            // くりかえしの間隔（毎秒でよければ書かない）
            int every = src["everySec"].asInt(1);
            if (every > 1) one.set("everySec", JValue::makeNum(every));
            // イベントの絞り込み。使っていなければ書かない。
            //   マウス系 … 当たった場所・相手
            //   ゴースト間通信 … 相手の名前・言われたことにふくまれる言葉
            // 前に別のイベントを選んでいたときの絞り込みが残っていても、
            // 今のイベントに関係しないものは書き出さない。
            std::string ev = src["event"].asStr();
            bool isMouse = (ev.compare(0, 7, "OnMouse") == 0) || ev == "OnNadeNade";
            bool isComm = (ev == "OnCommunicate");
            std::string area = isMouse ? src["area"].asStr() : std::string();
            int who = isMouse ? src["who"].asInt(-1) : -1;
            std::string from = isComm ? src["from"].asStr() : std::string();
            std::string contains = isComm ? src["contains"].asStr() : std::string();
            if (!area.empty() || who >= 0 || !from.empty() || !contains.empty()) {
                JValue f = JValue::makeObj();
                if (!area.empty()) f.set("area", JValue::makeStr(area));
                if (who >= 0) f.set("who", JValue::makeNum(who));
                if (!from.empty()) f.set("from", JValue::makeStr(from));
                if (!contains.empty()) f.set("contains", JValue::makeStr(contains));
                one.set("filter", f);
            }
        }
        if (kind == "talk" || kind == "function") one.set("name", JValue::makeStr(src["name"].asStr()));
        if (kind == "talk") one.set("weight", JValue::makeNum(src["weight"].asNum(1)));
        if (src["disabled"].asBool(false)) one.set("disabled", JValue::makeBool(true));
        one.set("blocks", src["blocks"].isArr() ? src["blocks"] : JValue::makeArr());
        scripts.arr.push_back(one);
    }
    root.set("scripts", scripts);

    return root.dump(2);
}

// ---------------------------------------------------------- ファイル一覧

static void AppendBalloon(std::vector<OutFile>& files, const JValue& project);  // 下にあります

std::vector<OutFile> BuildGhostFiles(const JValue& project, const std::string& dll,
                                     bool includeShell, std::string* folderOut) {
    const JValue& meta = project["meta"];
    std::string folder = meta["folder"].asStr();
    if (folder.empty()) folder = meta["name"].asStr("nashi-ghost");
    folder = SafeFolderName(folder, "nashi-ghost");
    if (folderOut) *folderOut = folder;

    std::vector<OutFile> files;
    OutFile f;

    bool withBalloon = includeShell && project["shell"]["balloonEnabled"].asBool(false);

    f.shell = false;
    f.name = "install.txt";
    f.data = Line("charset", "UTF-8") + Line("type", "ghost") +
             Line("name", meta["name"].asStr("なしゴースト")) + Line("directory", folder);
    if (withBalloon) {
        // アーカイブの中の balloon/ を、<フォルダ名>-balloon として入れてもらう
        f.data += Line("balloon.directory", folder + "-balloon") +
                  Line("balloon.source.directory", "balloon");
    }
    files.push_back(f);

    f.name = "readme.txt";
    f.data = meta["name"].asStr("なしゴースト");
    f.data += "\r\n\r\n";
    f.data += "作者      : " + meta["craftman"].asStr("(未設定)") + "\r\n";
    f.data += "バージョン: " + meta["version"].asStr("1.0.0") + "\r\n\r\n";
    f.data += meta["description"].asStr("") + "\r\n\r\n";
    f.data += "----\r\n";
    f.data += "このゴーストは nashi 栞 (nashi.dll) で動いています。\r\n";
    f.data += "会話の中身は ghost/master/ghost.json にブロック形式で入っています。\r\n";
    f.data += "なしスタジオで読み込むと編集できます。\r\n";
    files.push_back(f);

    f.name = "ghost/master/descript.txt";
    f.data = Line("charset", "UTF-8") + Line("type", "ghost") +
             Line("name", meta["name"].asStr("なしゴースト")) +
             Line("sakura.name", meta["sakuraName"].asStr("さくら")) +
             Line("kero.name", meta["keroName"].asStr("うにゅう")) +
             Line("craftman", meta["craftman"].asStr("unknown")) +
             Line("craftmanw", meta["craftman"].asStr("unknown")) +
             Line("craftmanurl", meta["craftmanUrl"].asStr("")) +
             Line("shiori", "nashi.dll") +
             // ネットワーク更新のありか。空なら書かない（書くと更新を試して失敗する）
             (meta["homeUrl"].asStr("").empty()
                  ? std::string()
                  : Line("homeurl", meta["homeUrl"].asStr(""))) +
             Line("sakura.seriko.defaultsurface",
                  NumToStr(project["settings"]["defaultSurfaceSakura"].asNum(0))) +
             Line("kero.seriko.defaultsurface",
                  NumToStr(project["settings"]["defaultSurfaceKero"].asNum(10)));
    files.push_back(f);

    f.name = "ghost/master/ghost.json";
    f.data = RuntimeProgramJson(project);
    files.push_back(f);

    if (!dll.empty()) {
        f.name = "ghost/master/nashi.dll";
        f.data = dll;
        files.push_back(f);
    }

    if (includeShell) AppendShell(files, project);
    if (withBalloon) AppendBalloon(files, project);
    return files;
}

// ----------------------------------------------------- バルーン（吹き出し）
//
// ゴーストに同梱するバルーンを作ります。中身は
//   balloon/descript.txt … 文字を書く場所などの決まりごと
//   balloon/balloons0.png … 本体（さくら）側、balloonk0.png … 相方側
//   balloon/arrow0.png / arrow1.png … 続きがあるときの目印（上向き・下向き）
// で、install.txt の balloon.source.directory で「アーカイブの中の場所」を伝えます。
static void AppendBalloon(std::vector<OutFile>& files, const JValue& project) {
    const JValue& shell = project["shell"];
    Rgb bg = RgbFromHex(shell["balloonColor"].asStr("#fffdf5"), Rgb());
    Rgb line = Shade(bg, -0.35);
    Rgb text = Shade(bg, -0.82);

    const int W = 330, H = 210;
    const int TAIL = 16;              // 下のしっぽのぶん
    const int BODY = H - TAIL;

    // 本体側（しっぽは左寄り）と相方側（しっぽは右寄り）
    for (int side = 0; side < 2; side++) {
        Canvas c(W, H);
        c.RoundRect(0, 0, W, (double)BODY, 18, line, 1.0);
        c.RoundRect(2, 2, W - 4, (double)BODY - 4, 16, bg, 1.0);

        double tx = side == 0 ? 46.0 : (double)W - 46.0;
        std::vector<std::pair<double, double> > tail;
        tail.push_back(std::make_pair(tx - 15, (double)BODY - 6));
        tail.push_back(std::make_pair(tx + 15, (double)BODY - 6));
        tail.push_back(std::make_pair(tx + (side == 0 ? -4 : 4), (double)H - 1));
        c.Polygon(tail, line, 1.0);
        std::vector<std::pair<double, double> > inner;
        inner.push_back(std::make_pair(tx - 11, (double)BODY - 8));
        inner.push_back(std::make_pair(tx + 11, (double)BODY - 8));
        inner.push_back(std::make_pair(tx + (side == 0 ? -4 : 4), (double)H - 5));
        c.Polygon(inner, bg, 1.0);

        OutFile f;
        f.shell = true;               // 自分で描いたものがあれば、そちらを残す
        f.name = side == 0 ? "balloon/balloons0.png" : "balloon/balloonk0.png";
        f.data = c.ToPng();
        files.push_back(f);
    }

    // 続きがあるときの目印（上向き・下向きの三角）
    for (int dir = 0; dir < 2; dir++) {
        Canvas c(16, 12);
        std::vector<std::pair<double, double> > t;
        if (dir == 0) {                               // arrow0 = 上向き
            t.push_back(std::make_pair(8.0, 1.0));
            t.push_back(std::make_pair(15.0, 10.0));
            t.push_back(std::make_pair(1.0, 10.0));
        } else {                                      // arrow1 = 下向き
            t.push_back(std::make_pair(8.0, 11.0));
            t.push_back(std::make_pair(15.0, 2.0));
            t.push_back(std::make_pair(1.0, 2.0));
        }
        c.Polygon(t, line, 1.0);
        OutFile f;
        f.shell = true;
        f.name = dir == 0 ? "balloon/arrow0.png" : "balloon/arrow1.png";
        f.data = c.ToPng();
        files.push_back(f);
    }

    char buf[64];
    OutFile d;
    d.shell = false;
    d.name = "balloon/descript.txt";
    d.data = Line("charset", "UTF-8") + Line("type", "balloon") +
             Line("name", project["meta"]["name"].asStr("なしゴースト") + " のバルーン") +
             Line("craftman", project["meta"]["craftman"].asStr("unknown"));
    d.data += "origin.x,18\r\norigin.y,16\r\n";
    d.data += "validrect.left,0\r\nvalidrect.top,0\r\nvalidrect.right,0\r\n";
    sprintf_s(buf, "validrect.bottom,-%d\r\n", TAIL);
    d.data += buf;
    d.data += "wordwrappoint.x,-22\r\n";
    sprintf_s(buf, "font.color.r,%d\r\n", (int)text.r); d.data += buf;
    sprintf_s(buf, "font.color.g,%d\r\n", (int)text.g); d.data += buf;
    sprintf_s(buf, "font.color.b,%d\r\n", (int)text.b); d.data += buf;
    d.data += "arrow0.x,300\r\narrow0.y,8\r\n";
    sprintf_s(buf, "arrow1.x,300\r\narrow1.y,-%d\r\n", TAIL + 14);
    d.data += buf;
    files.push_back(d);
}

// ------------------------------------------------ ネットワーク更新の照合表
//
// updates2.dau は「どのファイルが、どんな中身か」の一覧です。使う人の SSP は
// これを見て、変わったファイルだけ落としてきます。1 行が 1 ファイルで、
//
//     ghost\master\ghost.json <0x01> md5 <0x01> <CRLF>
//
// という形です（区切りは 0x01、MD5 は小文字 16 進）。
// この形は公式には書かれていないので、公開されている読み取り側の実装
// （ninix-kagari の lib/ninix/update.rb）に合わせています。
// うまくいかないときは、フォルダを SSP に投げれば SSP が作り直してくれます。
static bool SkipInDau(const std::wstring& rel) {
    std::wstring low = rel;
    for (size_t i = 0; i < low.size(); i++) {
        if (low[i] >= L'A' && low[i] <= L'Z') low[i] = (wchar_t)(low[i] - L'A' + L'a');
    }
    // 使う人ごとの持ちもの・こちらの作業用ファイルは配らない
    if (low.find(L"nashi_save.json") != std::wstring::npos) return true;
    if (low.find(L"nashi_debug.txt") != std::wstring::npos) return true;
    if (low == L"updates2.dau" || low == L"updates.txt") return true;
    if (low == L"delete.txt") return true;              // サーバ側だけの指示書
    if (low == L"thumbs.db" || low == L"desktop.ini") return true;
    if (low.compare(0, 5, L".git\\") == 0 || low == L".git") return true;
    return false;
}

std::string BuildUpdatesDau(const std::wstring& root) {
    std::vector<std::wstring> rels = ListFilesDeep(root);
    std::string out;
    for (size_t i = 0; i < rels.size(); i++) {
        if (SkipInDau(rels[i])) continue;
        std::string data;
        if (!ReadBinaryFile(PathJoin(root, rels[i]), data)) continue;
        std::string md5 = Md5Hex(data);
        if (md5.empty()) continue;
        out += WideToUtf8(rels[i]);
        out += '\x01';
        out += md5;
        out += '\x01';
        out += "\r\n";
    }
    return out;
}

// -------------------------------------------------------------- 書き出し

ExportResult ExportToDir(const JValue& project, const std::wstring& outDir, const std::string& dll,
                         bool includeShell, bool overwriteShell) {
    ExportResult r;
    std::string folder;
    std::vector<OutFile> files = BuildGhostFiles(project, dll, includeShell, &folder);
    r.folder = folder;
    r.root = PathJoin(outDir, Utf8ToWide(folder));

    if (!EnsureDir(r.root)) {
        r.error = "フォルダを作れませんでした";
        return r;
    }

    for (size_t i = 0; i < files.size(); i++) {
        std::wstring rel = Utf8ToWide(files[i].name);
        for (size_t k = 0; k < rel.size(); k++) {
            if (rel[k] == L'/') rel[k] = L'\\';
        }
        std::wstring dest = PathJoin(r.root, rel);
        if (files[i].shell && PathExists(dest) && !overwriteShell) {
            r.skipped.push_back(files[i].name);
            continue;
        }
        if (!WriteBinaryFile(dest, files[i].data)) {
            r.error = "書き込みに失敗しました: " + files[i].name;
            return r;
        }
        r.written.push_back(files[i].name);
    }

    // 「更新のありか」を決めているゴーストには、照合表も置く。
    // 中身から作るので、自分で差し替えた絵や、あとから足したファイルもそのまま入る。
    if (!project["meta"]["homeUrl"].asStr("").empty()) {
        std::string dau = BuildUpdatesDau(r.root);
        if (!dau.empty() && WriteBinaryFile(PathJoin(r.root, L"updates2.dau"), dau)) {
            r.written.push_back("updates2.dau");
        }
    }

    r.ok = true;
    return r;
}

ExportResult ExportToNar(const JValue& project, const std::wstring& outDir, const std::string& dll,
                         bool includeShell) {
    ExportResult r;
    std::string folder;
    std::vector<OutFile> files = BuildGhostFiles(project, dll, includeShell, &folder);
    r.folder = folder;

    std::vector<ZipEntry> entries;
    for (size_t i = 0; i < files.size(); i++) {
        ZipEntry e;
        e.name = files[i].name;
        e.data = files[i].data;
        entries.push_back(e);
    }
    std::string zip = CreateZip(entries);

    if (!EnsureDir(outDir)) {
        r.error = "フォルダを作れませんでした";
        return r;
    }
    r.root = PathJoin(outDir, Utf8ToWide(folder) + L".nar");
    if (!WriteBinaryFile(r.root, zip)) {
        r.error = ".nar を書き込めませんでした";
        return r;
    }
    r.written.push_back(folder + ".nar");
    r.ok = true;
    return r;
}

} // namespace nashi
