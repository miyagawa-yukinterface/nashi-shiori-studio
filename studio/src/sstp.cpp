#include "sstp.h"
#include "fsutil.h"
#include "util.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>

#pragma comment(lib, "ws2_32.lib")

namespace nashi {

static const int kSstpPort = 9801;
static const int kConnectTimeoutMs = 400;
static const int kReadTimeoutMs = 3000;

namespace {

struct WinsockScope {
    bool ok = false;
    WinsockScope() {
        WSADATA wsa;
        ok = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }
    ~WinsockScope() { if (ok) WSACleanup(); }
};

// つながらないときに固まらないよう、短いタイムアウトで接続する
bool ConnectLocal(SOCKET& out, int timeoutMs) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((u_short)kSstpPort);

    connect(s, (sockaddr*)&addr, sizeof(addr));

    fd_set writable, failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(s, &writable);
    FD_SET(s, &failed);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int n = select(0, NULL, &writable, &failed, &tv);
    if (n <= 0 || !FD_ISSET(s, &writable)) {
        closesocket(s);
        return false;
    }

    nonBlocking = 0;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    DWORD timeout = kReadTimeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    out = s;
    return true;
}

SstpResult Exchange(const std::string& request) {
    SstpResult r;
    WinsockScope winsock;
    if (!winsock.ok) {
        r.error = "ネットワーク機能を初期化できませんでした";
        return r;
    }

    SOCKET s = INVALID_SOCKET;
    if (!ConnectLocal(s, kConnectTimeoutMs)) {
        r.error = "SSP につながりません（SSP が起動しているか、SSTP を受け付ける設定かを確認してください）";
        return r;
    }

    size_t sent = 0;
    while (sent < request.size()) {
        int n = send(s, request.c_str() + sent, (int)(request.size() - sent), 0);
        if (n <= 0) {
            closesocket(s);
            r.error = "SSP へ送信できませんでした";
            return r;
        }
        sent += (size_t)n;
    }

    std::string response;
    char buf[4096];
    for (;;) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        response.append(buf, (size_t)n);
        if (response.size() > (1u << 20)) break;
    }
    closesocket(s);

    if (response.empty()) {
        r.error = "SSP から応答がありませんでした";
        return r;
    }

    size_t lineEnd = response.find("\r\n");
    std::string first = response.substr(0, lineEnd == std::string::npos ? response.size() : lineEnd);
    size_t sp = first.find(' ');
    if (sp != std::string::npos) r.status = atoi(first.c_str() + sp + 1);

    size_t bodyStart = response.find("\r\n\r\n");
    if (bodyStart != std::string::npos) r.body = Trim(response.substr(bodyStart + 4));

    r.ok = (r.status >= 200 && r.status < 300);
    if (!r.ok) r.error = "SSP が受け付けませんでした: " + first;
    return r;
}

std::string HeaderLine(const char* key, const std::string& value) {
    std::string v = value;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\r' || v[i] == '\n') v[i] = ' ';
    }
    return std::string(key) + ": " + v + "\r\n";
}

} // namespace

bool SstpAvailable() {
    WinsockScope winsock;
    if (!winsock.ok) return false;
    SOCKET s = INVALID_SOCKET;
    if (!ConnectLocal(s, kConnectTimeoutMs)) return false;
    closesocket(s);
    return true;
}

SstpResult SstpSend(const std::string& scriptUtf8, const std::string& sender) {
    std::string req = "SEND SSTP/1.4\r\n";
    req += HeaderLine("Charset", "UTF-8");
    req += HeaderLine("Sender", sender.empty() ? "なしスタジオ" : sender);
    req += HeaderLine("Script", scriptUtf8);
    req += HeaderLine("Option", "nodescript");
    req += "\r\n";
    return Exchange(req);
}

SstpResult SstpExecute(const std::string& command, const std::string& sender) {
    std::string req = "EXECUTE SSTP/1.4\r\n";
    req += HeaderLine("Charset", "UTF-8");
    req += HeaderLine("Sender", sender.empty() ? "なしスタジオ" : sender);
    req += HeaderLine("Command", command);
    req += "\r\n";
    return Exchange(req);
}

// 他のゴーストのふりをして話しかける。受け取ったゴーストには
// OnCommunicate（Reference0 = Sender、Reference1 = Sentence）として届く。
SstpResult SstpCommunicate(const std::string& sentence, const std::string& sender) {
    std::string req = "COMMUNICATE SSTP/1.1\r\n";
    req += HeaderLine("Charset", "UTF-8");
    req += HeaderLine("Sender", sender.empty() ? "なしスタジオ" : sender);
    req += HeaderLine("Sentence", sentence);
    req += "\r\n";
    return Exchange(req);
}

SstpResult SstpNotify(const std::string& eventName, const std::vector<std::string>& refs,
                      const std::string& sender) {
    std::string req = "NOTIFY SSTP/1.1\r\n";
    req += HeaderLine("Charset", "UTF-8");
    req += HeaderLine("Sender", sender.empty() ? "なしスタジオ" : sender);
    req += HeaderLine("Event", eventName);
    for (size_t i = 0; i < refs.size() && i < 32; i++) {
        char key[24];
        sprintf_s(key, "Reference%u", (unsigned)i);
        req += HeaderLine(key, refs[i]);
    }
    req += "\r\n";
    return Exchange(req);
}

// ------------------------------------------------------------- SSP の場所

/**
 * その番号のアプリの、exe の道すじ。
 *
 * QueryFullProcessImageNameW（Vista から）が使えればそれを、
 * 無ければ GetModuleFileNameExW（Windows 2000 の psapi.dll）を使います。
 * どちらも**その場で探して**呼びます。輸入表に載せると、
 * 古い Windows では起動そのものができなくなるためです。
 */
static std::wstring ExePathOf(DWORD pid) {
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc) {
        // Vista からは、もっと弱い権限でも聞けます
        proc = OpenProcess(0x1000 /* PROCESS_QUERY_LIMITED_INFORMATION */, FALSE, pid);
        if (!proc) return std::wstring();
    }

    wchar_t path[MAX_PATH * 2];
    path[0] = 0;
    DWORD size = MAX_PATH * 2;

    typedef BOOL (WINAPI *QueryFn)(HANDLE, DWORD, LPWSTR, PDWORD);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    QueryFn query = k32 ? (QueryFn)GetProcAddress(k32, "QueryFullProcessImageNameW") : NULL;
    if (query && query(proc, 0, path, &size)) {
        CloseHandle(proc);
        return std::wstring(path, size);
    }

    typedef DWORD (WINAPI *ModFn)(HANDLE, HMODULE, LPWSTR, DWORD);
    HMODULE psapi = LoadLibraryW(L"psapi.dll");
    DWORD n = 0;
    if (psapi) {
        ModFn mod = (ModFn)GetProcAddress(psapi, "GetModuleFileNameExW");
        if (mod) n = mod(proc, NULL, path, MAX_PATH * 2);
        FreeLibrary(psapi);
    }
    CloseHandle(proc);
    return n ? std::wstring(path, n) : std::wstring();
}

static std::wstring FindRunningSsp() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return std::wstring();

    std::wstring found;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"ssp.exe") != 0) continue;
            found = ExePathOf(entry.th32ProcessID);
            if (!found.empty()) break;
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return found;
}

SspInfo DetectSsp(const std::wstring& hint) {
    SspInfo info;

    std::wstring exe = FindRunningSsp();
    info.running = !exe.empty();

    if (exe.empty() && !hint.empty()) {
        std::wstring h = ExpandVars(hint);
        if (IsDirectory(h)) {
            if (PathExists(PathJoin(h, L"ssp.exe"))) exe = PathJoin(h, L"ssp.exe");
        } else if (PathExists(h)) {
            exe = h;
        }
    }

    if (exe.empty()) {
        const wchar_t* guesses[] = {
            L"C:\\ssp\\ssp.exe",
            L"%USERPROFILE%\\ssp\\ssp.exe",
            L"%USERPROFILE%\\Desktop\\ssp\\ssp.exe",
            L"%USERPROFILE%\\Documents\\ssp\\ssp.exe",
            L"%LOCALAPPDATA%\\ssp\\ssp.exe",
            L"%ProgramFiles%\\ssp\\ssp.exe",
        };
        for (int i = 0; i < 6; i++) {
            std::wstring p = ExpandVars(guesses[i]);
            if (PathExists(p)) { exe = p; break; }
        }
    }

    if (!exe.empty()) {
        info.exePath = exe;
        std::wstring dir = ParentDir(exe);
        std::wstring ghost = PathJoin(dir, L"ghost");
        if (IsDirectory(ghost)) info.ghostDir = ghost;
        else info.ghostDir = ghost;   // まだ無ければ作る前提で返す
    }

    info.sstp = SstpAvailable();
    if (info.sstp) {
        SstpResult name = SstpExecute("GetName", "なしスタジオ");
        if (name.ok && !name.body.empty()) info.ghostName = name.body;
    }
    return info;
}

} // namespace nashi
