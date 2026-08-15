#include "fsutil.h"

#include <commdlg.h>
#include <wincrypt.h>          // MD5（ネットワーク更新の照合表づくり）

#include <cstdio>

namespace nashi {

std::wstring ExePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH * 2);
    return std::wstring(buf, n);
}

std::wstring ExeDir() { return ParentDir(ExePath()); }

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::wstring out = a;
    if (out[out.size() - 1] != L'\\' && out[out.size() - 1] != L'/') out += L'\\';
    size_t start = (b[0] == L'\\' || b[0] == L'/') ? 1 : 0;
    out += b.substr(start);
    return out;
}

std::wstring ParentDir(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? std::wstring() : path.substr(0, p);
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

bool PathExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool IsDirectory(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDir(const std::wstring& path) {
    if (path.empty()) return false;
    if (IsDirectory(path)) return true;
    std::wstring parent = ParentDir(path);
    if (!parent.empty() && parent != path && !IsDirectory(parent)) {
        if (!EnsureDir(parent)) return false;
    }
    if (CreateDirectoryW(path.c_str(), NULL)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ReadBinaryFile(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > (64LL << 20)) { CloseHandle(h); return false; }
    out.assign((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    BOOL ok = out.empty() ? TRUE : ReadFile(h, &out[0], (DWORD)out.size(), &read, NULL);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(read);
    return true;
}

bool WriteBinaryFile(const std::wstring& path, const std::string& data) {
    EnsureDir(ParentDir(path));
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = data.empty() ? TRUE : WriteFile(h, data.c_str(), (DWORD)data.size(), &written, NULL);
    CloseHandle(h);
    return ok != FALSE;
}

bool DeleteFileIfExists(const std::wstring& path) {
    if (!PathExists(path)) return false;
    return DeleteFileW(path.c_str()) != FALSE;
}

std::vector<FileInfo> ListFiles(const std::wstring& dir, const std::wstring& pattern) {
    std::vector<FileInfo> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(PathJoin(dir, pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        FileInfo info;
        info.name = fd.cFileName;
        info.size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        info.modified = fd.ftLastWriteTime;
        out.push_back(info);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

// 下のフォルダもたどって、相対パスを集める（深さは 16 まで）
static void ListDeepInto(const std::wstring& root, const std::wstring& rel,
                         int depth, std::vector<std::wstring>& out) {
    if (depth > 16) return;
    std::wstring dir = rel.empty() ? root : PathJoin(root, rel);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(PathJoin(dir, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring next = rel.empty() ? name : (rel + L"\\" + name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ListDeepInto(root, next, depth + 1, out);
        } else {
            out.push_back(next);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

std::vector<std::wstring> ListFilesDeep(const std::wstring& dir) {
    std::vector<std::wstring> out;
    ListDeepInto(dir, std::wstring(), 0, out);
    return out;
}

std::string Md5Hex(const std::string& data) {
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    std::string out;
    if (!CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        return out;
    }
    if (CryptCreateHash(prov, CALG_MD5, 0, 0, &hash)) {
        if (CryptHashData(hash, (const BYTE*)data.c_str(), (DWORD)data.size(), 0)) {
            BYTE digest[16];
            DWORD len = sizeof(digest);
            if (CryptGetHashParam(hash, HP_HASHVAL, digest, &len, 0) && len == sizeof(digest)) {
                static const char* hex = "0123456789abcdef";
                for (DWORD i = 0; i < len; i++) {
                    out += hex[(digest[i] >> 4) & 0xF];
                    out += hex[digest[i] & 0xF];
                }
            }
        }
        CryptDestroyHash(hash);
    }
    CryptReleaseContext(prov, 0);
    return out;
}

std::string FileTimeToIso(const FILETIME& ft) {
    SYSTEMTIME st;
    FILETIME local;
    FileTimeToLocalFileTime(&ft, &local);
    FileTimeToSystemTime(&local, &st);
    char buf[40];
    sprintf_s(buf, "%04d-%02d-%02dT%02d:%02d:%02d", st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::vector<std::wstring> PickPngFiles() {
    std::vector<std::wstring> out;

    // 複数選べるので、バッファは大きめに取る
    std::vector<wchar_t> buf(64 * 1024, L'\0');
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = L"PNG 画像 (*.png)\0*.png\0すべてのファイル\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrTitle = L"立ち絵に使う PNG をえらぶ";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                OFN_ALLOWMULTISELECT | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return out;

    // 1つだけなら「フルパス\0」、複数なら「フォルダ\0名前1\0名前2\0\0」
    const wchar_t* p = buf.data();
    std::wstring first = p;
    p += first.size() + 1;
    if (*p == L'\0') {
        out.push_back(first);
        return out;
    }
    while (*p) {
        std::wstring name = p;
        out.push_back(PathJoin(first, name));
        p += name.size() + 1;
    }
    return out;
}

std::wstring ExpandVars(const std::wstring& s) {
    if (s.find(L'%') == std::wstring::npos) return s;
    wchar_t buf[1024];
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), buf, 1024);
    if (n == 0 || n > 1024) return s;
    return std::wstring(buf, n - 1);
}

} // namespace nashi
