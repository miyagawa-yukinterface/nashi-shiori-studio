# 手入れの手引き

しばらく触っていないこのリポジトリに戻ってきたとき用の覚え書きです。
「どこに何があるか」は README の**フォルダ構成**にあります。
ここに書くのは、**書いてある場所が離れていて、片方だけ直すと静かに壊れる決まり**のほうです。

---

## まず、これを走らせる

```powershell
node tools\check-blocks.js     # 1 秒。ビルド不要。ブロックがそろっているか
.\build.ps1 -Test              # 3 分ほど。ビルドしてテスト 5 本
```

`check-blocks.js` はビルドが要らないので、**手を入れたら、まずこれ**です。
`build.ps1 -Test` の中でも、ビルドより先に走ります（書き忘れが 2 分待たずに分かります）。

---

## この作りの、いちばん危ないところ

**ブロックを実行する規則が、2 つの言語で二重に書いてあります。**

| | |
|---|---|
| `ui/js/sim.js` | エディタの「ためす」で動くプレビュー（JavaScript） |
| `shiori/src/interp.cpp` | SSP に入れたとき実際に動く本番（C++） |

同じ答えを出さなければいけないのに、コンパイラも型も別です。
**片方だけ直しても、何のエラーも出ません。** エディタで思ったとおりに動いたのに
SSP では違う、という形で、あとから分かります。

これを見はっているのが次の 2 つです。両方とも `build.ps1 -Test` で走ります。

* **一致テスト**（`shiori/test/parity`）… 同じ `ghost.json` を両方に流して、出てきた
  さくらスクリプトが 1 文字でも違えば止めます。
* **そろい点検**（`tools/check-blocks.js`）… 一致テストは「**書いた分**しか見ません」。
  ブロックを足して一致テストの材料に入れ忘れると、誰もためさないので緑のままです。
  そこを機械的に見ます。

書きうつすときの決まりごとは、**両方のファイルにコメントで残してあります**
（`Emit` / `emit`、`CapText` / `capText`、`TagArg` / `safeTag`、`EmitScope` / `emitScope`）。
片方を直すときは、相方のコメントも探してください。

---

## ブロックを 1 つ足す

順番に、6 か所です。`node tools\check-blocks.js` が、抜けたところを名指しで教えます。

1. **`ui/js/blocks.js`** — `def({ type: '…', cat: '…', shape: '…', spec: '…', args: {…} })`。
   ここが**すべての正**です。そして `N.PALETTE` の、置きたいカテゴリに名前を足します
   （足さないと、定義してもパレットに並びません）。
2. **`ui/js/sim.js`** — `runBlock` の `switch` に `case '…':`。
   文字を出すときは `ctx.out +=` ではなく **`emit(ctx, …)`** を通してください（長さの上限がかかります）。
3. **`shiori/src/interp.cpp`** — `RunBlock` に `if (type == "…")`。同じく **`Emit(ctx, …)`** を通します。
4. **`docs/blocks.md`** — 表に 1 行。`{"type":"…"}` の形と、出るさくらスクリプトを書きます。
5. **`shiori/test/parity/ghost.json`** — `OnP**` の帽子で 1 つ。
   **毎回おなじ結果になる形**にしてください（乱数や時計を使うと、一致テストが毎回ちがう答えになります）。
6. **`ui/js/lint.js`** — 「空だと困る欄」があるブロックだけ。チェックタブが注意を出します。

`ui/` を直したら、**`studio\build.ps1` で exe を作りなおします**（UI は exe に埋め込まれています）。

### 情報ブロック（`sys`）を足すとき

`blocks.js` の `sys` の `options` ／ `sim.js` の `sysValue` ／ `interp.cpp` の `SysValue`、
の 3 か所です。時計まわりの値は毎回ちがう答えになるので、一致テストには乗せられません
（`check-blocks.js` が「テストで動かしていない情報ブロック」として、お知らせに並べます）。

### わざと片方だけにするとき

手書きの `ghost.json` むけに、パレットには出さない型や演算があります
（`stop`、`arith` の `min` / `max` など）。`tools/check-blocks.js` の **`KNOWN`** に、
**なぜそうなのかを書いて**足してください。書いておかないと、次に来たとき
「消し忘れ」なのか「わざと」なのか分かりません。

---

## テスト 5 本と、それぞれが守っているもの

| 走らせかた | 守っているもの |
|---|---|
| `node tools\check-blocks.js` | ブロックが 6 か所にそろっているか。上限の数が 2 つの実装で同じか |
| `node shiori\test\parity\parity.js` | プレビューと栞の**出力が同じ**か |
| `node shiori\test\behavior\behavior.js` | 栞**だけ**が持っている判断（どれを選ぶか・既定の反応・間引き・通信・SAORI） |
| `node studio\test\export.js` | 書き出したファイルの中身（バイトで見くらべ） |
| `node ui\test\editor.js` | 読みこみの整形（`model.js`）とチェックタブ（`lint.js`） |

`parity` と `behavior` は `shiori\dist\test_host.exe` を使うので、**先にビルドが要ります**
（`.\build.ps1 -Test`）。`check-blocks` と `editor` はビルド無しで走ります。

書き出しかたをわざと変えたときは、出てきたものを**目で確かめてから**
`node studio\test\export.js --update` で期待値を作りなおします。

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

**`ui/` を直したら exe を作りなおす。** ブラウザで `ui\index.html` を直接開いても
プレビューは動きますが、保存や書き出しは exe の中の API を呼ぶので動きません。

---

## 版を上げて配る

1. `VERSION`（ルートの 1 行）を直す。zip の名前と exe のプロパティは、ここだけを見ています。
2. `CHANGELOG.md` に、変わったことを足す。
3. `.\build.ps1 -Release` — きれいに作り直し、テスト 5 本を通し、`dist\nashi-studio-<版>.zip` を作ります。
4. `git tag -a v<版> -m "…"` して `git push origin v<版>`。
5. GitHub の Releases で、そのタグからリリースを作り、zip を添える
   （`gh` があれば `gh release create v<版> dist\nashi-studio-<版>.zip -F CHANGELOG.md`）。

---

## 迷ったら

* ブロックの意味 → `docs/blocks.md`
* `ghost.json` の形 → `docs/ghost-json.md`
* つかいかた・フォルダ構成・栞のしくみ → `README.md`
* さくらスクリプトや SHIORI の仕様そのもの → UKADOC（伺か関連ドキュメント）
