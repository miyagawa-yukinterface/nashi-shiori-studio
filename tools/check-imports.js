/* 栞が「どの Windows から動くか」を、出来あがった DLL から測る道具
 *
 *   node tools\check-imports.js [dll のパス]
 *
 * SSP は Windows 2000 以降で動きます（公式の動作環境）。栞は SSP に読みこまれる
 * DLL なので、**SSP が動く Windows すべてで動く**のが本来の姿です。
 *
 * ところが、これはソースを読んでも分かりません。新しい Windows でしか無い API は、
 * 自分で呼んでいなくても **C ランタイム（CRT）が勝手に持ちこむ**からです。
 * 手元が Windows 11 だと、当然ふつうに動いてしまうので、気づけません。
 *
 * そこで、ビルド結果の PE ファイルの「輸入表」を直に読んで、
 * Windows 2000 に無い API が混ざっていないかを見ます。外部ライブラリは使いません。
 */
'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const C = { red: '\x1b[31m', green: '\x1b[32m', yellow: '\x1b[33m', dim: '\x1b[2m', off: '\x1b[0m' };

// 目標。SSP の動作環境に合わせます。
const TARGET = { major: 5, minor: 0, name: 'Windows 2000' };

// ------------------------------------------------------------------ 早見表
// その API が「最初に入った Windows」。5.0 = Windows 2000。
// ここに無いものは「要確認」として出します（黙って通さないため）。
const SINCE = {
  // --- Windows 2000 (5.0) までにあるもの ---
  '5.0': [
    'CloseHandle', 'CreateFileW', 'CreateThread', 'DeleteCriticalSection',
    'DisableThreadLibraryCalls', 'EnterCriticalSection', 'ExitProcess',
    'FindClose', 'FindFirstFileExW', 'FindNextFileW', 'FlushFileBuffers',
    'FreeEnvironmentStringsW', 'FreeLibrary', 'FreeLibraryAndExitThread',
    'GetACP', 'GetCPInfo', 'GetCommandLineA', 'GetCommandLineW',
    'GetConsoleMode', 'GetConsoleOutputCP', 'GetCurrentProcess',
    'GetCurrentProcessId', 'GetCurrentThreadId', 'GetEnvironmentStringsW',
    'GetFileAttributesExW', 'GetFileAttributesW', 'GetFileSizeEx', 'GetFileType',
    'GetLastError', 'GetLocalTime', 'GetModuleFileNameW', 'GetModuleHandleW',
    'GetOEMCP', 'GetProcAddress', 'GetProcessHeap', 'GetStartupInfoW',
    'GetStdHandle', 'GetStringTypeW', 'GetSystemTimeAsFileTime', 'GetTickCount',
    'GlobalAlloc', 'GlobalFree', 'HeapAlloc', 'HeapFree', 'HeapReAlloc', 'HeapSize',
    'InitializeCriticalSection', 'IsDebuggerPresent', 'IsValidCodePage',
    'LCMapStringW', 'LeaveCriticalSection', 'LoadLibraryExW', 'LoadLibraryW',
    'MultiByteToWideChar', 'QueryPerformanceCounter', 'RaiseException', 'ReadFile',
    'RtlUnwind', 'SetFilePointer', 'SetFilePointerEx', 'SetLastError',
    'SetStdHandle', 'SetUnhandledExceptionFilter', 'Sleep', 'TerminateProcess',
    'UnhandledExceptionFilter', 'VirtualProtect', 'VirtualQuery',
    'WaitForSingleObject', 'WideCharToMultiByte', 'WriteConsoleW', 'WriteFile',
  ],
  // --- Windows XP (5.1) から ---
  '5.1': [
    'GetModuleHandleExW', 'GetModuleHandleExA',
    'InitializeSListHead', 'InterlockedFlushSList',
    'InterlockedPushEntrySList', 'InterlockedPopEntrySList',
    'IsProcessorFeaturePresent',
    'EncodePointer', 'DecodePointer',      // XP SP2 から
    'GetNativeSystemInfo',
  ],
  // --- Windows Vista (6.0) から ---
  '6.0': [
    'FlsAlloc', 'FlsFree', 'FlsGetValue', 'FlsSetValue',
    'InitializeCriticalSectionEx',
    'AcquireSRWLockExclusive', 'ReleaseSRWLockExclusive',
    'AcquireSRWLockShared', 'ReleaseSRWLockShared', 'InitializeSRWLock',
    'SleepConditionVariableSRW', 'SleepConditionVariableCS',
    'WakeConditionVariable', 'WakeAllConditionVariable',
    'InitOnceExecuteOnce', 'InitOnceBeginInitialize', 'InitOnceComplete',
    'GetTickCount64', 'GetLocaleInfoEx', 'GetUserDefaultLocaleName',
    'LCMapStringEx', 'GetStringTypeEx', 'CompareStringEx',
    'CreateThreadpoolTimer', 'SetThreadpoolTimer', 'CloseThreadpoolTimer',
    'GetSystemTimePreciseAsFileTime',
  ],
  // --- Windows 7 (6.1) 以降から ---
  '6.1': ['GetFileInformationByHandleEx', 'SetFileInformationByHandle'],
  '6.2': ['GetSystemTimePreciseAsFileTime2'],
};

const since = new Map();
for (const [ver, names] of Object.entries(SINCE)) {
  const [major, minor] = ver.split('.').map(Number);
  for (const n of names) since.set(n, { ver, major, minor });
}
const verName = { '5.0': 'Windows 2000', '5.1': 'Windows XP', '6.0': 'Windows Vista', '6.1': 'Windows 7', '6.2': 'Windows 8' };

// -------------------------------------------------------------- PE を読む
function readPe(buf) {
  if (buf.readUInt16LE(0) !== 0x5a4d) throw new Error('MZ ではありません');
  const peOff = buf.readUInt32LE(0x3c);
  if (buf.readUInt32LE(peOff) !== 0x00004550) throw new Error('PE ではありません');

  const coff = peOff + 4;
  const machine = buf.readUInt16LE(coff);
  const nSections = buf.readUInt16LE(coff + 2);
  const optSize = buf.readUInt16LE(coff + 16);
  const opt = coff + 20;
  const magic = buf.readUInt16LE(opt);
  const pe32plus = magic === 0x20b;

  const osMajor = buf.readUInt16LE(opt + 0x28);
  const osMinor = buf.readUInt16LE(opt + 0x2a);
  const subMajor = buf.readUInt16LE(opt + 0x30);
  const subMinor = buf.readUInt16LE(opt + 0x32);

  // データディレクトリの 2 番目（0 起点で 1）が輸入表
  const dirBase = opt + (pe32plus ? 0x70 : 0x60);
  const importRva = buf.readUInt32LE(dirBase + 8);

  const sections = [];
  const secBase = opt + optSize;
  for (let i = 0; i < nSections; i++) {
    const s = secBase + i * 40;
    sections.push({
      va: buf.readUInt32LE(s + 12),
      vsize: buf.readUInt32LE(s + 8),
      raw: buf.readUInt32LE(s + 20),
      rawSize: buf.readUInt32LE(s + 16),
    });
  }
  const toOffset = (rva) => {
    for (const s of sections) {
      const size = Math.max(s.vsize, s.rawSize);
      if (rva >= s.va && rva < s.va + size) return rva - s.va + s.raw;
    }
    return -1;
  };
  const cstr = (off) => {
    let e = off;
    while (e < buf.length && buf[e] !== 0) e++;
    return buf.toString('latin1', off, e);
  };

  const imports = [];
  if (importRva) {
    let d = toOffset(importRva);
    for (; d > 0; d += 20) {
      const oft = buf.readUInt32LE(d);
      const nameRva = buf.readUInt32LE(d + 12);
      const ft = buf.readUInt32LE(d + 16);
      if (!oft && !nameRva && !ft) break;
      const dll = cstr(toOffset(nameRva));
      const step = pe32plus ? 8 : 4;
      let t = toOffset(oft || ft);
      const fns = [];
      for (; t > 0; t += step) {
        const lo = buf.readUInt32LE(t);
        const hi = pe32plus ? buf.readUInt32LE(t + 4) : 0;
        if (!lo && !hi) break;
        const isOrdinal = pe32plus ? (hi & 0x80000000) !== 0 : (lo & 0x80000000) !== 0;
        if (isOrdinal) { fns.push('#' + (lo & 0xffff)); continue; }
        fns.push(cstr(toOffset(lo) + 2));       // Hint(2) のうしろが名前
      }
      imports.push({ dll, fns });
    }
  }
  return { machine, pe32plus, osMajor, osMinor, subMajor, subMinor, imports };
}

// ---------------------------------------------------------------------- 本体
const target = process.argv[2] || path.join(root, 'shiori', 'dist', 'nashi.dll');
if (!fs.existsSync(target)) {
  console.error(`${C.red}[輸入] ${target} がありません。先に .\\build.ps1 を実行してください。${C.off}`);
  process.exit(2);
}

let pe;
try {
  pe = readPe(fs.readFileSync(target));
} catch (e) {
  console.error(`${C.red}[輸入] ${target} を読めません: ${e.message}${C.off}`);
  process.exit(2);
}

const rel = path.relative(root, target) || target;
console.log('');
console.log(`  ${rel}  ${C.dim}(${pe.pe32plus ? '64bit' : '32bit'})${C.off}`);

const declared = `${pe.subMajor}.${String(pe.subMinor).padStart(2, '0')}`;
const wantDeclared = pe.subMajor < TARGET.major
  || (pe.subMajor === TARGET.major && pe.subMinor <= TARGET.minor);
console.log(`  ヘッダが名乗る最低 OS : ${declared}`
  + `（${verName[`${pe.subMajor}.${pe.subMinor}`] || '?'}）`
  + (wantDeclared ? ` ${C.green}OK${C.off}` : ` ${C.red}← ${TARGET.name} は ${TARGET.major}.0${C.off}`));

const bad = [];
const unknown = [];
let total = 0;
for (const imp of pe.imports) {
  for (const fn of imp.fns) {
    total++;
    if (fn.startsWith('#')) continue;
    const info = since.get(fn);
    if (!info) { unknown.push(`${imp.dll} : ${fn}`); continue; }
    if (info.major > TARGET.major || (info.major === TARGET.major && info.minor > TARGET.minor)) {
      bad.push({ dll: imp.dll, fn, ver: info.ver });
    }
  }
}

console.log(`  輸入している API      : ${total} 個`
  + `（${pe.imports.map((i) => i.dll).join(' / ')}）`);
console.log('');

if (bad.length) {
  console.log(`${C.red}  ${TARGET.name} に無い API が ${bad.length} 個あります${C.off}`);
  const byVer = {};
  for (const b of bad) (byVer[b.ver] = byVer[b.ver] || []).push(b.fn);
  for (const ver of Object.keys(byVer).sort()) {
    console.log(`    ${verName[ver] || ver} から: ${byVer[ver].sort().join(', ')}`);
  }
  console.log('');
}
if (unknown.length) {
  console.log(`${C.yellow}  要確認（早見表に無い API）${C.off}`);
  for (const u of unknown) console.log(`    ${u}`);
  console.log(`    ${C.dim}tools\\check-imports.js の SINCE に、入った Windows を書き足してください${C.off}`);
  console.log('');
}

if (bad.length || unknown.length || !wantDeclared) {
  console.log(`${C.red}[輸入] ${TARGET.name} では動きません。${C.off}`);
  process.exit(1);
}
console.log(`${C.green}[輸入] ${TARGET.name} 以降で動きます。${C.off}`);
