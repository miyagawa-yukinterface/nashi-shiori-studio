# なしスタジオ - 単体 exe をビルドする
#
#   .\build.ps1              64bit の nashi-studio.exe を作る
#   .\build.ps1 -Arch x86    32bit 版
#
[CmdletBinding()]
param(
    [ValidateSet('x86', 'x64')]
    [string]$Arch = 'x64',
    [switch]$Clean,
    # 書き出しの中身を見るコンソール（studio\test\export_host.cpp）も作る
    [switch]$Test,
    # コード署名証明書の拇印。指定すると exe に署名する
    # （スマートアプリコントロール対策。公的CAの証明書でないと効果は薄い）
    [string]$SignThumbprint
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $root
$src = Join-Path $root 'src'
$obj = Join-Path $root ('obj\' + $Arch)
$exeOut = Join-Path $repo 'nashi-studio.exe'

if ($Clean -and (Test-Path $obj)) { Remove-Item -Recurse -Force $obj }
New-Item -ItemType Directory -Force -Path $obj | Out-Null

# ---- MSVC 環境 -------------------------------------------------------------
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
    Write-Host "[studio] MSVC 環境を読み込みます ($Arch)..." -ForegroundColor DarkGray
    Import-VcVars $Arch
}

# ---- 埋め込みリソースを作る ------------------------------------------------
$dll = Join-Path $repo 'shiori\dist\nashi.dll'
$sample = Join-Path $root 'res\sample-project.json'

$versionFile = Join-Path $repo 'VERSION'
$version = if (Test-Path $versionFile) { (Get-Content $versionFile -Raw).Trim() } else { '0.0.0' }

Write-Host "[studio] リソースを準備中... (v$version)" -ForegroundColor Cyan
& (Join-Path $root 'tools\embed.ps1') `
    -Version $version `
    -PublicDir (Join-Path $repo 'ui') `
    -StageDir $obj `
    -DllPath $dll `
    -SamplePath $sample `
    -IconPath (Join-Path $root 'res\nashi.ico')

Push-Location $obj
try {
    # /c65001 … assets.rc を UTF-8 として読ませる（日本語のバージョン情報のため）
    & rc /nologo /c65001 /fo assets.res assets.rc
    if ($LASTEXITCODE -ne 0) { throw 'リソースのコンパイルに失敗しました。' }
} finally {
    Pop-Location
}

# ---- コンパイル ------------------------------------------------------------
$sources = @(
    (Join-Path $src 'main.cpp'),
    (Join-Path $src 'api.cpp'),
    (Join-Path $src 'assets.cpp'),
    (Join-Path $src 'webview.cpp'),
    (Join-Path $src 'webreq.cpp'),
    (Join-Path $src 'sstp.cpp'),
    (Join-Path $src 'exporter.cpp'),
    (Join-Path $src 'image.cpp'),
    (Join-Path $src 'pngread.cpp'),
    (Join-Path $src 'inflate.cpp'),
    (Join-Path $src 'zip.cpp'),
    (Join-Path $src 'deflate.cpp'),
    (Join-Path $src 'fsutil.cpp'),
    (Join-Path $src 'preview.cpp'),
    # ネイティブ版の画面（WebView2 をやめるための作りかけ。--w2k で出ます）
    (Join-Path $src 'w2k\blockdefs.cpp'),
    (Join-Path $src 'w2k\layout.cpp'),
    (Join-Path $src 'w2k\drag.cpp'),
    (Join-Path $src 'w2k\paint.cpp'),
    (Join-Path $src 'w2k\panel.cpp'),
    (Join-Path $src 'w2k\lint.cpp'),
    (Join-Path $src 'w2k\window.cpp'),
    # 「ためす」は栞そのものでブロックを動かします（同じ規則を二重に書かないため）
    (Join-Path $repo 'shiori\src\interp.cpp'),
    (Join-Path $repo 'shiori\src\program.cpp'),
    (Join-Path $repo 'shiori\src\json.cpp'),
    (Join-Path $repo 'shiori\src\util.cpp')
)

$wv2 = Join-Path $root 'third_party\webview2'
if (-not (Test-Path (Join-Path $wv2 'include\WebView2.h'))) {
    throw "WebView2 SDK が見つかりません: $wv2 (studio\third_party\webview2\README.md を参照)"
}

$clArgs = @(
    '/nologo', '/c', '/O2', '/MT', '/EHsc', '/W3', '/GS', '/std:c++17', '/utf-8',
    '/DNDEBUG', '/DWIN32', '/D_WINDOWS', '/DUNICODE', '/D_UNICODE', '/D_CRT_SECURE_NO_WARNINGS',
    "/I$src", "/I$repo\shiori\src", "/I$obj", "/I$wv2\include",
    "/Fo:$obj\"
) + $sources

Write-Host "[studio] コンパイル中 ($Arch)..." -ForegroundColor Cyan
& cl @clArgs
if ($LASTEXITCODE -ne 0) { throw 'コンパイルに失敗しました。' }

# 前のビルドで残った .obj を混ぜないよう、今回のソースから直接 obj 名を作る
$objs = $sources | ForEach-Object { Join-Path $obj ([System.IO.Path]::GetFileNameWithoutExtension($_) + '.obj') }
$linkArgs = @(
    '/nologo', "/OUT:$exeOut", '/SUBSYSTEM:WINDOWS', '/OPT:REF', '/OPT:ICF'
) + $objs + @((Join-Path $obj 'assets.res'), (Join-Path $wv2 "$Arch\WebView2LoaderStatic.lib")) +
    @('kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib', 'shlwapi.lib',
      'comdlg32.lib', 'advapi32.lib', 'ole32.lib', 'oleaut32.lib', 'version.lib', 'ws2_32.lib')

Write-Host '[studio] リンク中...' -ForegroundColor Cyan
& link @linkArgs
if ($LASTEXITCODE -ne 0) { throw 'リンクに失敗しました。' }

# ---- テスト用コンソール ----------------------------------------------------
# 画面を出さずに中身を見るためのもの。
#   export_host  … 書き出されるファイル（surfaces.txt など）
#   preview_host … 「ためす」の結果（一致テストが 32bit の栞と見くらべます）
if ($Test) {
    $testObj = Join-Path $obj 'test'
    New-Item -ItemType Directory -Force -Path $testObj | Out-Null

    Write-Host '[studio] 書き出しテスト用コンソールをビルド中...' -ForegroundColor Cyan
    $testSources = @(
        (Join-Path $root 'test\export_host.cpp'),
        (Join-Path $src 'exporter.cpp'),
        (Join-Path $src 'image.cpp'),
        (Join-Path $src 'zip.cpp'),
        (Join-Path $src 'deflate.cpp'),
        (Join-Path $src 'fsutil.cpp'),
        (Join-Path $repo 'shiori\src\json.cpp'),
        (Join-Path $repo 'shiori\src\util.cpp')
    )
    $testCl = @(
        '/nologo', '/c', '/O2', '/MT', '/EHsc', '/W3', '/std:c++17', '/utf-8',
        '/DNDEBUG', '/DWIN32', '/DUNICODE', '/D_UNICODE', '/D_CRT_SECURE_NO_WARNINGS',
        "/I$src", "/I$repo\shiori\src", "/Fo:$testObj\"
    ) + $testSources
    & cl @testCl
    if ($LASTEXITCODE -ne 0) { throw '書き出しテスト用コンソールのコンパイルに失敗しました。' }
    $testObjs = $testSources | ForEach-Object {
        Join-Path $testObj ([System.IO.Path]::GetFileNameWithoutExtension($_) + '.obj')
    }
    $testExe = Join-Path $root 'test\export_host.exe'
    $testLink = @('/nologo', "/OUT:$testExe", '/SUBSYSTEM:CONSOLE') + $testObjs +
        @('kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib', 'shlwapi.lib',
          'comdlg32.lib', 'advapi32.lib', 'ole32.lib', 'oleaut32.lib')
    & link @testLink
    if ($LASTEXITCODE -ne 0) { throw '書き出しテスト用コンソールのリンクに失敗しました。' }
    Write-Host "[studio] OK -> $testExe" -ForegroundColor Green

    # ネイティブ版の画面（w2k）で使うブロックの表を、ui\js\blocks.js から作りなおす
    $node = Get-Command node -ErrorAction SilentlyContinue
    if ($node) {
        & node (Join-Path $repo 'tools\gen-blockdefs.js') | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'gen-blockdefs.js が失敗しました。' }
    }

    Write-Host '[studio] 配置テスト用コンソールをビルド中...' -ForegroundColor Cyan
    $layObj = Join-Path $obj 'layout'
    New-Item -ItemType Directory -Force -Path $layObj | Out-Null
    $laySources = @(
        (Join-Path $root 'test\layout_host.cpp'),
        (Join-Path $src 'w2k\blockdefs.cpp'),
        (Join-Path $src 'w2k\layout.cpp'),
        (Join-Path $src 'w2k\drag.cpp'),
        (Join-Path $repo 'shiori\src\json.cpp'),
        (Join-Path $repo 'shiori\src\util.cpp')
    )
    $layCl = @(
        '/nologo', '/c', '/O2', '/MT', '/EHsc', '/W3', '/std:c++17', '/utf-8',
        '/DNDEBUG', '/DWIN32', '/DUNICODE', '/D_UNICODE', '/D_CRT_SECURE_NO_WARNINGS',
        "/I$src", "/I$repo\shiori\src", "/Fo:$layObj\"
    ) + $laySources
    & cl @layCl
    if ($LASTEXITCODE -ne 0) { throw '配置テスト用コンソールのコンパイルに失敗しました。' }
    $layObjs = $laySources | ForEach-Object {
        Join-Path $layObj ([System.IO.Path]::GetFileNameWithoutExtension($_) + '.obj')
    }
    $layExe = Join-Path $root 'test\layout_host.exe'
    & link '/nologo' "/OUT:$layExe" '/SUBSYSTEM:CONSOLE' @layObjs `
        'kernel32.lib' 'user32.lib' 'shell32.lib' 'shlwapi.lib' 'advapi32.lib' 'ole32.lib'
    if ($LASTEXITCODE -ne 0) { throw '配置テスト用コンソールのリンクに失敗しました。' }
    Write-Host "[studio] OK -> $layExe" -ForegroundColor Green

    # 描いた絵を PNG にして出すコンソール（窓を出さずに、目でも確かめられるように）
    Write-Host '[studio] 描画テスト用コンソールをビルド中...' -ForegroundColor Cyan
    $renObj = Join-Path $obj 'render'
    New-Item -ItemType Directory -Force -Path $renObj | Out-Null
    $renSources = @(
        (Join-Path $root 'test\render_host.cpp'),
        (Join-Path $src 'w2k\blockdefs.cpp'),
        (Join-Path $src 'w2k\layout.cpp'),
        (Join-Path $src 'w2k\drag.cpp'),
        (Join-Path $src 'w2k\paint.cpp'),
        (Join-Path $src 'w2k\panel.cpp'),
        (Join-Path $src 'w2k\lint.cpp'),
        (Join-Path $src 'w2k\window.cpp'),
        (Join-Path $src 'image.cpp'),
        (Join-Path $src 'pngread.cpp'),
        (Join-Path $src 'inflate.cpp'),
        (Join-Path $src 'deflate.cpp'),
        # 「ためす」と「書き出し」は、本体と同じものを使います
        (Join-Path $src 'preview.cpp'),
        (Join-Path $src 'exporter.cpp'),
        (Join-Path $src 'fsutil.cpp'),
        (Join-Path $src 'zip.cpp'),
        (Join-Path $repo 'shiori\src\interp.cpp'),
        (Join-Path $repo 'shiori\src\program.cpp'),
        (Join-Path $repo 'shiori\src\json.cpp'),
        (Join-Path $repo 'shiori\src\util.cpp')
    )
    $renCl = @(
        '/nologo', '/c', '/O2', '/MT', '/EHsc', '/W3', '/std:c++17', '/utf-8',
        '/DNDEBUG', '/DWIN32', '/DUNICODE', '/D_UNICODE', '/D_CRT_SECURE_NO_WARNINGS',
        "/I$src", "/I$repo\shiori\src", "/Fo:$renObj\"
    ) + $renSources
    & cl @renCl
    if ($LASTEXITCODE -ne 0) { throw '描画テスト用コンソールのコンパイルに失敗しました。' }
    $renObjs = $renSources | ForEach-Object {
        Join-Path $renObj ([System.IO.Path]::GetFileNameWithoutExtension($_) + '.obj')
    }
    $renExe = Join-Path $root 'test\render_host.exe'
    & link '/nologo' "/OUT:$renExe" '/SUBSYSTEM:CONSOLE' @renObjs `
        'kernel32.lib' 'user32.lib' 'gdi32.lib' 'shell32.lib' 'shlwapi.lib' 'advapi32.lib' `
        'ole32.lib' 'comdlg32.lib'
    if ($LASTEXITCODE -ne 0) { throw '描画テスト用コンソールのリンクに失敗しました。' }
    Write-Host "[studio] OK -> $renExe" -ForegroundColor Green

    # PNG の読み書きを、窓を出さずに確かめるコンソール
    Write-Host '[studio] PNG テスト用コンソールをビルド中...' -ForegroundColor Cyan
    $pngObj = Join-Path $obj 'png'
    New-Item -ItemType Directory -Force -Path $pngObj | Out-Null
    $pngSources = @(
        (Join-Path $root 'test\png_host.cpp'),
        (Join-Path $src 'pngread.cpp'),
        (Join-Path $src 'inflate.cpp'),
        (Join-Path $src 'image.cpp'),
        (Join-Path $src 'deflate.cpp')
    )
    $pngCl = @(
        '/nologo', '/c', '/O2', '/MT', '/EHsc', '/W3', '/std:c++17', '/utf-8',
        '/DNDEBUG', '/DWIN32', '/DUNICODE', '/D_UNICODE', '/D_CRT_SECURE_NO_WARNINGS',
        "/I$src", "/I$repo\shiori\src", "/Fo:$pngObj\"
    ) + $pngSources
    & cl @pngCl
    if ($LASTEXITCODE -ne 0) { throw 'PNG テスト用コンソールのコンパイルに失敗しました。' }
    $pngObjs = $pngSources | ForEach-Object {
        Join-Path $pngObj ([System.IO.Path]::GetFileNameWithoutExtension($_) + '.obj')
    }
    $pngExe = Join-Path $root 'test\png_host.exe'
    & link '/nologo' "/OUT:$pngExe" '/SUBSYSTEM:CONSOLE' @pngObjs 'kernel32.lib' 'user32.lib'
    if ($LASTEXITCODE -ne 0) { throw 'PNG テスト用コンソールのリンクに失敗しました。' }
    Write-Host "[studio] OK -> $pngExe" -ForegroundColor Green

    Write-Host '[studio] プレビューテスト用コンソールをビルド中...' -ForegroundColor Cyan
    $prevObj = Join-Path $obj 'preview'
    New-Item -ItemType Directory -Force -Path $prevObj | Out-Null
    $prevSources = @(
        (Join-Path $root 'test\preview_host.cpp'),
        (Join-Path $src 'preview.cpp'),
        (Join-Path $repo 'shiori\src\interp.cpp'),
        (Join-Path $repo 'shiori\src\program.cpp'),
        (Join-Path $repo 'shiori\src\json.cpp'),
        (Join-Path $repo 'shiori\src\util.cpp')
    )
    $prevCl = @(
        '/nologo', '/c', '/O2', '/MT', '/EHsc', '/W3', '/std:c++17', '/utf-8',
        '/DNDEBUG', '/DWIN32', '/DUNICODE', '/D_UNICODE', '/D_CRT_SECURE_NO_WARNINGS',
        "/I$src", "/I$repo\shiori\src", "/Fo:$prevObj\"
    ) + $prevSources
    & cl @prevCl
    if ($LASTEXITCODE -ne 0) { throw 'プレビューテスト用コンソールのコンパイルに失敗しました。' }
    $prevObjs = $prevSources | ForEach-Object {
        Join-Path $prevObj ([System.IO.Path]::GetFileNameWithoutExtension($_) + '.obj')
    }
    $prevExe = Join-Path $root 'test\preview_host.exe'
    $prevLink = @('/nologo', "/OUT:$prevExe", '/SUBSYSTEM:CONSOLE') + $prevObjs +
        @('kernel32.lib', 'user32.lib', 'shell32.lib', 'shlwapi.lib', 'advapi32.lib', 'ole32.lib')
    & link @prevLink
    if ($LASTEXITCODE -ne 0) { throw 'プレビューテスト用コンソールのリンクに失敗しました。' }
    Write-Host "[studio] OK -> $prevExe" -ForegroundColor Green
}

if ($SignThumbprint) {
    $cert = Get-ChildItem "Cert:\CurrentUser\My\$SignThumbprint" -ErrorAction SilentlyContinue
    if (-not $cert) { $cert = Get-ChildItem "Cert:\LocalMachine\My\$SignThumbprint" -ErrorAction SilentlyContinue }
    if (-not $cert) { throw "証明書が見つかりません: $SignThumbprint" }
    $result = Set-AuthenticodeSignature -FilePath $exeOut -Certificate $cert -HashAlgorithm SHA256 `
        -TimestampServer 'http://timestamp.digicert.com'
    Write-Host "[studio] 署名: $($result.Status)" -ForegroundColor DarkGray
}

$size = [math]::Round((Get-Item $exeOut).Length / 1KB, 1)
Write-Host "[studio] OK -> $exeOut ($size KB)" -ForegroundColor Green
