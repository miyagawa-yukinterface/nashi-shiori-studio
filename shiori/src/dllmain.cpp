// nashi SHIORI - DLL entry points (SHIORI/3.0)
//
//   BOOL    load(HGLOBAL h, long len)   h = module directory (MBCS), freed by us
//   BOOL    unload()
//   HGLOBAL request(HGLOBAL h, long* len)  h = request (freed by us), return = response
//
#include "shiori.h"
#include "util.h"

#include <string>

static nashi::Shiori* g_shiori = NULL;
static HINSTANCE g_instance = NULL;

BOOL APIENTRY DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = hInst;
        DisableThreadLibraryCalls(hInst);
    }
    return TRUE;
}

extern "C" {

__declspec(dllexport) BOOL __cdecl load(HGLOBAL h, long len) {
    std::string path;
    if (h != NULL) {
        if (len > 0) path.assign((const char*)h, (size_t)len);
        GlobalFree(h);
    }
    if (path.empty()) {
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(g_instance, buf, MAX_PATH);
        std::wstring w(buf, n);
        size_t slash = w.find_last_of(L"\\/");
        std::wstring dir = (slash == std::wstring::npos) ? L"" : w.substr(0, slash + 1);
        if (g_shiori) { g_shiori->Unload(); delete g_shiori; }
        g_shiori = new nashi::Shiori();
        return g_shiori->Load(dir) ? TRUE : FALSE;
    }

    std::wstring dir = nashi::AcpToWide(path);
    if (g_shiori) { g_shiori->Unload(); delete g_shiori; }
    g_shiori = new nashi::Shiori();
    return g_shiori->Load(dir) ? TRUE : FALSE;
}

__declspec(dllexport) BOOL __cdecl unload() {
    if (g_shiori) {
        g_shiori->Unload();
        delete g_shiori;
        g_shiori = NULL;
    }
    return TRUE;
}

__declspec(dllexport) HGLOBAL __cdecl request(HGLOBAL h, long* len) {
    std::string raw;
    if (h != NULL) {
        long n = (len && *len > 0) ? *len : 0;
        if (n > 0) raw.assign((const char*)h, (size_t)n);
        GlobalFree(h);
    }

    std::string response;
    if (g_shiori) {
        response = g_shiori->Request(raw);
    } else {
        response = "SHIORI/3.0 500 Internal Server Error\r\nSender: nashi\r\nCharset: UTF-8\r\n\r\n";
    }

    size_t size = response.size();
    HGLOBAL out = GlobalAlloc(GMEM_FIXED, size ? size : 1);
    if (out == NULL) {
        if (len) *len = 0;
        return NULL;
    }
    if (size) memcpy(out, response.c_str(), size);
    if (len) *len = (long)size;
    return out;
}

} // extern "C"
