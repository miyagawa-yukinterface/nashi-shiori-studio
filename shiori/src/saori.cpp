#include "saori.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>

namespace nashi {

// SAORI/1.0 の DLL が出しているもの（SHIORI とおなじ形）
typedef BOOL(__cdecl* SaoriLoadFn)(HGLOBAL, long);
typedef BOOL(__cdecl* SaoriUnloadFn)();
typedef HGLOBAL(__cdecl* SaoriRequestFn)(HGLOBAL, long*);

static HGLOBAL DupGlobal(const std::string& s, long* len) {
    HGLOBAL h = GlobalAlloc(GMEM_FIXED, s.size() ? s.size() : 1);
    if (!h) { if (len) *len = 0; return NULL; }
    if (s.size()) memcpy(h, s.c_str(), s.size());
    if (len) *len = (long)s.size();
    return h;
}

static std::string LowerAscii(const std::string& s) {
    std::string t = s;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] >= 'A' && t[i] <= 'Z') t[i] = (char)(t[i] - 'A' + 'a');
    }
    return t;
}

// UTF-8 として筋が通っているか（通らなければ Shift_JIS とみなす）
static bool LooksUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = (unsigned char)s[i];
        int more;
        if (c < 0x80) { i++; continue; }
        else if ((c & 0xE0) == 0xC0) more = 1;
        else if ((c & 0xF0) == 0xE0) more = 2;
        else if ((c & 0xF8) == 0xF0) more = 3;
        else return false;
        if (i + (size_t)more >= s.size()) return false;
        for (int k = 1; k <= more; k++) {
            if (((unsigned char)s[i + (size_t)k] & 0xC0) != 0x80) return false;
        }
        i += (size_t)more + 1;
    }
    return true;
}

// ゴーストのフォルダから外へ出る指定は受け付けない
static bool SafeRelative(const std::string& file) {
    if (file.empty()) return false;
    if (file.find("..") != std::string::npos) return false;
    if (file.find(':') != std::string::npos) return false;      // C:\ など
    if (file[0] == '\\' || file[0] == '/') return false;
    return true;
}

Saori::Module* Saori::Get(const std::string& file, std::string& err) {
    std::string key = LowerAscii(file);
    std::map<std::string, Module>::iterator it = mods_.find(key);
    if (it != mods_.end()) {
        if (!it->second.ready) { err = "読み込めなかった SAORI です: " + file; return NULL; }
        return &it->second;
    }

    Module m;
    mods_[key] = m;                       // 失敗も覚えて、毎回読みにいかないようにする
    Module* slot = &mods_[key];

    if (!SafeRelative(file)) {
        err = "SAORI のファイル名が使えません（ゴーストのフォルダの中だけ）: " + file;
        return NULL;
    }

    std::wstring path = dir_ + Utf8ToWide(file);
    HMODULE dll = LoadLibraryW(path.c_str());
    if (!dll) {
        char buf[32];
        sprintf_s(buf, "%lu", GetLastError());
        err = "SAORI を読み込めません（" + file + " / エラー " + buf + "）。"
              "栞と同じ 32bit の DLL かどうか確かめてください。";
        return NULL;
    }

    SaoriLoadFn fnLoad = (SaoriLoadFn)GetProcAddress(dll, "load");
    SaoriRequestFn fnReq = (SaoriRequestFn)GetProcAddress(dll, "request");
    SaoriUnloadFn fnUnload = (SaoriUnloadFn)GetProcAddress(dll, "unload");
    if (!fnLoad || !fnReq || !fnUnload) {
        FreeLibrary(dll);
        err = "SAORI の形をしていません（load / request / unload がありません）: " + file;
        return NULL;
    }

    // load には自分の置き場所を ANSI で渡す（SHIORI と同じ約束）
    std::wstring folder = path;
    size_t cut = folder.find_last_of(L"\\/");
    folder = (cut == std::wstring::npos) ? dir_ : folder.substr(0, cut + 1);
    long len = 0;
    HGLOBAL arg = DupGlobal(WideToMb(folder, CP_ACP), &len);
    if (!fnLoad(arg, len)) {
        FreeLibrary(dll);
        err = "SAORI の load が失敗しました: " + file;
        return NULL;
    }

    slot->dll = dll;
    slot->request = (void*)fnReq;
    slot->unload = (void*)fnUnload;
    slot->ready = true;
    Log("saori loaded: " + file);
    return slot;
}

SaoriResult Saori::Execute(const std::string& file, const std::vector<std::string>& args) {
    SaoriResult r;
    std::string err;
    Module* m = Get(file, err);
    if (!m) { r.error = err; return r; }

    std::string req = "EXECUTE SAORI/1.0\r\n";
    req += "Charset: UTF-8\r\n";
    req += "Sender: nashi\r\n";
    req += "SecurityLevel: local\r\n";
    for (size_t i = 0; i < args.size() && i < 32; i++) {
        char key[24];
        sprintf_s(key, "Argument%u: ", (unsigned)i);
        std::string v = args[i];
        for (size_t k = 0; k < v.size(); k++) {          // 見出しは 1 行に収める
            if (v[k] == '\r' || v[k] == '\n') v[k] = ' ';
        }
        req += key;
        req += v + "\r\n";
    }
    req += "\r\n";

    long len = 0;
    HGLOBAL in = DupGlobal(req, &len);
    HGLOBAL out = NULL;
    try {
        out = ((SaoriRequestFn)m->request)(in, &len);
    } catch (...) {
        r.error = "SAORI の中で例外が起きました: " + file;
        return r;
    }
    if (!out || len <= 0) { r.error = "SAORI が何も返しませんでした: " + file; return r; }

    std::string resp((const char*)out, (size_t)len);
    GlobalFree(out);

    // 1 行目 "SAORI/1.0 200 OK"、あとは "見出し: 中身"
    size_t pos = 0;
    bool first = true;
    while (pos <= resp.size()) {
        size_t e = resp.find('\n', pos);
        std::string line = (e == std::string::npos) ? resp.substr(pos) : resp.substr(pos, e - pos);
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (first) {
            first = false;
            size_t sp = line.find(' ');
            if (sp != std::string::npos) r.status = atoi(line.c_str() + sp + 1);
        } else if (!line.empty()) {
            size_t c = line.find(':');
            if (c != std::string::npos) {
                std::string k = LowerAscii(Trim(line.substr(0, c)));
                std::string v = Trim(line.substr(c + 1));
                if (k == "result") {
                    r.result = v;
                } else if (StartsWith(k, "value")) {
                    int idx = atoi(k.c_str() + 5);
                    if (idx >= 0 && idx < 64) {
                        if ((int)r.values.size() <= idx) r.values.resize((size_t)idx + 1);
                        r.values[(size_t)idx] = v;
                    }
                }
            }
        }
        if (e == std::string::npos) break;
        pos = e + 1;
    }

    // 中身が Shift_JIS で返ってくる SAORI もある。UTF-8 として読めなければ読み替える。
    if (!LooksUtf8(r.result)) r.result = WideToUtf8(MbToWide(r.result, 932));
    for (size_t i = 0; i < r.values.size(); i++) {
        if (!LooksUtf8(r.values[i])) r.values[i] = WideToUtf8(MbToWide(r.values[i], 932));
    }

    r.ok = true;
    return r;
}

void Saori::UnloadAll() {
    for (std::map<std::string, Module>::iterator it = mods_.begin(); it != mods_.end(); ++it) {
        if (!it->second.ready) continue;
        try {
            ((SaoriUnloadFn)it->second.unload)();
        } catch (...) {
            // 相手の後始末で落ちても、こちらは終了を続ける
        }
        FreeLibrary(it->second.dll);
        it->second.ready = false;
    }
    mods_.clear();
}

} // namespace nashi
