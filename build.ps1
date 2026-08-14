# なし栞 + なしスタジオ をまとめてビルドする
#
#   .\build.ps1            栞(32bit DLL) → スタジオ(64bit EXE) の順にビルド
#   .\build.ps1 -Test      栞のテストホストも作る
#   .\build.ps1 -Clean     中間ファイルを消してから
#
[CmdletBinding()]
param(
    [switch]$Test,
    [switch]$Clean,
    [ValidateSet('x86', 'x64')]
    [string]$StudioArch = 'x64'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

# それぞれ別プロセスで動かす（32bit と 64bit の環境変数が混ざらないように）
$host_exe = (Get-Process -Id $PID).Path
if (-not $host_exe) { $host_exe = 'powershell.exe' }

function Invoke-Sub([string]$script, [string[]]$scriptArgs) {
    $all = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $script) + $scriptArgs
    & $host_exe @all
    if ($LASTEXITCODE -ne 0) { throw "$([System.IO.Path]::GetFileName($script)) が失敗しました。" }
}

$shioriArgs = @('-Arch', 'x86')
if ($Test) { $shioriArgs += '-Test' }
if ($Clean) { $shioriArgs += '-Clean' }
Write-Host '=== 栞 (nashi.dll) ===' -ForegroundColor Yellow
Invoke-Sub (Join-Path $root 'shiori\build.ps1') $shioriArgs

$studioArgs = @('-Arch', $StudioArch)
if ($Clean) { $studioArgs += '-Clean' }
Write-Host ''
Write-Host '=== なしスタジオ (nashi-studio.exe) ===' -ForegroundColor Yellow
Invoke-Sub (Join-Path $root 'studio\build.ps1') $studioArgs

# プレビュー(ui\js\sim.js) と 栞(interp.cpp) がズレていないか見くらべる。
# 同じ規則を二重に実装しているので、片方だけ直したまま気づかない事故を防ぐため。
# Node.js を使うのはここだけで、アプリの動作には要りません。
if ($Test) {
    Write-Host ''
    Write-Host '=== プレビューと栞の一致テスト ===' -ForegroundColor Yellow
    $node = Get-Command node -ErrorAction SilentlyContinue
    if (-not $node) {
        Write-Host '  Node.js が無いので飛ばします（https://nodejs.org/ で入れると走ります）' -ForegroundColor DarkYellow
    } else {
        & node (Join-Path $root 'shiori\test\parity\parity.js')
        if ($LASTEXITCODE -ne 0) { throw '一致テストが失敗しました。' }
    }
}

Write-Host ''
Write-Host 'できあがりました。nashi-studio.exe をダブルクリックしてください。' -ForegroundColor Green
