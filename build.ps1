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

Write-Host ''
Write-Host 'できあがりました。nashi-studio.exe をダブルクリックしてください。' -ForegroundColor Green
