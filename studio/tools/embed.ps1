# なしスタジオ - 栞・見本・アイコンを exe に埋め込むためのリソースを作る
#
#   assets.rc … rc.exe に渡すリソース定義
#
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$StageDir,
    [string]$DllPath,
    [string]$SamplePath,
    [string]$IconPath,
    [string]$Version = '0.0.0'
)

$ErrorActionPreference = 'Stop'

$assetsDir = Join-Path $StageDir 'assets'
if (Test-Path $assetsDir) { Remove-Item -Recurse -Force $assetsDir }
New-Item -ItemType Directory -Force -Path $assetsDir | Out-Null

# ---- 栞・見本・アイコン ----------------------------------------------
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
# ---- exe のプロパティに出るバージョン情報 ----------------------------------
$nums = @($Version -split '[.\-+]' | Where-Object { $_ -match '^\d+$' })
while ($nums.Count -lt 4) { $nums += '0' }
$v4 = ($nums[0..3]) -join ','
[void]$rc.AppendLine('')
[void]$rc.AppendLine('1 VERSIONINFO')
[void]$rc.AppendLine("FILEVERSION $v4")
[void]$rc.AppendLine("PRODUCTVERSION $v4")
[void]$rc.AppendLine('FILEOS 0x4L')
[void]$rc.AppendLine('FILETYPE 0x1L')
[void]$rc.AppendLine('{')
[void]$rc.AppendLine(' BLOCK "StringFileInfo"')
[void]$rc.AppendLine(' {')
[void]$rc.AppendLine('  BLOCK "041104b0"')          # 日本語 + Unicode
[void]$rc.AppendLine('  {')
[void]$rc.AppendLine('   VALUE "ProductName", "なしスタジオ"')
[void]$rc.AppendLine('   VALUE "FileDescription", "なしスタジオ - 伺かゴーストをブロックで作る"')
[void]$rc.AppendLine('   VALUE "FileVersion", "' + $Version + '"')
[void]$rc.AppendLine('   VALUE "ProductVersion", "' + $Version + '"')
[void]$rc.AppendLine('   VALUE "OriginalFilename", "nashi-studio.exe"')
[void]$rc.AppendLine('  }')
[void]$rc.AppendLine(' }')
[void]$rc.AppendLine(' BLOCK "VarFileInfo"')
[void]$rc.AppendLine(' {')
[void]$rc.AppendLine('  VALUE "Translation", 0x411, 1200')
[void]$rc.AppendLine(' }')
[void]$rc.AppendLine('}')

$rcPath = Join-Path $StageDir 'assets.rc'
[System.IO.File]::WriteAllText($rcPath, $rc.ToString(), (New-Object System.Text.UTF8Encoding($false)))

Write-Host '[embed] 栞と見本を埋め込みます' -ForegroundColor DarkGray
