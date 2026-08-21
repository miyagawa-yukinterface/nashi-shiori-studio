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

画面（`ui/js/`）はブロックを解釈しません。`POST /api/preview` に頼んで、
返ってきたさくらスクリプトを `ui/js/player.js` が再生するだけです。

> むかしは `ui/js/sim.js` に同じ規則を JavaScript でもう一度書いていました。
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

1. **`ui/js/blocks.js`** — `def({ type: '…', cat: '…', shape: '…', spec: '…', args: {…} })`。
   見た目と `ghost.json` の形が決まります。そして `N.PALETTE` の、置きたいカテゴリに
   名前を足します（足さないと、定義してもパレットに並びません）。
2. **`shiori/src/interp.cpp`** — `RunBlock` に `if (type == "…")`。**ここだけが動かす側です。**
   文字を出すときは直に足さず、**`Emit(ctx, …)`** を通してください（長さの上限がかかります）。
3. **`docs/blocks.md`** — 表に 1 行。`{"type":"…"}` の形と、出るさくらスクリプトを書きます。
4. **`shiori/test/parity/ghost.json`** — `OnP**` の帽子で 1 つ。`id` も付けてください。
   **毎回おなじ結果になる形**にしてください（乱数や時計を使うと、毎回ちがう答えになります）。

「空だと困る欄」があるブロックなら、**`ui/js/lint.js`**（チェックタブ）にも足します。

`ui/` を直したら、**`studio\build.ps1` で exe を作りなおします**（UI は exe に埋め込まれています）。
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

## テスト 9 本と、それぞれが守っているもの

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

`parity` と `behavior` は `shiori\dist\test_host.exe` を、`parity` はさらに
`studio\test\preview_host.exe` を使うので、**先にビルドが要ります**（`.\build.ps1 -Test`）。
`check-imports` も出来あがった `nashi.dll` を見るので、ビルドが要ります。
`check-blocks` と `editor` と `modules` はビルド無しで走ります。
`layout` は `studio\test\layout_host.exe` と `render_host.exe` を使います。
`image` は `studio\test\png_host.exe` を使います。

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
* ランナーには MSVC も Windows SDK も入っていて、WebView2 の SDK は
  `studio/third_party` に置いてあるので、**落としてくるものはありません**。
* スマートアプリコントロールはランナーには無いので、手元より素直に通ります。

---

## 画面のファイルの決まりごと

`ui/js/` は `index.html` に書いた順に読みこまれ、それぞれ `window.NASHI`（`N`）に
自分のぶんを載せます。**読みこみ順が、そのまま依存の向き**です。

```
blocks → model → player → lint → render → drag → app → shell / search / dialog / ssp
```

* **先に読まれるものは、載せてある名前をそのまま受け取れます**
  （`const Model = N.Model;` のように、ファイルの先頭で）。
* **あとに読まれるものを呼ぶときは、その場で引きます**（`N.Shell.playSakura(...)`）。
  先頭で `const Shell = N.Shell;` と書くと、まだ載っていないので `undefined` になります。
* `app.js` が分けたファイルへ渡すものは、`app.js` の終わりのほうで `App.api = api;` の形で
  まとめて載せています。足すときはそこに書いてください。

`node ui\test\modules.js` が、この決まりを守れているかを見ます
（`N.Xxx.yyy()` の呼び先が無ければ、ファイルと行を名指しします）。

---

## ネイティブ版の画面（`studio\src\w2k`）

WebView2 は Windows 10 からしか動かないので、画面は Win32 と GDI で作りなおしています。
ここは**わざと 2 つに分けてあります**。

| ファイル | 何をするか | GDI |
|---|---|---|
| `blockdefs.h` / `blockdefs_gen.h` | ブロックの表（`ui\js\blocks.js` から `tools\gen-blockdefs.js` が作る） | 使わない |
| `layout.cpp` | 幅と高さを決める。どこを押したかを探す | **使わない** |
| `drag.cpp` | どこにつなげるか。つまむ・はなすと ghost.json がどう変わるか | **使わない** |
| `paint.cpp` | 決まったものを描く | 使う |

`layout.cpp` と `drag.cpp` に GDI が出てこないのは、**画面を出さずにテストするため**です。
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

## 栞は、C ランタイムを使わずに組んでいます

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
