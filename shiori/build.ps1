# nashi SHIORI - build script
#
#   .\build.ps1              32bit の nashi.dll をビルド (SSP 標準)
#   .\build.ps1 -Arch x64    64bit 版
#   .\build.ps1 -Test        テストホスト (test_host.exe) も一緒にビルド
#
[CmdletBinding()]
param(
    [ValidateSet('x86', 'x64')]
    [string]$Arch = 'x86',
    [switch]$Test,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src  = Join-Path $root 'src'
$dist = Join-Path $root 'dist'
$obj  = Join-Path $root ('obj\' + $Arch)

if ($Clean -and (Test-Path $obj)) { Remove-Item -Recurse -Force $obj }
New-Item -ItemType Directory -Force -Path $dist, $obj | Out-Null

# ---- make sure a matching MSVC environment is active -----------------------
function Import-VcVars([string]$targetArch) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = $null
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $vsPath) {
        $guess = Get-ChildItem "$env:ProgramFiles\Microsoft Visual Studio" -Directory -ErrorAction SilentlyContinue |
                 Sort-Object Name -Descending | Select-Object -First 1
        if ($guess) { $vsPath = (Get-ChildItem $guess.FullName -Directory | Select-Object -First 1).FullName }
    }
    if (-not $vsPath) { throw 'Visual Studio (C++ ビルドツール) が見つかりませんでした。' }

    $bat = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
    if (-not (Test-Path $bat)) { throw "vcvarsall.bat が見つかりません: $bat" }

    $lines = cmd /c "`"$bat`" $targetArch >nul 2>&1 && set"
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path ("Env:" + $matches[1]) -Value $matches[2] -ErrorAction SilentlyContinue
        }
    }
}

$needVcVars = $true
$cl = Get-Command cl -ErrorAction SilentlyContinue
if ($cl -and $env:LIB) {
    $isX64Env = $cl.Source -match '\\Hostx64\\x64\\' -or $env:VSCMD_ARG_TGT_ARCH -eq 'x64'
    if (($Arch -eq 'x64') -eq [bool]$isX64Env) { $needVcVars = $false }
}
if ($needVcVars) {
    Write-Host "[nashi] MSVC 環境を読み込みます ($Arch)..." -ForegroundColor DarkGray
    Import-VcVars $Arch
}

# ---- Windows 2000 でも読みこめるようにする ---------------------------------
# SSP は Windows 2000 以降で動きます。栞は SSP に読みこまれる DLL なので、そこに
# 合わせます。ところが Visual Studio の C ランタイムを静的リンク（/MT）すると、
# 自分では呼んでいない Vista からの API（FlsAlloc など）が輸入表に載ってしまい、
# 古い Windows では **DLL の読みこみ自体が失敗**します。
# そこで CRT を外し（/NODEFAULTLIB）、Windows に最初から入っている msvcrt.dll に
# つなぎます。足りないぶんは src\tinycrt.cpp が持っています。
#   ねらいどおりか  ->  node tools\check-imports.js
$msvcrtLib = Join-Path $obj 'msvcrt.lib'
Write-Host '[nashi] msvcrt.dll の輸入ライブラリを作ります...' -ForegroundColor DarkGray
& lib /nologo "/DEF:$src\msvcrt.def" "/OUT:$msvcrtLib" /MACHINE:X86 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'msvcrt.lib を作れませんでした。' }

# ---- compile ---------------------------------------------------------------
$sources = @('dllmain.cpp', 'shiori.cpp', 'program.cpp', 'interp.cpp', 'saori.cpp',
             'json.cpp', 'util.cpp', 'tinycrt.cpp') |
           ForEach-Object { Join-Path $src $_ }
$dllOut = Join-Path $dist 'nashi.dll'

# /GS- /GR- /Zc:threadSafeInit-  … どれも CRT の助けが要る仕掛けなので切ります
# /D_USE_STD_VECTOR_ALGORITHMS=0 … 新しい STL の SIMD 版も CRT を呼ぶので使いません
$clArgs = @(
    '/nologo', '/c', '/O2', '/EHsc', '/W3', '/GS', '/GR-', '/std:c++17', '/utf-8',
    '/Zc:threadSafeInit-', '/D_USE_STD_VECTOR_ALGORITHMS=0',
    '/DNDEBUG', '/D_CRT_SECURE_NO_WARNINGS', '/DWIN32', '/D_WINDOWS',
    '/D_WIN32_WINNT=0x0500', '/DWINVER=0x0500',
    "/Fo:$obj\"
) + $sources

Write-Host "[nashi] コンパイル中 ($Arch)..." -ForegroundColor Cyan
& cl @clArgs
if ($LASTEXITCODE -ne 0) { throw 'コンパイルに失敗しました。' }

$objs = Get-ChildItem $obj -Filter '*.obj' | ForEach-Object { $_.FullName }
$linkArgs = @(
    '/nologo', '/DLL', "/OUT:$dllOut", "/DEF:$src\nashi.def", '/OPT:REF', '/OPT:ICF',
    '/NODEFAULTLIB', '/ENTRY:DllMain', '/SAFESEH:NO'
) + $objs + @($msvcrtLib, 'kernel32.lib', 'user32.lib', 'advapi32.lib')

Write-Host "[nashi] リンク中..." -ForegroundColor Cyan
& link @linkArgs
if ($LASTEXITCODE -ne 0) { throw 'リンクに失敗しました。' }

# ---- 名乗る最低 OS を Windows 2000 にする ----------------------------------
# link.exe は 6.00（Vista）より下を受けつけないので、出来あがった PE の
# 2 か所（OperatingSystemVersion と SubsystemVersion）を直に 5.00 に書きかえます。
$bytes = [System.IO.File]::ReadAllBytes($dllOut)
$pe = [System.BitConverter]::ToInt32($bytes, 0x3c)
$opt = $pe + 24
foreach ($off in @(0x28, 0x30)) {          # Major/Minor が 2 バイトずつ並んでいます
    $bytes[$opt + $off]     = 5            # Major = 5
    $bytes[$opt + $off + 1] = 0
    $bytes[$opt + $off + 2] = 0            # Minor = 0
    $bytes[$opt + $off + 3] = 0
}
[System.IO.File]::WriteAllBytes($dllOut, $bytes)

$size = [math]::Round((Get-Item $dllOut).Length / 1KB, 1)
Write-Host "[nashi] OK -> $dllOut ($size KB)" -ForegroundColor Green

# この dll は studio\build.ps1 が exe に埋め込みます。

# ---- test host -------------------------------------------------------------
if ($Test) {
    # 中間ファイルは別のフォルダへ。栞のリンクが拾ってしまわないように。
    $testObj = Join-Path $obj 'testbuild'
    New-Item -ItemType Directory -Force -Path $testObj | Out-Null

    $testOut = Join-Path $dist 'test_host.exe'
    Write-Host "[nashi] テストホストをビルド中..." -ForegroundColor Cyan
    & cl /nologo /O2 /MT /EHsc /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS `
        (Join-Path $root 'test\test_host.cpp') `
        "/Fo:$testObj\" "/Fe:$testOut" /link /SUBSYSTEM:CONSOLE
    if ($LASTEXITCODE -ne 0) { throw 'テストホストのビルドに失敗しました。' }
    Copy-Item $dllOut (Join-Path $root 'test\fixture\nashi.dll') -Force
    Write-Host "[nashi] OK -> $testOut" -ForegroundColor Green

    # 待たない呼び出しを確かめるための、おそい SAORI（栞と同じ 32bit）
    $saoriOut = Join-Path $dist 'slow_saori.dll'
    Write-Host "[nashi] テスト用の SAORI をビルド中..." -ForegroundColor Cyan
    & cl /nologo /O2 /MT /EHsc /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS `
        (Join-Path $root 'test\saori\slow_saori.cpp') `
        "/Fo:$testObj\" "/Fe:$saoriOut" /link /DLL /EXPORT:load /EXPORT:unload /EXPORT:request
    if ($LASTEXITCODE -ne 0) { throw 'テスト用 SAORI のビルドに失敗しました。' }
    Write-Host "[nashi] OK -> $saoriOut" -ForegroundColor Green
    Write-Host "[nashi] 動作確認: .\dist\test_host.exe .\test\fixture OnFirstBoot OnBoot OnClose" -ForegroundColor DarkGray
}
