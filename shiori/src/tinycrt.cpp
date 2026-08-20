// nashi SHIORI - 小さなランタイム（Windows 2000 で動かすため）
//
// なぜこれが要るのか
// ------------------
// SSP は Windows 2000 以降で動きます。栞は SSP に読みこまれる DLL なので、
// 本来そこに合わせるべきです。ところが Visual Studio の C ランタイム（CRT）を
// ふつうに静的リンクすると、**自分では一度も呼んでいない**のに
// FlsAlloc / InitializeCriticalSectionEx / SRW ロック / 条件変数 といった
// Windows Vista からの API が輸入表に載ってしまい、そこから下の Windows では
// **DLL の読みこみ自体が失敗**します（手元が Windows 11 だと気づけません）。
//
// どうしているか
// --------------
// CRT を丸ごと外し（/NODEFAULTLIB）、代わりに **Windows に最初から入っている
// msvcrt.dll** につなぎます。strtod や sprintf、floor といった「自分で書くと
// 間違えるもの」は、そこにある本物を使います（shiori/src/msvcrt.def）。
// msvcrt.dll は Windows 2000 から 11 まで、どれにも入っています。
//
// それでも足りないぶん（Visual Studio 独自のもの）だけを、このファイルで用意します。
// 数は多くありません。node tools\check-imports.js で、目標どおりになっているか
// いつでも確かめられます。
//
// 引き換えにしたこと
// ------------------
// * 例外は投げません。投げるはずだった場面（メモリ不足・長さ超過）は、その場で
//   プロセスを終わらせます。栞は 32KB で頭打ちにしてあるので、ふつうは起きません。
// * このファイルは**栞だけ**が使います。なしスタジオ（64bit）は、ふつうの CRT の
//   ままです（同じ .cpp を両方で組んでいますが、これはリンクしません）。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

// msvcrt.dll から借りるもの（宣言だけ。実体は Windows の中にあります）
extern "C" {
    void* __cdecl malloc(size_t);
    void  __cdecl free(void*);
    int   __cdecl _vsnprintf(char*, size_t, const char*, va_list);
}

// --------------------------------------------------------------- おぼえ書き
// リンカが「浮動小数を使っている」印として探す値。中身に意味はありません。
extern "C" int _fltused = 0x9875;

// リンカが PE に載せたがる「読みこみ時の設定表」。ふだんは CRT が持っています。
// 中身は空で構いませんが、大きさだけは正しく書いておきます。
extern "C" IMAGE_LOAD_CONFIG_DIRECTORY32 _load_config_used = {
    sizeof(IMAGE_LOAD_CONFIG_DIRECTORY32),
};

// --------------------------------------------------------------- 置き場
// new / delete。CRT のものは使えないので、msvcrt.dll の malloc に乗せます。
// 足りなくなったら、例外ではなくその場で終わります（下の Die を見てください）。

static void Die(const char* why) {
    // ここに来たら続けられません。SSP を道連れにしないよう、静かに落とします。
    OutputDebugStringA(why);
    ExitProcess(0xE0000001);
}

void* operator new(size_t n) {
    if (n == 0) n = 1;
    void* p = malloc(n);
    if (!p) Die("nashi: メモリが足りません\n");
    return p;
}
void* operator new[](size_t n) { return operator new(n); }
void  operator delete(void* p) noexcept { if (p) free(p); }
void  operator delete[](void* p) noexcept { operator delete(p); }
void  operator delete(void* p, size_t) noexcept { operator delete(p); }
void  operator delete[](void* p, size_t) noexcept { operator delete(p); }

// ------------------------------------------------------------ 文字まわり
// msvcrt.dll には「_s が付く安全なほう」がありません。中身は同じで、
// 入りきらないときに必ず打ち切って終端する、という約束だけ足します。

extern "C" errno_t __cdecl strcpy_s(char* dst, size_t n, const char* src) {
    if (!dst || n == 0) return EINVAL;
    if (!src) { dst[0] = 0; return EINVAL; }
    size_t i = 0;
    for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
    return src[i] ? ERANGE : 0;
}

// sprintf_s（と仲間）は、ヘッダの中でこれを呼びます。
// 本物の書式づくりは msvcrt.dll の _vsnprintf にまかせます。
extern "C" int __cdecl __stdio_common_vsprintf_s(
        unsigned __int64 options, char* buf, size_t count,
        const char* fmt, void* locale, va_list args) {
    (void)options; (void)locale;
    if (!buf || count == 0 || !fmt) return -1;
    int n = _vsnprintf(buf, count, fmt, args);
    if (n < 0 || (size_t)n >= count) { buf[count - 1] = 0; return -1; }
    buf[n] = 0;
    return n;
}

// ------------------------------------------- はじめと終わりに呼ぶもの
//
// ふだんは CRT が、ファイルの外に置いた変数（g_logPath など）の**組み立て**を
// 読みこみのときに、**後片づけ**を終わりのときに、まとめてやってくれます。
// CRT を外したので、そこも自分でやります。やらないと、std::wstring の中身が
// でたらめなまま使われます（リンカが LNK4210 で教えてくれます）。
//
// コンパイラは、組み立てる関数へのポインタを .CRT$XCU という区画に並べます。
// その前後に目じるし（XCA と XCZ）を置いて、あいだを順に呼びます。

#pragma section(".CRT$XCA", read)
#pragma section(".CRT$XCZ", read)
typedef void (__cdecl* InitFn)();
extern "C" {
    __declspec(allocate(".CRT$XCA")) InitFn g_ctorFirst[] = { 0 };
    __declspec(allocate(".CRT$XCZ")) InitFn g_ctorLast[] = { 0 };
}

namespace {
    typedef void (__cdecl* ExitFn)();
    const int kMaxAtExit = 64;
    ExitFn g_atexit[kMaxAtExit];
    int g_atexitCount = 0;
}

extern "C" int __cdecl atexit(ExitFn fn) {
    if (!fn || g_atexitCount >= kMaxAtExit) return -1;
    g_atexit[g_atexitCount++] = fn;
    return 0;
}

// ------------------------------------------------------- スタックの見張り
//
// /GS を付けると、コンパイラは関数の積み場所に「帯（cookie）」を置き、戻るときに
// 壊れていないか見ます。バッファをはみ出して書いた事故を、そこで止めるためです。
// ふだんは CRT が持っている仕掛けなので、外したぶんを自分で用意します。
//
// 帯の値は起動のたびに変えます。決め打ちだと、値を知られた時点で意味がなくなるためです。

extern "C" UINT_PTR __security_cookie = 0xBB40E64EU;

extern "C" void __cdecl __report_gsfailure() {
    Die("nashi: 積み場所が壊れました（バッファのはみ出し）\n");
}

// 帯を確かめるところ。ecx に、その関数が持っていた帯が入っています。
extern "C" __declspec(naked) void __fastcall __security_check_cookie(UINT_PTR) {
    __asm {
        cmp ecx, __security_cookie
        jne bad
        ret
      bad:
        jmp __report_gsfailure
    }
}

// 帯を配るところ。**この関数じしんは見張られてはいけません**
// （帯を書きかえている最中に、自分の帯と食いちがうため）。safebuffers で外します。
extern "C" __declspec(safebuffers) void NashiInitCookie() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    UINT_PTR c = (UINT_PTR)(li.LowPart ^ ft.dwLowDateTime
                            ^ GetCurrentProcessId() ^ GetTickCount());
    if (c == 0 || c == 0xBB40E64EU) c = 0xBB40E64EU ^ 0x5A5A5A5AU;
    __security_cookie = c;
}

namespace nashi {

// dllmain.cpp が、栞を読みこんだ直後に呼びます。
__declspec(safebuffers) void TinyCrtStartup() {
    NashiInitCookie();                    // 先に帯を配る
    for (InitFn* p = g_ctorFirst; p < g_ctorLast; p++) {
        if (*p) (*p)();
    }
}

// dllmain.cpp が、栞を外すときに呼びます（積んだ順の逆にほどきます）。
void TinyCrtShutdown() {
    while (g_atexitCount > 0) {
        ExitFn fn = g_atexit[--g_atexitCount];
        if (fn) fn();
    }
}

} // namespace nashi

// ------------------------------------------------------------ 例外のかわり
// 栞は例外を投げません（投げるはずの場面は、上限で先に止めてあります）。
// それでもコンパイラは「投げたときのため」の呼び先を要求するので、
// ぜんぶ「その場で終わる」に向けておきます。ここに来たら、それはバグです。

// throw のときに呼ばれるもの。ヘッダが宣言している形に合わせるのが面倒なので、
// リンカに「この名前が無かったら、こちらを使え」と言うかたちにしておきます。
extern "C" void __stdcall NashiThrowStub(void*, void*) {
    Die("nashi: 例外が投げられました（起きないはずです）\n");
}
#pragma comment(linker, "/alternatename:__CxxThrowException@8=_NashiThrowStub@8")
extern "C" int __cdecl __CxxFrameHandler3(void*, void*, void*, void*) {
    Die("nashi: 例外の巻きもどしが起きました（起きないはずです）\n");
    return 0;
}
extern "C" void __cdecl __std_terminate() { Die("nashi: terminate\n"); }

// std::exception が持つ「説明の文字」の複製と後始末。投げない以上、何もしません。
extern "C" void __cdecl __std_exception_copy(const void*, void*) {}
extern "C" void __cdecl __std_exception_destroy(void*) {}

// std::string / std::vector が上限を超えたときに呼ぶもの。
namespace std {
    void __cdecl _Xlength_error(const char*) { Die("nashi: 文字が長すぎます\n"); }
    void __cdecl _Xout_of_range(const char*) { Die("nashi: 範囲の外です\n"); }
    void __cdecl _Xbad_alloc() { Die("nashi: メモリが足りません\n"); }
    void __cdecl _Xinvalid_argument(const char*) { Die("nashi: 引数がおかしいです\n"); }
}

// 抽象クラスの関数を呼んでしまったとき（起きません）。
extern "C" int __cdecl _purecall() { Die("nashi: purecall\n"); return 0; }

// type_info の関数表。RTTI は切ってあるので中身は要りませんが、
// 例外の型を書きとめるところから名前だけ参照されるので、空のものを置いておきます。
extern "C" void* NashiTypeInfoVtable[4] = { 0, 0, 0, 0 };
#pragma comment(linker, "/alternatename:??_7type_info@@6B@=_NashiTypeInfoVtable")
