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

# ---- compile ---------------------------------------------------------------
$sources = @('dllmain.cpp', 'shiori.cpp', 'program.cpp', 'interp.cpp', 'saori.cpp', 'json.cpp', 'util.cpp') |
           ForEach-Object { Join-Path $src $_ }
$dllOut = Join-Path $dist 'nashi.dll'

$clArgs = @(
    '/nologo', '/c', '/O2', '/MT', '/EHsc', '/W3', '/GS', '/std:c++17', '/utf-8',
    '/DNDEBUG', '/D_CRT_SECURE_NO_WARNINGS', '/DWIN32', '/D_WINDOWS',
    "/Fo:$obj\"
) + $sources

Write-Host "[nashi] コンパイル中 ($Arch)..." -ForegroundColor Cyan
& cl @clArgs
if ($LASTEXITCODE -ne 0) { throw 'コンパイルに失敗しました。' }

$objs = Get-ChildItem $obj -Filter '*.obj' | ForEach-Object { $_.FullName }
$linkArgs = @(
    '/nologo', '/DLL', "/OUT:$dllOut", "/DEF:$src\nashi.def", '/OPT:REF', '/OPT:ICF'
) + $objs + @('kernel32.lib', 'user32.lib', 'advapi32.lib')

Write-Host "[nashi] リンク中..." -ForegroundColor Cyan
& link @linkArgs
if ($LASTEXITCODE -ne 0) { throw 'リンクに失敗しました。' }

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
