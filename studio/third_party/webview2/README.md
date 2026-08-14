# WebView2 SDK（同梱）

NuGet の **Microsoft.Web.WebView2 1.0.4129.50** から、ビルドに必要なファイルだけを取り込んだものです。
<https://www.nuget.org/packages/Microsoft.Web.WebView2/>

| | |
|---|---|
| `include/WebView2.h`, `WebView2EnvironmentOptions.h` | ヘッダ |
| `x64/WebView2LoaderStatic.lib`, `x86/WebView2LoaderStatic.lib` | 静的リンク用のローダ（実行時に余分な DLL が要らない） |
| `LICENSE.txt` | ライセンス本文（Microsoft Corporation） |

## ライセンス

Copyright (C) Microsoft Corporation. All rights reserved.

BSD 3-Clause 系のライセンスで、**ソース・バイナリどちらの形でも再配布が認められています**。
条件は「著作権表示・条件文・免責事項を残すこと」「Microsoft の名前を推奨表示に使わないこと」です。
全文は同じフォルダの `LICENSE.txt` を見てください。

## 更新のしかた

新しい版に差し替えるときは、nupkg（ただの zip）から同じファイルを取り出して置き換えます。

```powershell
$ver = '1.0.4129.50'
Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$ver/microsoft.web.webview2.$ver.nupkg" -OutFile webview2.zip
# zip の中の build/native/include/*.h と build/native/{x64,x86}/WebView2LoaderStatic.lib、
# それに LICENSE.txt をこのフォルダへ
```

WebView2 の**ランタイム本体**は Windows 11 に標準で入っています（アプリ側に同梱は不要）。
