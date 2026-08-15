// nashi SHIORI - stand-alone test host.
//
//   test_host.exe <moduleDir> [request ...]
//
// A request is written as  ID  or  ID:ref0,ref1,...
// Example:
//   test_host.exe ghost\master OnFirstBoot OnMouseDoubleClick:10,20,0,0,head
//
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

typedef BOOL(__cdecl* LoadFn)(HGLOBAL, long);
typedef BOOL(__cdecl* UnloadFn)();
typedef HGLOBAL(__cdecl* RequestFn)(HGLOBAL, long*);

static HGLOBAL Dup(const std::string& s, long* len) {
    HGLOBAL h = GlobalAlloc(GMEM_FIXED, s.size() ? s.size() : 1);
    if (s.size()) memcpy(h, s.c_str(), s.size());
    if (len) *len = (long)s.size();
    return h;
}

static std::string BuildRequest(const std::string& spec) {
    std::string id = spec, refspec;
    size_t c = spec.find(':');
    if (c != std::string::npos) { id = spec.substr(0, c); refspec = spec.substr(c + 1); }

    std::string req = "GET SHIORI/3.0\r\nCharset: UTF-8\r\nSender: TestHost\r\nSecurityLevel: local\r\n";
    req += "ID: " + id + "\r\n";
    int n = 0;
    size_t pos = 0;
    while (pos <= refspec.size() && !refspec.empty()) {
        size_t e = refspec.find(',', pos);
        std::string v = (e == std::string::npos) ? refspec.substr(pos) : refspec.substr(pos, e - pos);
        char head[32];
        sprintf_s(head, "Reference%d: ", n++);
        req += head;
        req += v + "\r\n";
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    req += "\r\n";
    return req;
}

static std::string WideToCp(const wchar_t* w, UINT cp) {
    int n = WideCharToMultiByte(cp, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 1) return std::string();
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(cp, 0, w, -1, &s[0], n, NULL, NULL);
    return s;
}
static std::string WideToUtf8(const wchar_t* w) { return WideToCp(w, CP_UTF8); }

int wmain(int argc, wchar_t** wargv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) {
        printf("usage: test_host <moduleDir> [ID | ID:ref0,ref1 ...]\n");
        return 1;
    }

    // arguments arrive as UTF-16; the protocol wants UTF-8
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++) args.push_back(WideToUtf8(wargv[i]));

    std::wstring wdir = wargv[1];
    if (!wdir.empty() && wdir[wdir.size() - 1] != L'\\') wdir += L'\\';
    // SSP はモジュールのパスを OS の ANSI コードページで渡してくる
    std::string dir = WideToCp(wdir.c_str(), CP_ACP);

    HMODULE dll = LoadLibraryW((wdir + L"nashi.dll").c_str());
    if (!dll) {
        printf("cannot load %s (error %lu)\n", WideToUtf8(wdir.c_str()).c_str(), GetLastError());
        return 2;
    }

    LoadFn fnLoad = (LoadFn)GetProcAddress(dll, "load");
    UnloadFn fnUnload = (UnloadFn)GetProcAddress(dll, "unload");
    RequestFn fnRequest = (RequestFn)GetProcAddress(dll, "request");
    if (!fnLoad || !fnUnload || !fnRequest) {
        printf("missing exports (load=%p unload=%p request=%p)\n", fnLoad, fnUnload, fnRequest);
        return 3;
    }

    long len = 0;
    HGLOBAL h = Dup(dir, &len);
    if (!fnLoad(h, len)) {
        printf("load() returned FALSE\n");
        return 4;
    }
    printf("== load ok: %s\n", WideToUtf8(wdir.c_str()).c_str());

    bool unloaded = false;
    for (int i = 2; i < argc; i++) {
        std::string spec = args[(size_t)i];
        int repeat = 1;
        // "OnSecondChange*200" repeats the request
        size_t star = spec.find('*');
        if (star != std::string::npos) {
            repeat = atoi(spec.c_str() + star + 1);
            spec = spec.substr(0, star);
            if (repeat < 1) repeat = 1;
        }
        // "!unload" calls unload() early, then keeps the process alive.
        // Lets us check that a slow SAORI finishing *after* the ghost is gone
        // does not touch freed memory (see saori.cpp).
        if (spec == "!unload") {
            fnUnload();
            FreeLibrary(dll);           // SSP と同じで、unload のすぐあとに外す
            unloaded = true;
            printf("---- %s\n", spec.c_str());
            printf("(unloaded)\n");
            continue;
        }

        // "!sleep:300" waits instead of sending a request.
        // Used to give background work (async SAORI) time to finish.
        if (spec.compare(0, 7, "!sleep:") == 0) {
            int ms = atoi(spec.c_str() + 7);
            if (ms < 0) ms = 0;
            if (ms > 10000) ms = 10000;
            Sleep((DWORD)ms);
            printf("---- %s\n", spec.c_str());
            printf("(slept %d ms)\n", ms);
            continue;
        }

        for (int r = 0; r < repeat; r++) {
            std::string req = BuildRequest(spec);
            long rlen = 0;
            HGLOBAL rh = Dup(req, &rlen);
            HGLOBAL resp = fnRequest(rh, &rlen);
            std::string out;
            if (resp && rlen > 0) out.assign((const char*)resp, (size_t)rlen);
            if (resp) GlobalFree(resp);
            bool empty = out.find("204") != std::string::npos;
            if (repeat == 1 || !empty) {
                printf("---- %s%s\n", spec.c_str(), repeat > 1 ? " (repeat)" : "");
                printf("%s\n", out.c_str());
            }
        }
    }

    if (!unloaded) {
        fnUnload();
        FreeLibrary(dll);
    }
    printf("== unload ok\n");
    return 0;
}
