#include "saori.h"
#include "util.h"

#include <map>
#include <cstdio>
#include <cstdlib>

namespace nashi {

// SAORI/1.0 の DLL が出しているもの（SHIORI とおなじ形）
typedef BOOL(__cdecl* SaoriLoadFn)(HGLOBAL, long);
typedef BOOL(__cdecl* SaoriUnloadFn)();
typedef HGLOBAL(__cdecl* SaoriRequestFn)(HGLOBAL, long*);

namespace {

struct Module {
    HMODULE dll;
    void* request;
    void* unload;
    bool ready;
    Module() : dll(NULL), request(NULL), unload(NULL), ready(false) {}
};

HGLOBAL DupGlobal(const std::string& s, long* len) {
    HGLOBAL h = GlobalAlloc(GMEM_FIXED, s.size() ? s.size() : 1);
    if (!h) { if (len) *len = 0; return NULL; }
    if (s.size()) memcpy(h, s.c_str(), s.size());
    if (len) *len = (long)s.size();
    return h;
}

std::string LowerAscii2(const std::string& s) {
    std::string t = s;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] >= 'A' && t[i] <= 'Z') t[i] = (char)(t[i] - 'A' + 'a');
    }
    return t;
}

// UTF-8 として筋が通っているか（通らなければ Shift_JIS とみなす）
bool LooksUtf8Local(const std::string& s) {
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
bool SafeRelative(const std::string& file) {
    if (file.empty()) return false;
    if (file.find("..") != std::string::npos) return false;
    if (file.find(':') != std::string::npos) return false;      // C:\ など
    if (file[0] == '\\' || file[0] == '/') return false;
    return true;
}

} // namespace

// ------------------------------------------------------------------- 置き場
//
// Saori 本体と、走っているスレッドで分けあうところ。
// refs が 0 になった人が片づけます（＝いちばん最後まで残った人）。
struct Saori::Core {
    CRITICAL_SECTION execCs;             // DLL の呼び出しをひとつずつにする
    CRITICAL_SECTION doneCs;             // 届いた答え・走っている本数・スレッド
    std::wstring dir;                    // ゴーストのフォルダ
    std::map<std::string, Module> mods;  // execCs で守る
    std::vector<SaoriDone> done;         // doneCs で守る
    std::vector<HANDLE> threads;         // doneCs で守る
    int running;
    long refs;
    bool detached;                       // Saori はもういない（答えは捨てる）

    Core() : running(0), refs(1), detached(false) {
        InitializeCriticalSection(&execCs);
        InitializeCriticalSection(&doneCs);
    }
    ~Core() {
        // ここに来るのは「誰も使っていない」ときだけ。
        // 置いていった DLL があっても、解放はしません（終わり際に、返事をしない
        // モジュールをつつくと、そちらで固まったり落ちたりするため）。
        DeleteCriticalSection(&doneCs);
        DeleteCriticalSection(&execCs);
    }
};

namespace {

void CoreAddRef(Saori::Core* c) {
    InterlockedIncrement(&c->refs);
}

void CoreRelease(Saori::Core* c) {
    if (InterlockedDecrement(&c->refs) != 0) return;
    delete c;
}

// 呼び出しに使う DLL を用意する（execCs を持っている人だけが呼ぶこと）
Module* GetModule(Saori::Core* c, const std::string& file, std::string& err) {
    std::string key = LowerAscii2(file);
    std::map<std::string, Module>::iterator it = c->mods.find(key);
    if (it != c->mods.end()) {
        if (!it->second.ready) { err = "読み込めなかった SAORI です: " + file; return NULL; }
        return &it->second;
    }

    c->mods[key] = Module();              // 失敗も覚えて、毎回読みにいかないようにする
    Module* slot = &c->mods[key];

    if (!SafeRelative(file)) {
        err = "SAORI のファイル名が使えません（ゴーストのフォルダの中だけ）: " + file;
        return NULL;
    }

    std::wstring path = c->dir + Utf8ToWide(file);
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
    folder = (cut == std::wstring::npos) ? c->dir : folder.substr(0, cut + 1);
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

// 待たない呼び出し 1 件ぶんの持ちもの。
// Saori 本体は先に消えるかもしれないので、置き場（core）しか持ちません。
struct Job {
    Saori::Core* core;
    HMODULE self;                 // 栞じしん。走っている間は外されないように押さえておく
    std::string file, into;
    std::vector<std::string> args;
    int valueIndex;
};

} // namespace

// 実際の呼び出し。Saori 本体ではなく置き場だけで動くので、
// ゴーストが終わったあとに答えが返ってきても大丈夫です。
static SaoriResult ExecuteOn(Saori::Core* core, const std::string& file,
                             const std::vector<std::string>& args);

// --------------------------------------------------------------- Saori 本体

Saori::Saori() : core_(new Core()) {}

Saori::~Saori() {
    UnloadAll();
    // 走っているスレッドがいても、ここで置き場を壊さない。
    // 「もういない」の印だけ付けて、自分の分の持ち分を返します。
    EnterCriticalSection(&core_->doneCs);
    core_->detached = true;
    LeaveCriticalSection(&core_->doneCs);
    CoreRelease(core_);
    core_ = NULL;
}

void Saori::SetBaseDir(const std::wstring& dir) {
    EnterCriticalSection(&core_->execCs);
    core_->dir = dir;
    LeaveCriticalSection(&core_->execCs);
}

SaoriResult Saori::Execute(const std::string& file, const std::vector<std::string>& args) {
    return ExecuteOn(core_, file, args);
}

static SaoriResult ExecuteOn(Saori::Core* core, const std::string& file,
                             const std::vector<std::string>& args) {
    // 待たない呼び出しと同じ DLL をつつくことがあるので、ここで並びを作る
    EnterCriticalSection(&core->execCs);
    struct Unlock {                       // 途中で return しても必ず出る
        CRITICAL_SECTION* cs;
        ~Unlock() { LeaveCriticalSection(cs); }
    } unlock = { &core->execCs };

    SaoriResult r;
    std::string err;
    Module* m = GetModule(core, file, err);
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
                std::string k = LowerAscii2(Trim(line.substr(0, c)));
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
    if (!LooksUtf8Local(r.result)) r.result = WideToUtf8(MbToWide(r.result, 932));
    for (size_t i = 0; i < r.values.size(); i++) {
        if (!LooksUtf8Local(r.values[i])) r.values[i] = WideToUtf8(MbToWide(r.values[i], 932));
    }

    r.ok = true;
    return r;
}

// ---------------------------------------------------- 答えを待たない呼び出し

static DWORD WINAPI SaoriThreadMain(void* param) {
    Job* job = (Job*)param;
    Saori::Core* core = job->core;

    SaoriDone d;
    d.file = job->file;
    d.into = job->into;

    // ゴーストがもう終わっているなら、呼びにいかない
    bool gone = false;
    EnterCriticalSection(&core->doneCs);
    gone = core->detached;
    LeaveCriticalSection(&core->doneCs);

    if (!gone) {
        SaoriResult r = ExecuteOn(core, job->file, job->args);   // 中で順番待ちをする
        if (!r.ok) {
            Log(r.error);
        } else if (job->valueIndex < 0) {
            d.value = r.result;
        } else if ((size_t)job->valueIndex < r.values.size()) {
            d.value = r.values[(size_t)job->valueIndex];
        }
    }

    EnterCriticalSection(&core->doneCs);
    core->running--;
    if (!core->detached) core->done.push_back(d);   // 誰も受け取らないなら捨てる
    LeaveCriticalSection(&core->doneCs);

    HMODULE self = job->self;
    delete job;
    CoreRelease(core);            // ここが最後なら、置き場もここで片づく

    // 押さえておいた栞じしんを、ここで返してスレッドを終える。
    // SSP は unload のあとすぐ FreeLibrary するので、押さえていないと
    // 「いま動いているコードが消える」ことになります。
    if (self) FreeLibraryAndExitThread(self, 0);   // ここから先は戻ってきません
    return 0;
}

bool Saori::ExecuteAsync(const std::string& file, const std::vector<std::string>& args,
                         const std::string& into, int valueIndex) {
    if (Trim(file).empty()) return false;

    EnterCriticalSection(&core_->doneCs);
    bool tooMany = (core_->running >= kMaxJobs);
    if (!tooMany) core_->running++;
    // 終わったスレッドの後始末をここでしておく（たまるのを防ぐ）
    for (size_t i = 0; i < core_->threads.size(); ) {
        if (WaitForSingleObject(core_->threads[i], 0) == WAIT_OBJECT_0) {
            CloseHandle(core_->threads[i]);
            core_->threads.erase(core_->threads.begin() + (long)i);
        } else {
            i++;
        }
    }
    LeaveCriticalSection(&core_->doneCs);

    if (tooMany) {
        Log("saori async: 同時に呼びすぎです（" + file + "）");
        return false;
    }

    Job* job = new Job();
    job->core = core_;
    job->file = file;
    job->args = args;
    job->into = into;
    job->valueIndex = valueIndex;
    CoreAddRef(core_);            // スレッドのぶんの持ち分

    // 栞じしんを 1 つ押さえる（スレッドが終わるときに返します）。
    // これが無いと、走っている最中に SSP が FreeLibrary してコードが消えます。
    job->self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       (LPCWSTR)&SaoriThreadMain, &job->self);

    HANDLE th = CreateThread(NULL, 0, &SaoriThreadMain, job, 0, NULL);
    if (!th) {
        CoreRelease(core_);
        if (job->self) FreeLibrary(job->self);
        delete job;
        EnterCriticalSection(&core_->doneCs);
        core_->running--;
        LeaveCriticalSection(&core_->doneCs);
        Log("saori async: スレッドを作れませんでした（" + file + "）");
        return false;
    }

    EnterCriticalSection(&core_->doneCs);
    core_->threads.push_back(th);
    LeaveCriticalSection(&core_->doneCs);
    return true;
}

void Saori::TakeDone(std::vector<SaoriDone>& out) {
    EnterCriticalSection(&core_->doneCs);
    for (size_t i = 0; i < core_->done.size(); i++) out.push_back(core_->done[i]);
    core_->done.clear();
    LeaveCriticalSection(&core_->doneCs);
}

int Saori::Running() {
    EnterCriticalSection(&core_->doneCs);
    int n = core_->running;
    LeaveCriticalSection(&core_->doneCs);
    return n;
}

bool Saori::WaitAll(DWORD ms) {
    std::vector<HANDLE> hs;
    EnterCriticalSection(&core_->doneCs);
    hs = core_->threads;
    core_->threads.clear();
    LeaveCriticalSection(&core_->doneCs);

    bool allDone = true;
    DWORD start = GetTickCount();
    for (size_t i = 0; i < hs.size(); i++) {
        DWORD spent = GetTickCount() - start;
        DWORD left = (spent >= ms) ? 0 : (ms - spent);
        if (WaitForSingleObject(hs[i], left) != WAIT_OBJECT_0) allDone = false;
        CloseHandle(hs[i]);       // 走っていても、こちらの持ち手は閉じてよい
    }
    return allDone;
}

void Saori::UnloadAll() {
    // 走っている呼び出しが終わるのを少し待つ。
    WaitAll(5000);

    // まだ走っているものがあるなら、DLL には触らずに置いていきます。
    // ここで execCs を取りにいくと、その呼び出しが返るまで SSP が固まるためです
    // （待つ時間を決めた意味がなくなります）。
    // 置き場はスレッドと分けあっているので、あとから答えを書かれても平気です。
    EnterCriticalSection(&core_->doneCs);
    bool busy = (core_->running > 0);
    LeaveCriticalSection(&core_->doneCs);
    if (busy) {
        Log("saori: 終わらない呼び出しがあるので、DLL は解放せずに置いていきます");
        return;
    }

    EnterCriticalSection(&core_->execCs);
    for (std::map<std::string, Module>::iterator it = core_->mods.begin();
         it != core_->mods.end(); ++it) {
        if (!it->second.ready) continue;
        try {
            ((SaoriUnloadFn)it->second.unload)();
        } catch (...) {
            // 相手の後始末で落ちても、こちらは終了を続ける
        }
        FreeLibrary(it->second.dll);
        it->second.ready = false;
    }
    core_->mods.clear();
    LeaveCriticalSection(&core_->execCs);
}

} // namespace nashi
