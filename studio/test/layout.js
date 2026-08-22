/* ネイティブ版の画面 — ブロックの置き場所のテスト
 *
 *   node studio\test\layout.js
 *
 * WebView2 をやめて Win32 で描きなおす作業の土台です。
 * いまは CSS が担っている「どこに何を置くか」を、studio\src\w2k\layout.cpp が
 * 自分で計算します。そこには GDI が出てこないので、画面を出さずに確かめられます。
 *
 * 文字の幅は layout_host.exe が「半角7・全角14」と決め打ちにしているので、
 * どの環境でも同じ数が出ます（本物の画面では GDI で測ります）。
 */
'use strict';

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const root = path.resolve(__dirname, '..', '..');
const host = path.join(root, 'studio', 'test', 'layout_host.exe');
const C = { red: '\x1b[31m', green: '\x1b[32m', dim: '\x1b[2m', off: '\x1b[0m' };

if (!fs.existsSync(host)) {
  console.error(`${C.red}[配置] layout_host.exe がありません。`
    + ` 先に .\\build.ps1 -Test を実行してください。${C.off}`);
  process.exit(2);
}

function run(args) {
  return execFileSync(host, args, { encoding: 'utf8', maxBuffer: 1 << 24 });
}

let bad = 0;
function check(name, got, want) {
  const ok = String(got) === String(want);
  if (ok) console.log(`${C.green}  OK  ${C.off}${name}  ${C.dim}${got}${C.off}`);
  else {
    bad++;
    console.log(`${C.red}  ちがう ${name}${C.off}`);
    console.log(`        いま  : ${got}`);
    console.log(`        ほしい: ${want}`);
  }
}

// ------------------------------------------------ 1. 定義が blocks.js と合っているか
global.window = {};
(0, eval)(fs.readFileSync(path.join(root, 'tools', 'blocks.js'), 'utf8'));
const N = global.window.NASHI;

const defs = run(['--defs']);
check('カテゴリの数', (defs.match(/^== カテゴリ (\d+)/m) || [])[1], N.CATEGORIES.length);
check('ブロックの数', (defs.match(/^== ブロック (\d+)/m) || [])[1], Object.keys(N.BLOCKS).length);

// blocks.js のパレットにある key が、C++ 側にもあること
const paletteKeys = [];
for (const cat in N.PALETTE) {
  for (const item of N.PALETTE[cat]) if (typeof item === 'string' && item !== '@vars') paletteKeys.push(item);
}
const missing = paletteKeys.filter((k) => !new RegExp(`^  ${k.replace(/[#+*/%|]/g, (c) => '\\' + c)}\\s`, 'm').test(defs));
check('パレットのブロックが C++ にもある', missing.length ? missing.join(',') : 'ぜんぶある', 'ぜんぶある');

// ------------------------------------------------------------ 2. 代表的な寸法
const ghost = path.join(root, 'shiori', 'test', 'behavior', 'main', 'ghost.json');
const boot = run([ghost, 'b_boot']);

// 「さくら が おきた と話す（改行しない）」
check('say の大きさ', (boot.match(/ブロック \(\s*0,\s*48\)\s*(\d+x\d+)\s+say/) || [])[1], '348x32');
check('えらぶ欄は見出しを出す', /欄=nl 「改行しない」/.test(boot) ? 'はい' : 'いいえ', 'はい');
check('帽子はイベント名を出す', /欄=event 「OnBoot」/.test(boot) ? 'はい' : 'いいえ', 'はい');

// ---------------------------------------------- 3. C ブロックが中身を包むか
const parity = path.join(root, 'shiori', 'test', 'parity', 'ghost.json');
const p05 = run([parity, 'p05']);
const repeatW = Number((p05.match(/ブロック \([^)]*\)\s*(\d+)x\d+\s+repeat/) || [])[1]);
const innerW = Number((p05.match(/腕\s+\([^)]*\)\s*(\d+)x\d+/) || [])[1]);
check('repeat が中身より広い', repeatW > innerW ? 'はい' : `いいえ (${repeatW} <= ${innerW})`, 'はい');

// はめこんだ演算ブロックが、欄の中で場所を取っていること
check('cond の欄に演算が入っている', /欄=cond\s*$/m.test(p05) ? 'はい' : 'いいえ', 'はい');

// ------------------------------------------------------------ 4. 当たり判定
const hitSlot = run([ghost, 'b_boot', '--hit', '140', '60']);
check('欄をつかめる', /欄=text/.test(hitSlot) ? 'text の欄' : hitSlot.trim(), 'text の欄');
check('その欄を持つブロックが分かる', /ブロック: say/.test(hitSlot) ? 'say' : '?', 'say');

const hitHat = run([ghost, 'b_boot', '--hit', '5', '10']);
check('帽子をつかめる', /ブロック: @event/.test(hitHat) ? '@event' : '?', '@event');

const hitNone = run([ghost, 'b_boot', '--hit', '9000', '9000']);
check('外を押したら何もない', /には何もありません/.test(hitNone) ? 'はい' : 'いいえ', 'はい');

// -------------------------------------------------- 5. つまむ・つなぐ（drag）
// 決まりごとは ui\js\drag.js と同じにしてあります。
const drops = run([parity, '--drops', 'p05', 'stack']);

// ならびの数だけ、つなぎ目がある（帽子の下の 4 つ ＋ 最後の 1 つ）
check('外がわのつなぎ目の数',
  (drops.match(/^  ならび scripts\[4\]\.blocks {2}/gm) || []).length, 5);
// C ブロックの中にも、つなぎ目ができている
check('repeat の中にもつなげる',
  /ならび scripts\[4\]\.blocks\[0\]\.body\s+0 番目/.test(drops) ? 'はい' : 'いいえ', 'はい');
check('入れ子の中にもつなげる',
  /ならび scripts\[4\]\.blocks\[3\]\.body\[0\]\.body\s+0 番目/.test(drops) ? 'はい' : 'いいえ', 'はい');

// 丸いブロックは、空いている入力欄にだけ入る（えらぶ欄・ふさがった欄には入らない）
const dropsRep = run([parity, '--drops', 'p05', 'reporter']);
check('丸ブロックが入れる欄の数',
  (dropsRep.match(/^  欄 /gm) || []).length, 8);
check('えらぶ欄には入らない', /欄=nl|欄=who|欄=name/.test(dropsRep) ? 'いいえ' : 'はい', 'はい');
check('ふさがった欄には入らない', /欄=cond\s/.test(dropsRep) ? 'いいえ' : 'はい', 'はい');

// 六角のブロックは、空いている六角の欄にだけ入る。p05 の cond は埋まっているので 0 個。
const dropsBool = run([parity, '--drops', 'p05', 'boolean']);
check('六角のブロックが入れる欄の数', (dropsBool.match(/^  欄 /gm) || []).length, 0);

// いちばん近いつなぎ目をえらぶ
const near = run([parity, '--drag', 'p05', 'stack', '14', '82']);
check('ちょうど上に置いたら、その前につながる',
  /blocks\[0\]\.body\s+0 番目/.test(near) ? 'はい' : near.trim(), 'はい');
const near2 = run([parity, '--drag', 'p05', 'stack', '20', '95']);
check('下にずらしたら、その後ろにつながる',
  /blocks\[0\]\.body\s+1 番目/.test(near2) ? 'はい' : near2.trim(), 'はい');
const far = run([parity, '--drag', 'p05', 'stack', '900', '900']);
check('遠いところでは、つながらない', /つなげる場所なし/.test(far) ? 'はい' : far.trim(), 'はい');

// つまんで、はなす。つまむと下にあるものもついてくる（scratch と同じ）。
const moved = run([parity, '--move', 'p05', '1', '16', '84']);
check('つまむと下もついてくる',
  (moved.match(/^つまむ: (.*)$/m) || [])[1], 'set>while>repeat');
check('つまんだあと、もとの場所には上だけ残る',
  (moved.match(/^のこり: (.*)$/m) || [])[1], 'repeat');
check('はなした先に入っている',
  (moved.match(/^置いた先: \S+ = (.*)$/m) || [])[1], 'set>while>repeat>say');

// 「ここでおわる」ブロックのまわりの決まりは、専用の見本でたしかめます
// （parity の見本には cap のブロックが出てこないため）。
const fixture = path.join(root, 'studio', 'test', 'drag_fixture.json');
const capDrops = run([fixture, '--drops', 'd_cap', 'stack']);
check('おわるブロックの後ろにはつなげない',
  /^  ならび scripts\[0\]\.blocks\s+2 番目/m.test(capDrops) ? 'つなげてしまう' : 'はい', 'はい');
check('おわるブロックの前にはつなげる',
  /^  ならび scripts\[0\]\.blocks\s+1 番目/m.test(capDrops) ? 'はい' : 'いいえ', 'はい');

// つまんでいるものの最後が cap なら、その後ろにブロックを残せないので、ならびの終わりだけ
const capPayload = run([fixture, '--drops', 'd_cap', 'cap']);
check('おわるブロックはならびの終わりにしか置けない',
  (capPayload.match(/^  ならび /gm) || []).length, 1);

// 空の六角の欄には、六角のブロックだけが入る
const boolDrops = run([fixture, '--drops', 'd_bool', 'boolean']);
check('空の六角の欄が見つかる',
  /欄=cond\s+六角/.test(boolDrops) ? 'はい' : boolDrops.trim(), 'はい');
const repIntoBool = run([fixture, '--drops', 'd_bool', 'reporter']);
check('六角の欄に丸ブロックは入らない',
  /欄=cond/.test(repIntoBool) ? '入ってしまう' : 'はい', 'はい');

// ------------------------------------------------------------ 6. 描けるか
// 窓を出さずに、記憶の中の絵へ GDI で描いて PNG にします。
// 中身の見た目までは見ませんが、「落ちずに、それらしい大きさの絵が出る」までは見ます。
const renderHost = path.join(root, 'studio', 'test', 'render_host.exe');
if (fs.existsSync(renderHost)) {
  const outDir = path.join(root, 'studio', 'test', 'render_out');
  fs.mkdirSync(outDir, { recursive: true });
  const outPng = path.join(outDir, 'p05.png');
  if (fs.existsSync(outPng)) fs.unlinkSync(outPng);

  let ranOk = true;
  let said = '';
  try {
    said = execFileSync(renderHost, [parity, 'p05', outPng],
      { encoding: 'utf8', maxBuffer: 1 << 24 });
  } catch (e) {
    // 作りたての exe は、スマートアプリコントロールに止められることがあります
    // （docs\maintenance.md の「つまずきやすいところ」）。中身の問題ではないので、
    // そのときだけは飛ばします。CI にはこの仕組みが無いので、向こうでは必ず走ります。
    if (String(e.code) === 'UNKNOWN' || /Application Control|アプリケーション制御/.test(String(e.message))) {
      console.log(`${C.dim}  ――  render_host.exe を起動できませんでした`
        + `（スマートアプリコントロール）。描画の確かめは飛ばします。${C.off}`);
      ranOk = false;
    } else {
      throw e;
    }
  }
  if (ranOk) check('絵が書き出せた', fs.existsSync(outPng) ? 'はい' : said.trim(), 'はい');

  if (ranOk && fs.existsSync(outPng)) {
    const buf = fs.readFileSync(outPng);
    const sig = buf.slice(0, 8).toString('hex');
    check('PNG の印がある', sig, '89504e470d0a1a0a');
    // IHDR の幅と高さ
    const pw = buf.readUInt32BE(16), ph = buf.readUInt32BE(20);
    check('絵の幅がそれらしい', pw > 200 && pw < 900 ? 'はい' : String(pw), 'はい');
    check('絵の高さがそれらしい', ph > 200 && ph < 900 ? 'はい' : String(ph), 'はい');
  }
} else {
  console.log(`${C.dim}  ――  render_host.exe が無いので、描画は飛ばします${C.off}`);
}

// ---------------------------------------------------------------------- 結果
console.log('');
if (bad) {
  console.log(`${C.red}[配置] ${bad} か所ちがいます。${C.off}`);
  process.exit(1);
}
console.log(`${C.green}[配置] ブロックの置き場所は期待どおりです。${C.off}`);
