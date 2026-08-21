/*
 * なしスタジオ - ネイティブ版の編集画面のテスト
 *
 * 画面まわりは、動かしてみないと分からないことばかりです。そこで window.cpp には
 * 「窓を出さずに、マウスの動きだけまねる」入口（ProbeEditor）を用意してあります。
 * ここは render_host.exe ごしに、それを呼びます。
 *
 *   --window   いまの画面ぜんぶを PNG に
 *   --palette  左のブロック置き場に、何がどこにあるか
 *   --drag     押して、動かして、はなす（あとの ghost.json が返ってきます）
 *
 * 場所は「たての位置」でえらんでいます。ブロックの高さは文字の幅に左右されないので、
 * どの環境でも同じところをつかめます（よこは文字の幅で変わるので、左のはしを押します）。
 */
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');

const root = path.resolve(__dirname, '..', '..');
const host = path.join(root, 'studio', 'test', 'render_host.exe');
const fixture = path.join(root, 'studio', 'test', 'drag_fixture.json');
const C = { red: '\x1b[31m', green: '\x1b[32m', dim: '\x1b[2m', off: '\x1b[0m' };

if (!fs.existsSync(host)) {
  console.error(`${C.red}[画面] render_host.exe がありません。`
    + ` 先に .\\build.ps1 -Test を実行してください。${C.off}`);
  process.exit(2);
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'nashi-win-'));
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

function run(args) {
  try {
    return execFileSync(host, args, { encoding: 'utf8', maxBuffer: 1 << 24 });
  } catch (e) {
    // 作りたての exe は、スマートアプリコントロールに止められることがあります
    // （docs\maintenance.md の「つまずきやすいところ」）。CI にはこの仕組みがありません。
    if (String(e.code) === 'UNKNOWN'
        || /Application Control|アプリケーション制御/.test(String(e.message))) {
      console.log(`${C.dim}  ――  render_host.exe を起動できませんでした`
        + `（スマートアプリコントロール）。画面のテストは飛ばします。${C.off}`);
      process.exit(0);
    }
    throw e;
  }
}

/** つまんで、動かして、はなす。返るのは、そのあとの ghost.json。 */
function drag(name, opt) {
  const png = path.join(tmp, name + '.png');
  const args = [fixture, '--drag', png,
    String(opt.from ? 0 : opt.x1), String(opt.from ? 0 : opt.y1),
    String(opt.x2), String(opt.y2)];
  if (opt.drop !== false) args.push('--drop');
  if (opt.from) args.push('--from', opt.from);
  const said = run(args);
  return { json: JSON.parse(said), png };
}

/** かたまりの中身を「type(文字)」でならべる。 */
function summary(script) {
  return script.blocks
    .map((b) => b.type + (b.text ? `(${b.text})` : ''))
    .join(' > ') || '(からっぽ)';
}

// 見本の置き場所（studio\test\drag_fixture.json）
//   d_cap  は (40, 40)   帽子 40〜88、say 88〜120、end 120〜152、say 152〜184
//   d_bool は (40, 300)
// 左のブロック置き場が 210px あるので、画面の x は 40 + 210 = 250 から。
const HAT = { x: 270, y: 60 };
const BLOCK0 = { x: 260, y: 100 };   // say（はじめ）
const BLOCK2 = { x: 260, y: 165 };   // say（ここには来ない）

// ------------------------------------------------------------ 1. 画面が出るか
console.log(`${C.dim}-- 画面が出るか${C.off}`);
{
  const png = path.join(tmp, 'window.png');
  const said = run([fixture, '--window', png, '900', '600']);
  check('画面を描けた', /画面\s+900 x 600/.test(said) ? 'はい' : said.trim(), 'はい');
  check('PNG が書けた', fs.existsSync(png) ? 'はい' : 'いいえ', 'はい');

  const buf = fs.readFileSync(png);
  check('PNG の印がある', buf.slice(0, 8).toString('hex'), '89504e470d0a1a0a');
  check('絵の幅', buf.readUInt32BE(16), 900);
  check('絵の高さ', buf.readUInt32BE(20), 600);
}

// ------------------------------------------------- 2. 左のブロック置き場
console.log(`${C.dim}-- 左のブロック置き場${C.off}`);
{
  const said = run([fixture, '--palette', 'x']);
  const keys = said.trim().split('\n').map((l) => l.trim().split(/\s+/)[0]);
  check('置き場所にブロックがならぶ', keys.length > 20 ? 'はい' : `${keys.length} 個`, 'はい');
  check('いちばん上は「〜されたとき」', keys[0], '@event');
  check('say も出ている', keys.includes('say') ? 'はい' : 'いいえ', 'はい');

  // たてに、重ならずにならんでいること
  const ys = said.trim().split('\n')
    .map((l) => Number((l.match(/\(\s*\d+,\s*(\d+)\)/) || [])[1]));
  let ordered = true;
  for (let i = 1; i < ys.length; i++) if (!(ys[i] > ys[i - 1])) ordered = false;
  check('上から順にならんでいる', ordered ? 'はい' : 'いいえ', 'はい');
}

// --------------------------------------------- 3. ブロックをつまんで動かす
console.log(`${C.dim}-- ブロックをつまんで動かす${C.off}`);
{
  const before = JSON.parse(fs.readFileSync(fixture, 'utf8'));
  check('はじめの中身', summary(before.scripts[0]),
    'say(はじめ) > end > say(ここには来ない)');

  // いちばん下の say を、いちばん上へ
  const r = drag('up', { x1: BLOCK2.x, y1: BLOCK2.y, x2: 260, y2: 101 });
  check('上へ動かせる', summary(r.json.scripts[0]),
    'say(ここには来ない) > say(はじめ) > end');

  // つまむと、その下にあるものもついてくる（scratch と同じ）
  const r2 = drag('all', { x1: BLOCK0.x, y1: BLOCK0.y, x2: 700, y2: 400 });
  check('いちばん上をつまむと、ぜんぶついてくる', summary(r2.json.scripts[0]), '(からっぽ)');
  check('どこにもつながらなければ、あたらしいかたまりになる',
    r2.json.scripts.length, before.scripts.length + 1);
  check('あたらしいかたまりの中身',
    summary(r2.json.scripts[r2.json.scripts.length - 1]),
    'say(はじめ) > end > say(ここには来ない)');
}

// ------------------------------------------------- 4. 置き場所へもどすと消える
console.log(`${C.dim}-- 置き場所へもどすと消える${C.off}`);
{
  const r = drag('trash', { x1: BLOCK0.x, y1: BLOCK0.y, x2: 100, y2: 300 });
  check('すてられる', summary(r.json.scripts[0]), '(からっぽ)');
  check('かたまりの数は変わらない', r.json.scripts.length, 2);
}

// --------------------------------------------------- 5. かたまりごと動かす
console.log(`${C.dim}-- かたまりごと動かす${C.off}`);
{
  const r = drag('move', { x1: HAT.x, y1: HAT.y, x2: HAT.x + 230, y2: HAT.y + 100 });
  check('帽子をつかむと、かたまりが動く',
    `${r.json.scripts[0].x},${r.json.scripts[0].y}`, '270,140');
  check('中身は変わらない', summary(r.json.scripts[0]),
    'say(はじめ) > end > say(ここには来ない)');
}

// ------------------------------------------- 6. 置き場所から持ってきて置く
console.log(`${C.dim}-- 置き場所から持ってきて置く${C.off}`);
{
  const r = drag('new', { from: 'newline', x2: 260, y2: 101 });
  check('あたらしいブロックがつながる', summary(r.json.scripts[0]),
    'newline > say(はじめ) > end > say(ここには来ない)');

  // 帽子を持ってくると、あたらしいかたまりができる
  const r2 = drag('newhat', { from: '@event', x2: 700, y2: 500 });
  check('帽子を持ってくると、かたまりがふえる', r2.json.scripts.length, 3);
  check('あたらしいかたまりは空', summary(r2.json.scripts[2]), '(からっぽ)');
  check('あたらしいかたまりの kind', r2.json.scripts[2].kind, 'event');
}

// -------------------------------- 7. 「ここでおわる」ブロックの後ろにはつなげない
console.log(`${C.dim}-- おわるブロックの決まりも守られるか${C.off}`);
{
  // end（120〜152）のすぐ下（＝152）をねらっても、そこには入れない。
  // いちばん近い置ける場所（end の前）に入るはずです。
  const r = drag('cap', { from: 'newline', x2: 260, y2: 153 });
  const got = summary(r.json.scripts[0]);
  check('おわるブロックの後ろには入らない',
    /end > newline/.test(got) ? 'うしろに入ってしまった' : 'はい', 'はい');
  check('そのかわり、前に入る', got, 'say(はじめ) > newline > end > say(ここには来ない)');
}

// ---------------------------------------------------------------------- 結果
try { fs.rmSync(tmp, { recursive: true, force: true }); } catch (e) { /* 消せなくても構わない */ }

console.log('');
if (bad) {
  console.log(`${C.red}[画面] ${bad} か所ちがいます。${C.off}`);
  process.exit(1);
}
console.log(`${C.green}[画面] ネイティブ版の編集画面は期待どおりです。${C.off}`);
