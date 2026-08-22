# 手入れの手引き

しばらく触っていないこのリポジトリに戻ってきたとき用の覚え書きです。
「どこに何があるか」は README の**フォルダ構成**にあります。
ここに書くのは、**書いてある場所が離れていて、片方だけ直すと静かに壊れる決まり**のほうです。

---

## まず、これを走らせる

```powershell
node tools\check-blocks.js     # 1 秒。ビルド不要。ブロックがそろっているか
.\build.ps1 -Test              # 3 分ほど。ビルドしてテスト 7 本
```

`check-blocks.js` はビルドが要らないので、**手を入れたら、まずこれ**です。
`build.ps1 -Test` の中でも、ビルドより先に走ります（書き忘れが 2 分待たずに分かります）。

---

## ブロックを動かすのは、いつも栞です

**規則は `shiori/src/interp.cpp` の 1 か所にしかありません。**

| どこで動くか | 誰が動かすか |
|---|---|
| SSP に入れたゴースト | `nashi.dll`（32bit）の `interp.cpp` |
| エディタの「ためす」 | `nashi-studio.exe`（64bit）に積んだ **同じ** `interp.cpp` |

画面はブロックを解釈しません。`preview.cpp` ごしに栞へ頼んで、
返ってきたさくらスクリプトを出すだけです。

> むかしは画面の JavaScript に同じ規則をもう一度書いていました。
> 片方だけ直すと、エディタでは思ったとおりなのに SSP では違う、という形で
> あとから分かる、いちばん厄介なズレかたをしました。474 行を消してあります。

そのかわり **32bit と 64bit で同じものを組む**ことになったので、一致テストは
その 2 つを見くらべます。`int` と `size_t` の大きさに頼った書きかたをすると、
そこで止まります。

`interp.cpp` は SAORI の DLL の読みかたを知りません（`saori.h` の `SaoriHost` で
一枚はさんであります）。おかげでスタジオは `saori.cpp` を持たずに解釈部だけを使えます。
プレビューは SAORI を呼ばないので、そこに `NULL` を渡しています。

---

## ブロックを 1 つ足す

順番に、4 か所です。`node tools\check-blocks.js` が、抜けたところを名指しで教えます。

1. **`tools/blocks.js`** — `def({ type: '…', cat: '…', shape: '…', spec: '…', args: {…} })`。
   見た目と `ghost.json` の形が決まります。そして `N.PALETTE` の、置きたいカテゴリに
   名前を足します（足さないと、定義してもパレットに並びません）。
2. **`shiori/src/interp.cpp`** — `RunBlock` に `if (type == "…")`。**ここだけが動かす側です。**
   文字を出すときは直に足さず、**`Emit(ctx, …)`** を通してください（長さの上限がかかります）。
3. **`docs/blocks.md`** — 表に 1 行。`{"type":"…"}` の形と、出るさくらスクリプトを書きます。
4. **`shiori/test/parity/ghost.json`** — `OnP**` の帽子で 1 つ。`id` も付けてください。
   **毎回おなじ結果になる形**にしてください（乱数や時計を使うと、毎回ちがう答えになります）。

「空だと困る欄」があるブロックなら、**`studio/src/w2k/lint.cpp`**（チェックのたな）にも
足します。

`tools/blocks.js` を直したら、**`.\build.ps1` を通してください**
（`tools\gen-blockdefs.js` が C++ の表を作りなおします）。
`interp.cpp` を直したときは、**栞とスタジオの両方**を作りなおしてください
（`.\build.ps1` が順に作ります）。

### 情報ブロック（`sys`）を足すとき

`blocks.js` の `sys` の `options` と、`interp.cpp` の `SysValue` の 2 か所です。
時計まわりの値は毎回ちがう答えになるので、一致テストには乗せられません
（`check-blocks.js` が「テストで動かしていない情報ブロック」として、お知らせに並べます）。

### わざと片方だけにするとき

手書きの `ghost.json` むけに、パレットには出さない型や演算があります
（`stop`、`arith` の `min` / `max` など）。`tools/check-blocks.js` の **`KNOWN`** に、
**なぜそうなのかを書いて**足してください。書いておかないと、次に来たとき
「消し忘れ」なのか「わざと」なのか分かりません。

---

## テスト 13 本と、それぞれが守っているもの

| 走らせかた | 守っているもの |
|---|---|
| `node tools\check-blocks.js` | ブロックが 4 か所にそろっているか（書き忘れ探し） |
| `node shiori\test\parity\parity.js` | 同じ `interp.cpp` を 32bit と 64bit で組んで、出力が同じか（ビット幅に頼った書きかたを見つける） |
| `node shiori\test\behavior\behavior.js` | 栞**だけ**が持っている判断（どれを選ぶか・既定の反応・間引き・通信・SAORI） |
| `node studio\test\export.js` | 書き出したファイルの中身（バイトで見くらべ） |
| `node ui\test\editor.js` | 読みこみの整形（`model.js`）とチェックタブ（`lint.js`） |
| `node ui\test\modules.js` | `ui\js\*.js` が読みこめて、`N.Xxx.yyy()` の呼び先がそろっているか |
| `node tools\check-imports.js` | `nashi.dll` が Windows 2000 から読みこめるか（輸入表を直に読む） |
| `node studio\test\layout.js` | ネイティブ版のブロックの置き場所・つまみかた・描画（`studio\src\w2k`） |
| `node studio\test\image.js` | PNG の読み書きと inflate（`pngread.cpp` / `inflate.cpp`） |
| `node studio\test\window.js` | ネイティブ版の編集画面（つまむ・つなぐ・すてる・欄に打ちこむ。`window.cpp`） |
| `node studio\test\panel.js` | 右の作業だな。**言いあらわしが JavaScript 版と同じか**も見ます |
| `node studio\test\lint.js` | チェック。**JavaScript 版と同じことを言うか**を 1 件ずつくらべます |
| `node studio\test\normalize.js` | 読みこんだときの下ごしらえ（`NormalizeProject`）とかたまりの見出し |

`parity` と `behavior` は `shiori\dist\test_host.exe` を、`parity` はさらに
`studio\test\preview_host.exe` を使うので、**先にビルドが要ります**（`.\build.ps1 -Test`）。
`check-imports` も出来あがった `nashi.dll` を見るので、ビルドが要ります。
`check-blocks` と `editor` と `modules` はビルド無しで走ります。
`layout` は `studio\test\layout_host.exe` と `render_host.exe` を使います。
`image` は `studio\test\png_host.exe`、`window` は `render_host.exe` を使います。

書き出しかたをわざと変えたときは、出てきたものを**目で確かめてから**
`node studio\test\export.js --update` で期待値を作りなおします。

### 押したら勝手に走ります

`.github/workflows/build.yml` が、`main` への push・タグ（`v*`）・プルリクのたびに
**手元と同じ `.\build.ps1 -Test`** を windows-latest で走らせます。
打ち忘れても止まるようにするためのものなので、**赤くなったら直してから進んでください**。

* ジョブは 2 つ。`そろい点検`（ビルド不要・1 分ほど）が先に赤/緑を出し、
  `ビルドとテスト`（5 分ほど）が本番と同じ手順を通します。
* できた `nashi-studio.exe` と `nashi.dll` は、実行結果の画面から 14 日間落とせます
  （署名していないので、手元の exe と同じく Windows に止められることがあります）。
* ランナーには MSVC も Windows SDK も入っています。**落としてくるものはありません**
  （外の物には何も頼っていないので）。
* スマートアプリコントロールはランナーには無いので、手元より素直に通ります。

---

## 画面（`studio\src\w2k`）

画面は Win32 と GDI だけで描いています。WebView2 は Windows 10 からしか動かないので、
外しました。ここは**わざと分けてあります**。

| ファイル | 何をするか | GDI |
|---|---|---|
| `blockdefs.h` / `blockdefs_gen.h` | ブロックの表（`tools\blocks.js` から `tools\gen-blockdefs.js` が作る） | 使わない |
| `layout.cpp` | 幅と高さを決める。どこを押したかを探す | **使わない** |
| `drag.cpp` | どこにつなげるか。つまむ・はなすと ghost.json がどう変わるか | **使わない** |
| `panel.cpp` | 右の作業だなの中身を組み立てる | **使わない** |
| `lint.cpp` | あぶないところ探し（チェック） | **使わない** |
| `paint.cpp` | 決まったものを描く | 使う |
| `window.cpp` | 窓を出して、マウスとキーを受ける | 使う |

設定ファイルを知っているのは `main.cpp` だけです。画面のコードには
`SetShioriDll` / `SetSspHint` / `SetProjectsDir` と `EditorOptions` で渡します。

GDI が出てこないものが多いのは、**画面を出さずにテストするため**です。
文字の幅だけは環境で変わるので、`TextMeasurer` を外から渡してもらいます
（本物は GDI、テストは「半角 7px・全角 14px」の決め打ち）。

確かめるときは、コンソールから直に呼べます。

```powershell
.\studio\test\layout_host.exe .\shiori\test\parity\ghost.json p05
.\studio\test\layout_host.exe .\shiori\test\parity\ghost.json --drops p05 stack
.\studio\test\layout_host.exe .\shiori\test\parity\ghost.json --move p05 1 16 84
.\studio\test\render_host.exe .\shiori\test\parity\ghost.json --all .\studio\test\render_out
```

`render_host.exe` は窓を出さずに PNG を書き出すので、**見た目を目で確かめられます**。
置き場所を触ったら、数字だけでなく絵も一度見てください（数字が合っていても、
とがったところに欄がはみ出す、といったことが起きます）。

### 画面は「窓を出さずに」動かせます

画面まわりは、動かしてみないと分からないことばかりです。そこで `window.cpp` には
**窓を出さずにマウスの動きだけまねる入口**（`ProbeEditor`）を用意してあります。
`studio\test\window.js` はこれを使って、つまむ・つなぐ・すてるを確かめています。

```powershell
# 編集の画面ぜんぶを PNG に（左のブロック置き場もふくめて）
.\studio\test\render_host.exe .\studio\test\drag_fixture.json --window out.png 900 600

# 左のブロック置き場に、何がどこにあるか
.\studio\test\render_host.exe .\studio\test\drag_fixture.json --palette x

# (260,165) をつまんで (260,101) へ。はなしたあとの ghost.json が返ります
.\studio\test\render_host.exe .\studio\test\drag_fixture.json --drag out.png 260 165 260 101 --drop

# 左の置き場から名前でつまむ（ならびが変わってもテストが壊れないように）
.\studio\test\render_host.exe .\studio\test\drag_fixture.json --drag out.png 0 0 260 101 --drop --from newline

# 画面に出ている欄をぜんぶならべる（よこの位置は文字の幅で変わるので、ここから引きます）
.\studio\test\render_host.exe .\studio\test\drag_fixture.json --fields

# その場所の欄を調べる。中身をうしろに付けると書きかえます
.\studio\test\render_host.exe .\studio\test\drag_fixture.json --field 470 104
.\studio\test\render_host.exe .\studio\test\drag_fixture.json --field 470 104 0
```

テストで場所を指すときは、**たての位置**でえらんでください。ブロックの高さは
文字の幅に左右されないので、どの環境でも同じところをつかめます。
よこは文字の幅で変わるので、左のはしのあたりを押すか、`--fields` から引きます。

ブロックをつかむときは、**左のはし**を押してください。すこし右は欄になっていて、
押すと打ちこみ・えらびに入ります（それはそれで正しい動きです）。

### 右の作業だな

`panel.cpp` が「見出し・説明・ボタン・欄・行」のならびを作り、`window.cpp` が
それを描いて、押されたところを探します。部品には `var.del.2` のような目じるしが
付いていて、テストはこれで押す場所を言います（見た目が変わっても壊れません）。

たなは 9 つあります。番号はこの順です。

| 番号 | たな | いまのようす |
|---|---|---|
| 0 | ためす | かたまりを押すと、**栞そのもの**で動かします。動いている SSP へも送れます |
| 1 | ゴースト | 名前・作者・更新のありか・ランダムトークの設定 |
| 2 | 立ち絵 | 6 つの見本を出す。色をえらぶ／PNG を割りあてる |
| 3 | うごき | SERIKO のこまと、そのあいだだけの当たり判定 |
| 4 | 変数 | つくる・けす・名前とはじめの値 |
| 5 | さがす | セリフ・変数・トーク名から引く |
| 6 | チェック | `lint.cpp` の結果 |
| 7 | 書き出し | フォルダ／`.nar`／SSP に入れて動かす。栞は `SetShioriDll` で渡してもらいます |
| 8 | ヘルプ | つかいかたとキーの案内 |

「立ち絵」のたなは、**書いた PNG デコーダの出番**です。割りあてられた PNG は
`pngread.cpp` で読み、仮シェルは `exporter.cpp` の `RenderSurfacePng` で描いて、
どちらも同じように GDI へ渡します（`StretchDIBits`）。同じ絵を何度も作らないよう、
`ShellPicFor` が覚えておきます。色を変えたときは忘れさせます。

テストは、**読んで・描いて・また読む**まで通します。まっ赤な PNG を割りあて、
画面を絵にして、`png_host.exe --pixel` でその色が出ているかを見ます。

```powershell
# たなを見る
.\studio\test\render_host.exe .\studio\test\drag_fixture.json --panel 2

# ボタンを押してみる。うしろに字を足すと、その欄に打ちこみます
.\studio\test\render_host.exe .\studio\test\drag_fixture.json --panel 2 --click var.add
.\studio\test\render_host.exe .\studio\test\drag_fixture.json --panel 2 --click var.name.0 すきど

# さがす／動かす／書き出す
.\studio\test\render_host.exe .\studio\test\drag_fixture.json --panel 3 --q はじめ
.\studio\test\render_host.exe .\shiori\test\parity\ghost.json --panel 0 --click run.go.0
.\studio\test\render_host.exe .\studio\test\drag_fixture.json --panel 5 --dir C:\out --click export.folder

# たなを開いた画面を、絵にする（さいごの数がたなの番号）
.\studio\test\render_host.exe .\shiori\test\parity\ghost.json --window out.png 1200 760 4
```

「ためす」と「書き出し」の中身は `studio\src\preview.cpp` と
`studio\src\exporter.cpp` にあります。`window.cpp` が exe のリソースを
直に見ないのは、テストからも動かせるようにするためです。栞（`nashi.dll`）は
`SetShioriDll` で渡してもらい、`main.cpp` がそれを入れます。

### 言葉が変わっていないか見張る

チェックの言いかたと、ブロック・かたまりの言いあらわしは、**前と同じかを 1 つずつ
くらべます**。期待するものは `studio\test\expected\` に置いてあります。
言いかたをわざと直したときは、**出てきたものを目で確かめてから**置きかえます。

```powershell
node studio\test\lint.js --update
node studio\test\panel.js --update
```

もとは JavaScript 版（`lint.js` / `model.js`）と見くらべていました。
そちらを外したので、いまは「前と同じか」を見ています。

* `panel.js` … かたまりの見出しとブロックの言いあらわし（3 つのゴースト、323 行）
* `lint.js` … チェックの結果を、出た順・段階・言葉・説明の 4 つとも
  （見本 3 つ＋お手本 5 つ＋**わざと変なところを作った 16 とおり**）

見本のゴーストだけでは通らない道が多いので、`lint.js` は変な ghost.json を
その場で組み立てます。空の欄、無いものを指している、毎秒しゃべる、といった形です。

この見張りが、読みこみの下ごしらえ（`NormalizeProject`）の抜けを 2 つ見つけました。

1. 読みこんだゴーストは、しぼり込みを `filter` の中に持っています。
   **編集するときは `area` / `who` / `from` / `contains` にほどく**必要があります。
2. `settings` と `meta` の**既定値をうめていません**でした。
   うめないと「しゃべる間隔が 0 秒」と言ってしまいます。

### 部品のたぐい

`PanelItem` の `kind` で、出しかたと押されたときのことが決まります。

| kind | 出しかた | 押すと |
|---|---|---|
| `Head` / `Hint` / `Text` | 字を出すだけ | 何も |
| `Button` | 枠つきのボタン | その目じるしの手あて |
| `Field` | 左に見出し、右に打ちこみ欄 | 打ちこみに入る |
| `Color` | 欄の右はしに、その色の四角 | 色えらびの窓（窓が無ければ打ちこみ） |
| `Choice` | 欄の右はしに ▼ | えらぶならびが出る（窓が無ければ打ちこみ） |
| `Image` | 立ち絵の見本 | 画像をえらぶ |
| `Row` | 1 行のならび（説明つき） | その場所へ動く／動かす |

`Color` と `Choice` と `Image` は、押すと Windows の窓が出ます。窓が無いとき
（テスト）は打ちこみと同じ道に落ちるので、**入れ先を決めるところは一本**です
（`ApplyEditText`）。テストはその一本を通ります。

### 欄のこと

* 打ちこむ欄は、その場に小さな EDIT の窓を重ねて出します。日本語の入力も、
  文字の選びなおしも、Windows にまかせられるからです。Enter で決まり、Esc でやめ、
  よそを押しても決まります。
* えらぶ欄は `TrackPopupMenu` を出します。
* えらべるものは、決まっているもの（`blocks.js` の options）のほか、
  **その ghost から作る**ものがあります。変数の名前、トークの名前、
  うごきに書いてある当たり判定の名前です。
* イベント名は `blocks.js` の `N.EVENTS` を、`tools\gen-blockdefs.js` が
  C++ の表に写しています。ここも手で書き写さないでください。
  うごきの言葉（`N.ANIM_INTERVALS` / `N.ANIM_METHODS` / `N.ANIM_SHAPES`）も同じです。
  もともと `shell.js` にありましたが、両方から使うので `blocks.js` へ移しました。
* 数の欄は、ちゃんと数になっていれば**数として**入れます（`ValueForField`）。
  「1.5あ」のように途中までしか数でないものは、文字のままにします。

```powershell
.\nashi-studio.exe                                    前に開いていたものを出します
.\nashi-studio.exe .\studio\test\drag_fixture.json  そのファイルを開きます
```

窓の場所と、前に開いていたものは `nashi-studio.json` に覚えます。

### SSP に送るところ

`sstp.cpp` を通して、動いている SSP に送れます。ボタンは「ためす」と「書き出し」に
あります。

| 目じるし | すること |
|---|---|
| `ssp.check` | SSP を探して、様子を出す |
| `ssp.say` | さいごに動かしたさくらスクリプトを、いまのゴーストにしゃべらせる |
| `ssp.event` | さいごに動かしたかたまりのイベントを送る |
| `ssp.comm` | 他のゴーストのふりをして話しかける（OnCommunicate の確かめ） |
| `ssp.forget` | 書き出したゴーストが覚えた変数（`nashi_save.json`）を消す |
| `ssp.install` | SSP の ghost フォルダへ書き出して、そのゴーストに切りかえる |

SSP の場所は設定（`nashi-studio.json` の `sspPath`）に覚えてあるものを、
`main.cpp` が `SetSspHint` で渡します。

テストを走らせる機械で SSP が動いているとはかぎらないので、`panel.js` は
**動いていないときに、送らずにそう言うか**を見ています。
`ssp.forget` は本当にファイルを消すので、テストでは**名前のかぶらないゴースト**で
呼んでいます（うっかり本物の記憶を消さないため）。

**WebView2 は外しました。** 画面も、ブロックの表も、チェックも、下ごしらえも、
ぜんぶ C++ にあります。JavaScript で残っているのは `tools/blocks.js`（ブロック定義の
「正」）と、node で走らせるテストだけです。

exe も **32bit・CRT なし**で組むようになりました。栞と同じやりかたです
（下の「C ランタイムを使わずに組んでいます」を見てください）。
`node tools\check-imports.js` が、栞と exe の**両方**を見ます。

つなぎかたの決まりは `ui\js\drag.js` と同じにしてあります。片方だけ変えないでください。

* つながる先は 46px まで（縦のずれを重く見る）／欄にはまるのは 34px まで
* 「ここでおわる」ブロックの後ろにはつなげない
* 六角の欄には六角のブロックだけ、丸いブロックは空いている**打ちこみ**欄だけ
* つまむと、その下にあるものもついてくる

## 絵の読み書き（`studio\src`）

外の物に頼らない決まりなので、PNG も zlib も自前です。

| ファイル | 何をするか |
|---|---|
| `deflate.cpp` | 縮める（LZ77 ＋ 決まりきったハフマン）。ZIP と PNG の両方が使います |
| `inflate.cpp` | ほどく。**そのまま・決まりきった・その場で作った**の 3 とおりぜんぶ |
| `image.cpp` | PNG を書く。絵を描く道具（`Canvas`）も |
| `pngread.cpp` | PNG を読む。RGBA にして返します |

読むほうは、シェルの絵を出すために要ります。世の中の PNG は書きかたがまちまちなので、
出てくるものはひととおり扱います。

* 色の入れかた 0（灰）/ 2（RGB）/ 3（色見本）/ 4（灰＋透け）/ 6（RGBA）
* 1 色あたり 1, 2, 4, 8, 16 ビット（16 ビットは上の 8 ビットだけ使います）
* 行の下ごしらえ 0〜4（なし・左・上・平均・Paeth）
* とびとびの並べかた（Adam7）
* `tRNS`（灰・RGB・色見本の、透ける色の指定）

扱わないのは、動く PNG（APNG は 1 枚目だけ）と `gAMA` などの色あわせです。

見本の PNG は `studio\test\image.js` が**その場で組み立てます**。ファイルとして
置いていないのは、「どう作ったか」がテストに書いてあるほうが直しやすいからです。
確かめかたは、コンソールからも呼べます。

```powershell
.\studio\test\png_host.exe --info  なにか.png
.\studio\test\png_host.exe --pixel なにか.png 3 5
.\studio\test\png_host.exe --round なにか.png    # 読む → 書く → また読む
```

## C ランタイムを使わずに組んでいます（栞も、なしスタジオも）

SSP は **Windows 2000 以降**で動きます。栞は SSP に読みこまれる DLL なので、そこに合わせます。

ところが Visual Studio の C ランタイムをふつうに静的リンク（`/MT`）すると、**自分では
一度も呼んでいないのに** `FlsAlloc` や `InitializeCriticalSectionEx`（どちらも Vista から）
といった API が輸入表に載ります。輸入表は**読みこみのときに全部そろっている必要がある**ので、
古い Windows では **DLL を開くことすらできません**。手元が Windows 11 だと当然動くので、
ソースを読んでも気づけません。

そこで栞は、

* `/NODEFAULTLIB` で CRT を丸ごと外し、
* **Windows に最初から入っている `msvcrt.dll`** につなぎ（`shiori/src/msvcrt.def`）、
* それでも足りない Visual Studio 独自のものだけを `shiori/src/tinycrt.cpp` で用意する

という組みかたにしてあります。`strtod` や `sprintf` のような「自分で書くと間違えるもの」は
借りものの本物です。`nashi.dll` は 268KB から 134KB になりました。

**ねらいどおりかは `node tools\check-imports.js` が見ます。** 出来あがった PE の輸入表を
直に読んで、Windows 2000 に無い API が混ざっていたら名指しで止めます。
`.\build.ps1 -Test` にも入っています。

### ここを触るときの注意

* **`shiori/src/msvcrt.def` は ASCII だけで書いてください。** `lib.exe` は `.def` を
  ANSI コードページで読むので、日本語のコメントを入れると**次の行を巻きこんで消します**
  （実際に `memcpy` と `strlen` が黙って落ちました）。
* 借りる関数を足すときは、**Windows 2000 の `msvcrt.dll` にもあるか**確かめてください。
* 例外は投げません。投げるはずの場面（メモリ不足・長さ超過）は、その場で終わります。
  栞は 32KB で頭打ちにしてあるので、ふつうは起きません。
* `<random>` や 64bit 整数と小数の行き来は、CRT にしか無い関数を呼びます。
  避けかたは `util.cpp` の `NumToStr` と乱数のところにコメントで書いてあります。
* **なしスタジオ（64bit）はふつうの CRT のままです。** 同じ `.cpp` を両方で組んでいますが、
  `tinycrt.cpp` をリンクするのは栞だけです。一致テストが、両方で同じ答えになることを見ています。

---

## つまずきやすいところ

**スマートアプリコントロールが、作りたてのファイルを止める。**
`test_host.exe` や `nashi.dll` や `nashi-studio.exe` が
「An Application Control policy has blocked this file」（エラー 4551）で
読み込めないことがあります。**もう一度ビルドすると通る**ことが多いです。
くわしくは README の「スマートアプリコントロールでブロックされたら」。

ウイルス対策（Defender）に見つかったのだと思いがちですが、**別のものです**。
止めているのはコード整合性（Code Integrity）で、Defender の検出履歴には何も残りません。
**どちらなのかは、この 2 つで分かります。**

```powershell
# 1=オン(強制) / 2=評価中 / 0=オフ
Get-ItemPropertyValue "HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy" VerifiedAndReputablePolicyState

# 止めた本人が、ファイル名つきで残っている（ID 3077 がブロック）
Get-WinEvent -FilterHashtable @{LogName="Microsoft-Windows-CodeIntegrity/Operational"; Id=3077} -MaxEvents 5 |
  Select-Object TimeCreated, Message
```

ここに `nashi.dll` の名前が出ていれば、それが原因です。
Defender のほうを疑うなら `Get-MpThreatDetection`（何も出なければ、Defender は無関係）。

**`studio\test\expected\` は、バイトのまま見くらべています。**
`.gitattributes` で `-text` にして、git が改行を書きかえないようにしてあります。
期待値を作りなおしたあとに差分がおかしいときは `git add --renormalize` を試してください。

**テスト用の SAORI（`slow_saori.dll`）は `load` / `unload` を持っています。**
栞と同じ名前なので、中間ファイルを同じ場所に置くとリンクでぶつかります
（`obj\x86\testbuild\` に分けてあります）。

**`nashi-studio.exe` は画面つきです。** スクリプトから `& .\nashi-studio.exe` と書くと
戻ってきません。動きを確かめたいときは自分の手で起動してください。

**`ui/` を直したら exe を作りなおす。** ブラウザで `ui\index.html` を直接開くと
見た目は出ますが、保存・書き出し・「ためす」は動きません（どれも exe の中を呼んでいます）。

---

## 手を入れるときの進めかた

**main に直に push しません。** 作業ごとにブランチを切って、Pull Request で入れます。

```powershell
git switch -c fix/なんとか      # 直すもの
git switch -c feature/なんとか  # 足すもの
git switch -c docs/なんとか     # 読みもの
git switch -c test/なんとか     # テスト
```

1. **Issue を立てる**。何が困っているか、どうしたいかを書きます
2. **ブランチを切る**。細かくてかまいません（1 つのことに 1 つ）
3. 直したら **`.\build.ps1 -Test` を通してから**コミットします
4. `git push -u origin <ブランチ>` して、**Pull Request を出す**
5. CI が緑になったら main へ

大きな変更（戻しにくいもの）は、**段に分けて別々の PR** にします。
WebView2 をやめたときは 6 段に分けました。1 つずつ CI を通しておくと、
あとで「どこで変わったか」を追えます。

コミットの言葉は日本語で、**何をしたか**と**なぜか**を書きます。

---

## 版を上げて配る

1. `VERSION`（ルートの 1 行）を直す。zip の名前と exe のプロパティは、ここだけを見ています。
2. `CHANGELOG.md` に、変わったことを足す。
3. `.\build.ps1 -Release` — きれいに作り直し、テスト 7 本を通し、`dist\nashi-studio-<版>.zip` を作ります。
4. `git tag -a v<版> -m "…"` して `git push origin v<版>`。
5. GitHub の Releases で、そのタグからリリースを作り、zip を添える
   （`gh` があれば `gh release create v<版> dist\nashi-studio-<版>.zip -F CHANGELOG.md`）。

---

## 迷ったら

* ブロックの意味 → `docs/blocks.md`
* `ghost.json` の形 → `docs/ghost-json.md`
* つかいかた・フォルダ構成・栞のしくみ → `README.md`
* さくらスクリプトや SHIORI の仕様そのもの → UKADOC（伺か関連ドキュメント）
