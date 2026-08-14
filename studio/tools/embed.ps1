# なしスタジオ - UI・栞・サンプルを exe に埋め込むためのリソースを作る
#
#   assets.rc      … rc.exe に渡すリソース定義
#   assets_gen.h   … パス → リソースID の対応表
#
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PublicDir,
    [Parameter(Mandatory = $true)][string]$StageDir,
    [string]$DllPath,
    [string]$SamplePath,
    [string]$IconPath
)

$ErrorActionPreference = 'Stop'

$MimeMap = @{
    '.html' = 'text/html; charset=utf-8'
    '.css'  = 'text/css; charset=utf-8'
    '.js'   = 'text/javascript; charset=utf-8'
    '.json' = 'application/json; charset=utf-8'
    '.svg'  = 'image/svg+xml'
    '.png'  = 'image/png'
    '.ico'  = 'image/x-icon'
    '.woff2' = 'font/woff2'
    '.map'  = 'application/json; charset=utf-8'
}

$assetsDir = Join-Path $StageDir 'assets'
if (Test-Path $assetsDir) { Remove-Item -Recurse -Force $assetsDir }
New-Item -ItemType Directory -Force -Path $assetsDir | Out-Null

# ---- UI ファイルを ASCII 名で並べ直す（rc.exe に日本語パスを渡さないため）----
$files = Get-ChildItem -Path $PublicDir -Recurse -File | Sort-Object FullName
$entries = @()
$id = 1000
foreach ($f in $files) {
    $rel = $f.FullName.Substring((Resolve-Path $PublicDir).Path.Length).TrimStart('\', '/')
    $webPath = '/' + ($rel -replace '\\', '/')
    $stageName = 'a{0}.bin' -f $id
    Copy-Item $f.FullName (Join-Path $assetsDir $stageName) -Force
    $ext = $f.Extension.ToLowerInvariant()
    $mimeType = if ($MimeMap.ContainsKey($ext)) { $MimeMap[$ext] } else { 'application/octet-stream' }
    $entries += [pscustomobject]@{ Path = $webPath; Id = $id; Mime = $mimeType; File = $stageName }
    $id++
}

# ---- 栞・サンプル・アイコン ----------------------------------------------
$rc = New-Object System.Text.StringBuilder
[void]$rc.AppendLine('// このファイルは studio\tools\embed.ps1 が自動生成します。編集しないでください。')

if ($IconPath -and (Test-Path $IconPath)) {
    Copy-Item $IconPath (Join-Path $assetsDir 'nashi.ico') -Force
    [void]$rc.AppendLine('101 ICON "assets\\nashi.ico"')
}
if ($DllPath -and (Test-Path $DllPath)) {
    Copy-Item $DllPath (Join-Path $assetsDir 'nashi.dll') -Force
    [void]$rc.AppendLine('900 RCDATA "assets\\nashi.dll"')
} else {
    Write-Warning '[embed] nashi.dll が見つかりません。栞なしの exe になります。'
}
if ($SamplePath -and (Test-Path $SamplePath)) {
    Copy-Item $SamplePath (Join-Path $assetsDir 'sample.json') -Force
    [void]$rc.AppendLine('901 RCDATA "assets\\sample.json"')
}
foreach ($e in $entries) {
    [void]$rc.AppendLine(('{0} RCDATA "assets\\{1}"' -f $e.Id, $e.File))
}

$rcPath = Join-Path $StageDir 'assets.rc'
[System.IO.File]::WriteAllText($rcPath, $rc.ToString(), (New-Object System.Text.UTF8Encoding($false)))

# ---- 対応表 ---------------------------------------------------------------
$h = New-Object System.Text.StringBuilder
[void]$h.AppendLine('// このファイルは studio\tools\embed.ps1 が自動生成します。編集しないでください。')
[void]$h.AppendLine('#pragma once')
[void]$h.AppendLine('')
[void]$h.AppendLine('struct WebAsset { const char* path; int id; const char* mime; };')
[void]$h.AppendLine('')
[void]$h.AppendLine('static const WebAsset kWebAssets[] = {')
foreach ($e in $entries) {
    [void]$h.AppendLine(('    {{ "{0}", {1}, "{2}" }},' -f $e.Path, $e.Id, $e.Mime))
}
[void]$h.AppendLine('};')
[void]$h.AppendLine(('static const int kWebAssetCount = {0};' -f $entries.Count))

$hPath = Join-Path $StageDir 'assets_gen.h'
[System.IO.File]::WriteAllText($hPath, $h.ToString(), (New-Object System.Text.UTF8Encoding($false)))

Write-Host ("[embed] UI {0} ファイルを埋め込みます" -f $entries.Count) -ForegroundColor DarkGray
