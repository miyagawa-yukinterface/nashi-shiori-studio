#include "assets.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "assets_gen.h"   // ビルド時に tools\embed.ps1 が作る

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

bool FindWebAsset(const std::string& path, std::string& out, std::string& mime) {
    std::string want = path;
    if (want.empty() || want == "/") want = "/index.html";
    for (int i = 0; i < kWebAssetCount; i++) {
        if (want == kWebAssets[i].path) {
            mime = kWebAssets[i].mime;
            return LoadEmbedded(kWebAssets[i].id, out);
        }
    }
    return false;
}

} // namespace nashi
