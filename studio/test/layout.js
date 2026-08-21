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
(0, eval)(fs.readFileSync(path.join(root, 'ui', 'js', 'blocks.js'), 'utf8'));
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

// ------------------------------------------------------------ 5. 描けるか
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
