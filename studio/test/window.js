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
//   d_bool は (40, 300) / d_field は (40, 460) / d_talk は (600, 40)
// 左のブロック置き場が 210px あるので、画面の x は 40 + 210 = 250 から。
//
// つかむのは、ブロックの**左のはし**です。すこし右は欄になっていて、押すと
// 打ちこみ・えらびに入ります（それはそれで正しい動きです）。
const HAT = { x: 254, y: 60 };
const BLOCK0 = { x: 254, y: 100 };   // say（はじめ）
const BLOCK2 = { x: 254, y: 165 };   // say（ここには来ない）

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
  const r2 = drag('all', { x1: BLOCK0.x, y1: BLOCK0.y, x2: 760, y2: 700 });
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
  const before = JSON.parse(fs.readFileSync(fixture, 'utf8'));
  const r = drag('trash', { x1: BLOCK0.x, y1: BLOCK0.y, x2: 100, y2: 300 });
  check('すてられる', summary(r.json.scripts[0]), '(からっぽ)');
  check('かたまりの数は変わらない', r.json.scripts.length, before.scripts.length);
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
  const before = JSON.parse(fs.readFileSync(fixture, 'utf8'));
  const r2 = drag('newhat', { from: '@event', x2: 760, y2: 700 });
  check('帽子を持ってくると、かたまりがふえる',
    r2.json.scripts.length, before.scripts.length + 1);
  const added = r2.json.scripts[r2.json.scripts.length - 1];
  check('あたらしいかたまりは空', summary(added), '(からっぽ)');
  check('あたらしいかたまりの kind', added.kind, 'event');
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

// -------------------------------------------------- 8. 欄（打ちこみ・えらぶ）
console.log(`${C.dim}-- 欄（打ちこみ・えらぶ）${C.off}`);

/**
 * 画面に出ている欄をぜんぶ引く。
 * よこの位置は文字の幅で変わるので、決めうちにせず、ここから引きます。
 */
function fields() {
  return run([fixture, '--fields']).trim().split(/\r?\n/).map((line) => {
    const m = line.match(/^(\S+)\s+(\S+)\s+(\S+)\s+\(\s*(\d+),\s*(\d+)\)\s+(\d+)x(\d+)/);
    if (!m) return null;
    return {
      owner: m[1], arg: m[2], kind: m[3],
      x: Number(m[4]), y: Number(m[5]), w: Number(m[6]), h: Number(m[7]),
    };
  }).filter(Boolean);
}

function findField(owner, arg) {
  const f = fields().find((x) => x.owner === owner && x.arg === arg);
  if (!f) throw new Error(`${owner} の ${arg} という欄が見つかりません`);
  return f;
}

/** その欄の まんなか を押してみる。value を渡すと書きこみます。 */
function field(f, value) {
  const args = [fixture, '--field',
    String(f.x + Math.min(10, f.w >> 1)), String(f.y + (f.h >> 1))];
  if (value !== undefined) args.push(value);
  const said = run(args);
    const info = {};
  const choices = [];
  // 行の終わりの \r まで拾わないように、切りかたを決めておきます
  // （JavaScript の「.」は \r に当たらないので、$ で止めた形が合わなくなります）
  for (const line of said.split(/\r?\n/)) {
    const c = line.match(/^えらべる (.*) = (.*)$/);
    if (c) { choices.push([c[1], c[2]]); continue; }
    const m = line.match(/^(欄|やりかた|見出し|いま|どこ) (.*)$/);
    if (m) info[m[1]] = m[2];
  }
  info.choices = choices;
  const cut = said.indexOf('---- ghost.json');
  if (cut >= 0) info.json = JSON.parse(said.slice(cut + '---- ghost.json'.length));
  return info;
}

{
  const all = fields();
  check('欄がならぶ', all.length >= 14 ? 'はい' : `${all.length} 個`, 'はい');
  check('帽子の欄はかたまり自身のもの',
    all.filter((f) => f.owner === 'scripts[0]' && f.arg === 'event').length, 1);
  check('えらぶ欄と打ちこみ欄を見分ける',
    findField('scripts[0].blocks[0]', 'nl').kind + '/'
    + findField('scripts[0].blocks[0]', 'text').kind, 'dropdown/input');
}

// えらぶ欄
{
  const nl = field(findField('scripts[0].blocks[0]', 'nl'));
  check('えらぶ欄のいまの中身', nl['いま'], '1');
  check('えらぶ欄は見出しを出す', nl['見出し'], '改行する');
  check('えらべるものが出る',
    nl.choices.map(([l, v]) => `${l}=${v}`).join(' '), '改行する=1 改行しない=0');

  const nl2 = field(findField('scripts[0].blocks[0]', 'nl'), '0');
  check('えらぶと ghost.json が変わる', nl2.json.scripts[0].blocks[0].nl, '0');
}

// イベント名の欄
{
  const ev = field(findField('scripts[0]', 'event'));
  check('イベント名の欄', `${ev['やりかた']} ${ev['いま']}`, 'eventname OnBoot');
  check('イベントの見出しがつく',
    ev.choices.some(([l, v]) => v === 'OnBoot' && l === '起動したとき') ? 'はい' : 'いいえ', 'はい');
  check('「その他（自分で書く）」もある',
    ev.choices.some(([, v]) => v === '__custom__') ? 'はい' : 'いいえ', 'はい');
  check('えらべるイベントの数', ev.choices.length, 27);
}

// 変数の名前の欄（ghost.json に書いてある変数から作る）
{
  const v = field(findField('scripts[2].blocks[1]', 'name'));
  check('変数の欄', v['やりかた'], 'varname');
  check('その ghost の変数がならぶ',
    v.choices.map(([, val]) => val).join(','), 'カウンタ,きぶん');
}

// 打ちこみの欄
{
  const t = findField('scripts[0].blocks[0]', 'text');
  const r = field(t, 'こんばんは');
  check('打ちこむと ghost.json が変わる',
    r.json.scripts[0].blocks[0].text, 'こんばんは');

  // 数の欄は、数として入れる（文字にしない）
  const ms = findField('scripts[2].blocks[0]', 'ms');
  const r2 = field(ms, '250');
  check('数の欄は数のまま入る', typeof r2.json.scripts[2].blocks[0].ms, 'number');
  check('数の欄の中身', r2.json.scripts[2].blocks[0].ms, 250);

  const r3 = field(ms, 'すうじでない');
  check('数にならないものは文字のまま', typeof r3.json.scripts[2].blocks[0].ms, 'string');
}

// 欄でないところ
{
  const said = run([fixture, '--field', '900', '700']);
  check('何もないところ', said.trim(), 'そこには何もありません');
}

// 欄を押したときは、ブロックが動かないこと（押しわけができているか）
{
  const before = JSON.parse(fs.readFileSync(fixture, 'utf8'));
  const f = findField('scripts[0].blocks[0]', 'text');
  const r = drag('slotgrab', { x1: f.x + 5, y1: f.y + (f.h >> 1), x2: 760, y2: 700 });
  check('欄を押しても、ブロックは動かない',
    summary(r.json.scripts[0]), summary(before.scripts[0]));
  check('欄を押しても、かたまりはふえない',
    r.json.scripts.length, before.scripts.length);
}

// ---------------------------------------------------------------------- 結果
try { fs.rmSync(tmp, { recursive: true, force: true }); } catch (e) { /* 消せなくても構わない */ }

console.log('');
if (bad) {
  console.log(`${C.red}[画面] ${bad} か所ちがいます。${C.off}`);
  process.exit(1);
}
console.log(`${C.green}[画面] ネイティブ版の編集画面は期待どおりです。${C.off}`);
