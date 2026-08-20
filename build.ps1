# なし栞 + なしスタジオ をまとめてビルドする
#
#   .\build.ps1            栞(32bit DLL) → スタジオ(64bit EXE) の順にビルド
#   .\build.ps1 -Test      栞のテストホストも作り、テストも走らせる
#   .\build.ps1 -Clean     中間ファイルを消してから
#   .\build.ps1 -Release   きれいに作り直してテストし、配布用の zip を作る
#
[CmdletBinding()]
param(
    [switch]$Test,
    [switch]$Clean,
    [switch]$Release,
    [ValidateSet('x86', 'x64')]
    [string]$StudioArch = 'x64'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

# 配るときは、途中の作りかけが混ざらないように作り直して、テストも必ず通す
if ($Release) { $Clean = $true; $Test = $true }

$versionFile = Join-Path $root 'VERSION'
$version = if (Test-Path $versionFile) { (Get-Content $versionFile -Raw).Trim() } else { '0.0.0' }

# それぞれ別プロセスで動かす（32bit と 64bit の環境変数が混ざらないように）
$host_exe = (Get-Process -Id $PID).Path
if (-not $host_exe) { $host_exe = 'powershell.exe' }

function Invoke-Sub([string]$script, [string[]]$scriptArgs) {
    $all = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script) + $scriptArgs
    & $host_exe @all
    if ($LASTEXITCODE -ne 0) { throw "$([System.IO.Path]::GetFileName($script)) が失敗しました。" }
}

# ブロックの「そろい」だけは、ビルドが要りません。先に見ておくと、
# 書き忘れが 2 分待たずに分かります（くわしくは docs\maintenance.md）。
$node = Get-Command node -ErrorAction SilentlyContinue
if ($Test -and $node) {
    Write-Host '=== ブロックのそろい ===' -ForegroundColor Yellow
    & node (Join-Path $root 'tools\check-blocks.js')
    if ($LASTEXITCODE -ne 0) { throw 'check-blocks.js が失敗しました。' }
    Write-Host ''
}

$shioriArgs = @('-Arch', 'x86')
if ($Test) { $shioriArgs += '-Test' }
if ($Clean) { $shioriArgs += '-Clean' }
Write-Host '=== 栞 (nashi.dll) ===' -ForegroundColor Yellow
Invoke-Sub (Join-Path $root 'shiori\build.ps1') $shioriArgs

$studioArgs = @('-Arch', $StudioArch)
if ($Test) { $studioArgs += '-Test' }
if ($Clean) { $studioArgs += '-Clean' }
Write-Host ''
Write-Host '=== なしスタジオ (nashi-studio.exe) ===' -ForegroundColor Yellow
Invoke-Sub (Join-Path $root 'studio\build.ps1') $studioArgs

# テスト。Node.js を使うのはここだけで、アプリの動作には要りません。
#   そろい   … ブロックが、栞・説明・テストにそろっているか（上で先に走ります）
#   parity   … 同じ interp.cpp を 32bit と 64bit で組んで、出力が一致するか
#   behavior … 栞だけが持っている判断（どれを選ぶか・既定の反応・間引き）が期待どおりか
#   export   … 書き出したファイル（surfaces.txt など）の中身が期待どおりか
#   editor   … エディタのデータ整形とチェックタブが期待どおりか
#   modules  … 画面のファイルが読みこめて、呼び先がそろっているか（分けたときの移し忘れ）
if ($Test) {
    Write-Host ''
    Write-Host '=== テスト ===' -ForegroundColor Yellow
    if (-not $node) {
        Write-Host '  Node.js が無いので飛ばします（https://nodejs.org/ で入れると走ります）' -ForegroundColor DarkYellow
    } else {
        foreach ($t in @('shiori\test\parity\parity.js', 'shiori\test\behavior\behavior.js',
                         'studio\test\export.js', 'ui\test\editor.js', 'ui\test\modules.js')) {
            & node (Join-Path $root $t)
            if ($LASTEXITCODE -ne 0) { throw "$([System.IO.Path]::GetFileName($t)) が失敗しました。" }
        }
    }
}

# 配布用の zip。exe 1 つで動くので、読みものを添えるだけです。
if ($Release) {
    Write-Host ''
    Write-Host '=== 配布用のまとめ ===' -ForegroundColor Yellow
    $stage = Join-Path $root "obj\release\nashi-studio-$version"
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    New-Item -ItemType Directory -Force $stage | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $stage 'docs') | Out-Null

    Copy-Item (Join-Path $root 'nashi-studio.exe') $stage
    foreach ($f in @('README.md', 'CHANGELOG.md', 'LICENSE')) {
        $p = Join-Path $root $f
        if (Test-Path $p) { Copy-Item $p $stage }
    }
    Get-ChildItem (Join-Path $root 'docs') -Filter '*.md' -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName (Join-Path $stage 'docs') }

    $dist = Join-Path $root 'dist'
    New-Item -ItemType Directory -Force $dist | Out-Null
    $zip = Join-Path $dist "nashi-studio-$version.zip"
    if (Test-Path $zip) { Remove-Item -Force $zip }
    Compress-Archive -Path $stage -DestinationPath $zip
    $kb = [math]::Round((Get-Item $zip).Length / 1KB, 1)
    Write-Host "  できました -> $zip ($kb KB)" -ForegroundColor Green
    Write-Host '  中身: nashi-studio.exe / README.md / CHANGELOG.md / docs' -ForegroundColor DarkGray
}

Write-Host ''
Write-Host "できあがりました（v$version）。nashi-studio.exe をダブルクリックしてください。" -ForegroundColor Green
