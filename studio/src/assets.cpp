#include "assets.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nashi {

bool LoadEmbedded(int id, std::string& out) {
    HMODULE mod = GetModuleHandleW(NULL);
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!res) return false;
    DWORD size = SizeofResource(mod, res);
    HGLOBAL handle = LoadResource(mod, res);
    if (!handle || size == 0) return false;
    const void* data = LockResource(handle);
    if (!data) return false;
    out.assign((const char*)data, size);
    return true;
}

} // namespace nashi
