#include "api.h"
#include "assets.h"
#include "exporter.h"
#include "fsutil.h"
#include "image.h"
#include "json.h"
#include "sstp.h"
#include "util.h"

#include <shellapi.h>
#include <algorithm>

namespace nashi {

static const char* kStudioVersion = "1.0.0";

// ------------------------------------------------------------------ 補助

static void Json(HttpResponse& res, int status, const JValue& v) {
    res.status = status;
    res.contentType = "application/json; charset=utf-8";
    res.body = v.dump(0);
}

static void JsonError(HttpResponse& res, int status, const std::string& message) {
    JValue o = JValue::makeObj();
    o.set("error", JValue::makeStr(message));
    Json(res, status, o);
}

static bool ParseBody(const HttpRequest& req, JValue& out) {
    std::string err;
    if (req.body.empty()) { out = JValue::makeObj(); return true; }
    return JsonParse(ToUtf8(req.body), out, err);
}

// ------------------------------------------------------------------ init

void Api::Init() {
    exeDir_ = ExeDir();
    projectsDir_ = PathJoin(exeDir_, L"projects");
    outputDir_ = PathJoin(exeDir_, L"output");
    configPath_ = PathJoin(exeDir_, L"nashi-studio.json");

    EnsureDir(projectsDir_);

    // 初回起動のときだけ、埋め込んだサンプルを置く
    if (ListFiles(projectsDir_, L"*.json").empty()) {
        std::string sample;
        if (LoadEmbedded(IDR_SAMPLE_PROJECT, sample)) {
            WriteBinaryFile(PathJoin(projectsDir_, L"なしちゃん.json"), sample);
        }
    }
}

std::string Api::DllBytes() const {
    // exe のとなりに nashi.dll があればそちらを優先（栞を作り直したときのため）
    std::string data;
    if (ReadBinaryFile(PathJoin(exeDir_, L"nashi.dll"), data) && !data.empty()) return data;
    if (LoadEmbedded(IDR_NASHI_DLL, data)) return data;
    return std::string();
}

std::wstring Api::DefaultOutDir() const {
    std::string text;
    JValue cfg;
    std::string err;
    if (ReadBinaryFile(configPath_, text) && JsonParse(ToUtf8(text), cfg, err)) {
        std::string last = cfg["lastOutDir"].asStr();
        if (!last.empty()) return Utf8ToWide(last);
    }
    SspInfo ssp = Ssp();
    return ssp.ghostDir.empty() ? outputDir_ : ssp.ghostDir;
}

SspInfo Api::Ssp() const {
    unsigned long now = GetTickCount();
    if (sspCacheTick_ != 0 && now - sspCacheTick_ < 2000) return sspCache_;

    std::string text;
    JValue cfg;
    std::string err;
    std::wstring hint;
    if (ReadBinaryFile(configPath_, text) && JsonParse(ToUtf8(text), cfg, err)) {
        hint = Utf8ToWide(cfg["sspPath"].asStr());
    }
    sspCache_ = DetectSsp(hint);
    sspCacheTick_ = now;
    return sspCache_;
}

static JValue SspToJson(const SspInfo& ssp) {
    JValue o = JValue::makeObj();
    o.set("running", JValue::makeBool(ssp.running));
    o.set("sstp", JValue::makeBool(ssp.sstp));
    o.set("exe", JValue::makeStr(WideToUtf8(ssp.exePath)));
    o.set("ghostDir", JValue::makeStr(WideToUtf8(ssp.ghostDir)));
    o.set("ghost", JValue::makeStr(ssp.ghostName));
    return o;
}

static JValue ReadConfig(const std::wstring& path) {
    std::string text;
    JValue cfg = JValue::makeObj();
    std::string err;
    if (ReadBinaryFile(path, text)) {
        JValue parsed;
        if (JsonParse(ToUtf8(text), parsed, err) && parsed.isObj()) cfg = parsed;
    }
    return cfg;
}

static void WriteConfig(const std::wstring& path, const std::string& key, const std::string& value) {
    JValue cfg = ReadConfig(path);
    cfg.set(key, JValue::makeStr(value));
    WriteBinaryFile(path, cfg.dump(2));
}

JValue Api::LoadConfig() const { return ReadConfig(configPath_); }

void Api::SaveConfig(const std::string& key, const JValue& value) {
    JValue cfg = ReadConfig(configPath_);
    cfg.set(key, value);
    WriteBinaryFile(configPath_, cfg.dump(2));
}

// ------------------------------------------------------------------ 配信

void Api::Handle(const HttpRequest& req, HttpResponse& res) {
    if (req.path.compare(0, 5, "/api/") == 0) {
        HandleApi(req, res);
        return;
    }
    std::string data, mime;
    if (FindWebAsset(req.path, data, mime)) {
        res.status = 200;
        res.contentType = mime;
        res.body = data;
        return;
    }
    res.status = 404;
    res.contentType = "text/plain; charset=utf-8";
    res.body = "not found";
}

void Api::HandleApi(const HttpRequest& req, HttpResponse& res) {
    // 他のサイトから勝手に叩かれないように、独自ヘッダを必須にする
    if (req.method != "GET") {
        std::map<std::string, std::string>::const_iterator it = req.headers.find("x-nashi");
        if (it == req.headers.end() || it->second != "1") {
            JsonError(res, 403, "unexpected origin");
            return;
        }
    }

    // ---- 状態 ----------------------------------------------------------
    if (req.path == "/api/state" && req.method == "GET") {
        JValue o = JValue::makeObj();
        std::string dll = DllBytes();
        o.set("dllFound", JValue::makeBool(!dll.empty()));
        o.set("dll", JValue::makeStr(dll.empty() ? "" : "nashi.dll (exe に同梱)"));
        o.set("version", JValue::makeStr(kStudioVersion));

        JValue list = JValue::makeArr();
        std::vector<FileInfo> files = ListFiles(projectsDir_, L"*.json");
        std::sort(files.begin(), files.end(), [](const FileInfo& a, const FileInfo& b) {
            return CompareFileTime(&a.modified, &b.modified) > 0;
        });
        for (size_t i = 0; i < files.size(); i++) {
            std::wstring stem = files[i].name.substr(0, files[i].name.size() - 5);
            std::string title = WideToUtf8(stem);
            std::string text;
            if (ReadBinaryFile(PathJoin(projectsDir_, files[i].name), text)) {
                JValue p;
                std::string err;
                if (JsonParse(ToUtf8(text), p, err)) {
                    std::string name = p["meta"]["name"].asStr();
                    if (!name.empty()) title = name;
                }
            }
            JValue one = JValue::makeObj();
            one.set("file", JValue::makeStr(WideToUtf8(stem)));
            one.set("title", JValue::makeStr(title));
            one.set("updated", JValue::makeStr(FileTimeToIso(files[i].modified)));
            list.arr.push_back(one);
        }
        o.set("projects", list);
        o.set("defaultOutDir", JValue::makeStr(WideToUtf8(DefaultOutDir())));
        SspInfo ssp = Ssp();
        o.set("ssp", SspToJson(ssp));
        o.set("sspGhostDir", ssp.ghostDir.empty() ? JValue() : JValue::makeStr(WideToUtf8(ssp.ghostDir)));
        JValue cfg = ReadConfig(configPath_);
        o.set("lastProject", cfg["lastProject"]);
        Json(res, 200, o);
        return;
    }

    // ---- 仮シェルのリアルタイム描画 ------------------------------------
    if (req.path == "/api/shell" && req.method == "GET") {
        std::map<std::string, std::string>::const_iterator it;
        int id = 0;
        it = req.query.find("id");
        if (it != req.query.end()) id = atoi(it->second.c_str());
        it = req.query.find("hair");
        std::string hair = it == req.query.end() ? "" : it->second;
        it = req.query.find("cloth");
        std::string cloth = it == req.query.end() ? "" : it->second;
        res.status = 200;
        res.contentType = "image/png";
        res.cacheable = true;  // 色が変われば URL も変わるので、そのままキャッシュしてよい
        res.body = RenderSurfacePng(id, hair, cloth);
        return;
    }

    // ---- 立ち絵の画像 --------------------------------------------------
    // 用意した PNG を選ぶ（Windows のいつものダイアログを出す）
    if (req.path == "/api/shell/pick" && req.method == "POST") {
        std::vector<std::wstring> picked = PickPngFiles();
        JValue o = JValue::makeObj();
        JValue arr = JValue::makeArr();
        for (size_t i = 0; i < picked.size(); i++) {
            std::string data;
            int w = 0, h = 0;
            if (!ReadBinaryFile(picked[i], data) || !PngSize(data, &w, &h)) continue;
            JValue one = JValue::makeObj();
            one.set("path", JValue::makeStr(WideToUtf8(picked[i])));
            one.set("name", JValue::makeStr(WideToUtf8(FileNameOf(picked[i]))));
            one.set("width", JValue::makeNum(w));
            one.set("height", JValue::makeNum(h));
            arr.arr.push_back(one);
        }
        o.set("files", arr);
        Json(res, 200, o);
        return;
    }

    // 選んだ画像を画面に出すため、そのまま返す
    if (req.path == "/api/shell/file" && req.method == "GET") {
        std::map<std::string, std::string>::const_iterator it = req.query.find("path");
        std::wstring path = it == req.query.end() ? std::wstring() : Utf8ToWide(it->second);
        std::string data;
        int w = 0, h = 0;
        if (path.empty() || !ReadBinaryFile(path, data) || !PngSize(data, &w, &h)) {
            JsonError(res, 404, "画像が読めません");
            return;
        }
        res.status = 200;
        res.contentType = "image/png";
        res.body = data;
        return;
    }

    // ---- プロジェクト --------------------------------------------------
    if (req.path == "/api/project" && req.method == "GET") {
        std::map<std::string, std::string>::const_iterator it = req.query.find("name");
        std::string name = it == req.query.end() ? "" : SafeFolderName(it->second, "");
        std::wstring file = PathJoin(projectsDir_, Utf8ToWide(name) + L".json");
        std::string text;
        if (name.empty() || !ReadBinaryFile(file, text)) {
            JsonError(res, 404, "プロジェクトが見つかりません");
            return;
        }
        res.status = 200;
        res.contentType = "application/json; charset=utf-8";
        res.body = ToUtf8(text);
        return;
    }

    if (req.path == "/api/project" && req.method == "POST") {
        JValue body;
        if (!ParseBody(req, body)) { JsonError(res, 400, "JSON が壊れています"); return; }
        std::string name = body["name"].asStr();
        if (name.empty()) name = body["project"]["meta"]["name"].asStr();
        name = SafeFolderName(name, "");
        if (name.empty()) { JsonError(res, 400, "名前がありません"); return; }
        EnsureDir(projectsDir_);
        std::wstring file = PathJoin(projectsDir_, Utf8ToWide(name) + L".json");
        if (!WriteBinaryFile(file, body["project"].dump(2))) {
            JsonError(res, 500, "保存できませんでした");
            return;
        }
        WriteConfig(configPath_, "lastProject", name);
        JValue o = JValue::makeObj();
        o.set("saved", JValue::makeStr(name));
        o.set("path", JValue::makeStr(WideToUtf8(file)));
        Json(res, 200, o);
        return;
    }

    if (req.path == "/api/project/delete" && req.method == "POST") {
        JValue body;
        if (!ParseBody(req, body)) { JsonError(res, 400, "JSON が壊れています"); return; }
        std::string name = SafeFolderName(body["name"].asStr(), "");
        std::wstring file = PathJoin(projectsDir_, Utf8ToWide(name) + L".json");
        if (name.empty() || !DeleteFileIfExists(file)) {
            JsonError(res, 404, "プロジェクトが見つかりません");
            return;
        }
        JValue o = JValue::makeObj();
        o.set("deleted", JValue::makeStr(name));
        Json(res, 200, o);
        return;
    }

    // ---- 書き出し ------------------------------------------------------
    if (req.path == "/api/export" && req.method == "POST") {
        JValue body;
        if (!ParseBody(req, body)) { JsonError(res, 400, "JSON が壊れています"); return; }
        const JValue& project = body["project"];
        if (!project.isObj()) { JsonError(res, 400, "プロジェクトがありません"); return; }

        std::string outUtf8 = body["outDir"].asStr();
        std::wstring outDir = outUtf8.empty() ? outputDir_ : Utf8ToWide(outUtf8);
        std::string dll = DllBytes();
        bool includeShell = body["includeShell"].asBool(true);
        bool overwriteShell = body["overwriteShell"].asBool(false);
        bool nar = body["mode"].asStr() == "nar";

        ExportResult r = nar ? ExportToNar(project, outDir, dll, includeShell)
                             : ExportToDir(project, outDir, dll, includeShell, overwriteShell);
        if (!r.ok) { JsonError(res, 500, r.error.empty() ? "書き出しに失敗しました" : r.error); return; }
        WriteConfig(configPath_, "lastOutDir", WideToUtf8(outDir));

        JValue o = JValue::makeObj();
        o.set("mode", JValue::makeStr(nar ? "nar" : "dir"));
        o.set("folder", JValue::makeStr(r.folder));
        o.set("dll", JValue::makeBool(!dll.empty()));
        o.set("root", JValue::makeStr(WideToUtf8(r.root)));
        o.set("path", JValue::makeStr(WideToUtf8(r.root)));
        JValue written = JValue::makeArr();
        for (size_t i = 0; i < r.written.size(); i++) written.arr.push_back(JValue::makeStr(r.written[i]));
        o.set("written", written);
        JValue skipped = JValue::makeArr();
        for (size_t i = 0; i < r.skipped.size(); i++) skipped.arr.push_back(JValue::makeStr(r.skipped[i]));
        o.set("skipped", skipped);
        Json(res, 200, o);
        return;
    }

    // ---- 読み込み ------------------------------------------------------
    if (req.path == "/api/import" && req.method == "POST") {
        JValue body;
        if (!ParseBody(req, body)) { JsonError(res, 400, "JSON が壊れています"); return; }
        std::string raw = Trim(body["path"].asStr());
        if (raw.size() >= 2 && raw[0] == '"' && raw[raw.size() - 1] == '"') {
            raw = raw.substr(1, raw.size() - 2);
        }
        if (raw.empty()) { JsonError(res, 400, "パスがありません"); return; }
        std::wstring target = Utf8ToWide(raw);
        if (IsDirectory(target)) {
            const wchar_t* rel[] = { L"ghost.json", L"ghost\\master\\ghost.json", L"master\\ghost.json" };
            for (int i = 0; i < 3; i++) {
                std::wstring c = PathJoin(target, rel[i]);
                if (PathExists(c)) { target = c; break; }
            }
        }
        std::string text;
        if (!ReadBinaryFile(target, text)) { JsonError(res, 400, "読み込めませんでした"); return; }
        JValue project;
        std::string err;
        if (!JsonParse(ToUtf8(text), project, err)) {
            JsonError(res, 400, "JSON を読めませんでした: " + err);
            return;
        }
        JValue o = JValue::makeObj();
        o.set("project", project);
        o.set("path", JValue::makeStr(WideToUtf8(target)));
        Json(res, 200, o);
        return;
    }

    // ---- エクスプローラで開く ------------------------------------------
    if (req.path == "/api/reveal" && req.method == "POST") {
        JValue body;
        if (!ParseBody(req, body)) { JsonError(res, 400, "JSON が壊れています"); return; }
        std::wstring path = Utf8ToWide(body["path"].asStr());
        if (path.empty() || !PathExists(path)) { JsonError(res, 404, "見つかりません"); return; }
        if (!IsDirectory(path)) path = ParentDir(path);
        ShellExecuteW(NULL, L"open", L"explorer.exe", (L"\"" + path + L"\"").c_str(), NULL, SW_SHOWNORMAL);
        JValue o = JValue::makeObj();
        o.set("ok", JValue::makeBool(true));
        Json(res, 200, o);
        return;
    }

    // ---- SSP 連携 ------------------------------------------------------
    if (req.path == "/api/ssp" && req.method == "GET") {
        Json(res, 200, SspToJson(Ssp()));
        return;
    }

    if (req.path == "/api/ssp/path" && req.method == "POST") {
        JValue body;
        if (!ParseBody(req, body)) { JsonError(res, 400, "JSON が壊れています"); return; }
        std::string path = Trim(body["path"].asStr());
        if (path.size() >= 2 && path[0] == '"' && path[path.size() - 1] == '"') {
            path = path.substr(1, path.size() - 2);
        }
        WriteConfig(configPath_, "sspPath", path);
        sspCacheTick_ = 0;                       // すぐ探しなおす
        Json(res, 200, SspToJson(Ssp()));
        return;
    }

    if (req.path == "/api/ssp/launch" && req.method == "POST") {
        SspInfo ssp = Ssp();
        if (ssp.exePath.empty()) {
            JsonError(res, 404, "ssp.exe が見つかりません。SSP の場所を指定してください。");
            return;
        }
        if (!ssp.running) {
            ShellExecuteW(NULL, L"open", ssp.exePath.c_str(), NULL,
                          ParentDir(ssp.exePath).c_str(), SW_SHOWNORMAL);
            Sleep(1500);
        }
        sspCacheTick_ = 0;
        Json(res, 200, SspToJson(Ssp()));
        return;
    }

    if (req.path == "/api/ssp/script" && req.method == "POST") {
        JValue body;
        if (!ParseBody(req, body)) { JsonError(res, 400, "JSON が壊れています"); return; }
        std::string script = body["script"].asStr();
        if (script.empty()) { JsonError(res, 400, "スクリプトがありません"); return; }
        SstpResult r = SstpSend(script, "なしスタジオ");
        if (!r.ok) { JsonError(res, 502, r.error); return; }
        JValue o = JValue::makeObj();
        o.set("ok", JValue::makeBool(true));
        o.set("status", JValue::makeNum(r.status));
        Json(res, 200, o);
        return;
    }

    if (req.path == "/api/ssp/notify" && req.method == "POST") {
        JValue body;
        if (!ParseBody(req, body)) { JsonError(res, 400, "JSON が壊れています"); return; }
        std::string eventName = body["event"].asStr();
        if (eventName.empty()) { JsonError(res, 400, "イベント名がありません"); return; }
        std::vector<std::string> refs;
        const JValue& list = body["refs"];
        for (size_t i = 0; i < list.size(); i++) refs.push_back(list.at(i).asStr());
        SstpResult r = SstpNotify(eventName, refs, "なしスタジオ");
        if (!r.ok) { JsonError(res, 502, r.error); return; }
        JValue o = JValue::makeObj();
        o.set("ok", JValue::makeBool(true));
        o.set("status", JValue::makeNum(r.status));
        Json(res, 200, o);
        return;
    }

    if (req.path == "/api/ssp/install" && req.method == "POST") {
        JValue body;
        if (!ParseBody(req, body)) { JsonError(res, 400, "JSON が壊れています"); return; }
        const JValue& project = body["project"];
        if (!project.isObj()) { JsonError(res, 400, "プロジェクトがありません"); return; }

        SspInfo ssp = Ssp();
        if (ssp.ghostDir.empty()) {
            JsonError(res, 404, "SSP が見つかりません。SSP の場所を指定してください。");
            return;
        }

        std::string dll = DllBytes();
        ExportResult r = ExportToDir(project, ssp.ghostDir, dll,
                                     body["includeShell"].asBool(true),
                                     body["overwriteShell"].asBool(false));
        if (!r.ok) { JsonError(res, 500, r.error.empty() ? "書き出しに失敗しました" : r.error); return; }

        std::string name = project["meta"]["name"].asStr();
        std::string action = "none";
        if (ssp.sstp && body["activate"].asBool(true) && !name.empty()) {
            bool already = !ssp.ghostName.empty() &&
                           (ssp.ghostName == name ||
                            ssp.ghostName == project["meta"]["sakuraName"].asStr());
            SstpResult sent = already
                ? SstpSend("\\![reload,shiori]", "なしスタジオ")
                : SstpSend("\\![change,ghost," + name + "]", "なしスタジオ");
            if (sent.ok) action = already ? "reload" : "change";
            else action = "failed";
        }

        JValue o = JValue::makeObj();
        o.set("root", JValue::makeStr(WideToUtf8(r.root)));
        o.set("folder", JValue::makeStr(r.folder));
        o.set("files", JValue::makeNum((double)r.written.size()));
        o.set("skipped", JValue::makeNum((double)r.skipped.size()));
        o.set("action", JValue::makeStr(action));
        o.set("ssp", SspToJson(ssp));
        Json(res, 200, o);
        return;
    }

    JsonError(res, 404, "unknown endpoint");
}

} // namespace nashi
