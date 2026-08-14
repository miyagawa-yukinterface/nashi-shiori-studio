#include "exporter.h"
#include "image.h"
#include "zip.h"
#include "fsutil.h"
#include "util.h"

#include <cstdio>

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
            // マウス系イベントの絞り込み（当たった場所・相手）。使っていなければ書かない。
            std::string area = src["area"].asStr();
            int who = src["who"].asInt(-1);
            if (!area.empty() || who >= 0) {
                JValue f = JValue::makeObj();
                if (!area.empty()) f.set("area", JValue::makeStr(area));
                if (who >= 0) f.set("who", JValue::makeNum(who));
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

std::vector<OutFile> BuildGhostFiles(const JValue& project, const std::string& dll,
                                     bool includeShell, std::string* folderOut) {
    const JValue& meta = project["meta"];
    std::string folder = meta["folder"].asStr();
    if (folder.empty()) folder = meta["name"].asStr("nashi-ghost");
    folder = SafeFolderName(folder, "nashi-ghost");
    if (folderOut) *folderOut = folder;

    std::vector<OutFile> files;
    OutFile f;

    f.shell = false;
    f.name = "install.txt";
    f.data = Line("charset", "UTF-8") + Line("type", "ghost") +
             Line("name", meta["name"].asStr("なしゴースト")) + Line("directory", folder);
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
    return files;
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
