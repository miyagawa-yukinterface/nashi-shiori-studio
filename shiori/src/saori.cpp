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

Saori::Saori() : running_(0) {
    InitializeCriticalSection(&execCs_);
    InitializeCriticalSection(&doneCs_);
}

Saori::~Saori() {
    UnloadAll();
    DeleteCriticalSection(&doneCs_);
    DeleteCriticalSection(&execCs_);
}

SaoriResult Saori::Execute(const std::string& file, const std::vector<std::string>& args) {
    // 待たない呼び出しと同じ DLL をつつくことがあるので、ここで並びを作る
    EnterCriticalSection(&execCs_);
    struct Unlock {                       // 途中で return しても必ず出る
        CRITICAL_SECTION* cs;
        ~Unlock() { LeaveCriticalSection(cs); }
    } unlock = { &execCs_ };

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

// ---------------------------------------------------- 答えを待たない呼び出し

DWORD WINAPI Saori::ThreadMain(void* param) {
    Job* job = (Job*)param;
    Saori* self = job->self;

    SaoriDone d;
    d.file = job->file;
    d.into = job->into;

    SaoriResult r = self->Execute(job->file, job->args);   // 中で順番待ちをする
    if (!r.ok) {
        Log(r.error);
    } else if (job->valueIndex < 0) {
        d.value = r.result;
    } else if ((size_t)job->valueIndex < r.values.size()) {
        d.value = r.values[(size_t)job->valueIndex];
    }

    EnterCriticalSection(&self->doneCs_);
    self->done_.push_back(d);
    self->running_--;
    LeaveCriticalSection(&self->doneCs_);

    delete job;
    return 0;
}

bool Saori::ExecuteAsync(const std::string& file, const std::vector<std::string>& args,
                         const std::string& into, int valueIndex) {
    if (Trim(file).empty()) return false;

    EnterCriticalSection(&doneCs_);
    bool tooMany = (running_ >= kMaxJobs);
    if (!tooMany) running_++;
    // 終わったスレッドの後始末をここでしておく（たまるのを防ぐ）
    for (size_t i = 0; i < threads_.size(); ) {
        if (WaitForSingleObject(threads_[i], 0) == WAIT_OBJECT_0) {
            CloseHandle(threads_[i]);
            threads_.erase(threads_.begin() + (long)i);
        } else {
            i++;
        }
    }
    LeaveCriticalSection(&doneCs_);

    if (tooMany) {
        Log("saori async: 同時に呼びすぎです（" + file + "）");
        return false;
    }

    Job* job = new Job();
    job->self = this;
    job->file = file;
    job->args = args;
    job->into = into;
    job->valueIndex = valueIndex;

    HANDLE th = CreateThread(NULL, 0, &Saori::ThreadMain, job, 0, NULL);
    if (!th) {
        delete job;
        EnterCriticalSection(&doneCs_);
        running_--;
        LeaveCriticalSection(&doneCs_);
        Log("saori async: スレッドを作れませんでした（" + file + "）");
        return false;
    }

    EnterCriticalSection(&doneCs_);
    threads_.push_back(th);
    LeaveCriticalSection(&doneCs_);
    return true;
}

void Saori::TakeDone(std::vector<SaoriDone>& out) {
    EnterCriticalSection(&doneCs_);
    for (size_t i = 0; i < done_.size(); i++) out.push_back(done_[i]);
    done_.clear();
    LeaveCriticalSection(&doneCs_);
}

int Saori::Running() {
    EnterCriticalSection(&doneCs_);
    int n = running_;
    LeaveCriticalSection(&doneCs_);
    return n;
}

bool Saori::WaitAll(DWORD ms) {
    std::vector<HANDLE> hs;
    EnterCriticalSection(&doneCs_);
    hs = threads_;
    threads_.clear();
    LeaveCriticalSection(&doneCs_);

    bool allDone = true;
    DWORD start = GetTickCount();
    for (size_t i = 0; i < hs.size(); i++) {
        DWORD spent = GetTickCount() - start;
        DWORD left = (spent >= ms) ? 0 : (ms - spent);
        if (WaitForSingleObject(hs[i], left) != WAIT_OBJECT_0) allDone = false;
        CloseHandle(hs[i]);
    }
    return allDone;
}

void Saori::UnloadAll() {
    // 走っている呼び出しが終わるのを待つ。終わらないものがあるときは、
    // DLL を解放すると落ちるので、解放せずに置いていく（終了間際なので害は小さい）。
    if (!WaitAll(5000)) {
        Log("saori: 終わらない呼び出しがあるので、DLL を解放せずに終わります");
        EnterCriticalSection(&execCs_);
        mods_.clear();
        LeaveCriticalSection(&execCs_);
        return;
    }

    EnterCriticalSection(&execCs_);
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
    LeaveCriticalSection(&execCs_);
}

} // namespace nashi
