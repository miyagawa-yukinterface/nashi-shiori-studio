/* プレビュー(ui/js/sim.js) と 栞(shiori/src/interp.cpp) の出力を突き合わせる
 *
 *   node shiori\test\parity\parity.js
 *
 * 同じ ghost.json を両方に流して、出てきたさくらスクリプトを見くらべます。
 * ズレていたら、どのイベントのどこが違うかを出して、終了コード 1 で終わります。
 *
 * この 2 つは同じ規則を二重に実装しているので、ブロックを足すと片方だけ直して
 * もう片方を忘れる、ということが起きます。それを見つけるためのものです。
 * 新しいブロックを足したら、ghost.json にも「毎回おなじ結果になる形」で足してください。
 */
'use strict';

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const here = __dirname;
const root = path.resolve(here, '..', '..', '..');
const dllSrc = path.join(root, 'shiori', 'dist', 'nashi.dll');
const testHost = path.join(root, 'shiori', 'dist', 'test_host.exe');
const fixture = path.join(here, 'ghost.json');

const RED = '\x1b[31m', GREEN = '\x1b[32m', DIM = '\x1b[2m', OFF = '\x1b[0m';

function die(msg) {
  console.error(`${RED}[parity] ${msg}${OFF}`);
  process.exit(2);
}

// ------------------------------------------------------------------ したごしらえ
if (!fs.existsSync(testHost)) {
  die(`test_host.exe がありません。先に .\\build.ps1 -Test を実行してください。\n         ${testHost}`);
}
if (!fs.existsSync(dllSrc)) die(`nashi.dll がありません: ${dllSrc}`);

// 栞は自分と同じフォルダの ghost.json を読むので、DLL をここへ持ってくる
fs.copyFileSync(dllSrc, path.join(here, 'nashi.dll'));
// 前回の変数の保存が残っていると結果が変わるので消す
const savePath = path.join(here, 'nashi_save.json');
if (fs.existsSync(savePath)) fs.unlinkSync(savePath);

const project = JSON.parse(fs.readFileSync(fixture, 'utf8'));
const cases = (project.scripts || []).filter(
  (s) => s.kind === 'event' && /^OnP\d+$/.test(s.event || '')
);
if (!cases.length) die('ghost.json に OnP** のかたまりがありません。');

// ------------------------------------------------------------------ 栞 を動かす
const specs = cases.map((s) => {
  const refs = s.refs || [];
  return refs.length ? `${s.event}:${refs.join(',')}` : s.event;
});
for (const spec of specs) {
  if (spec.includes(' ')) die(`Reference に空白は使えません（test_host の都合）: ${spec}`);
}

let raw;
try {
  raw = execFileSync(testHost, [here, ...specs], { encoding: 'utf8', maxBuffer: 1 << 24 });
} catch (e) {
  die(`test_host.exe の実行に失敗しました: ${e.message}`);
}

// "---- OnP01" のかたまりごとに、Value: と Reference0: を取り出す
const shiori = [];
let cur = null;
for (const rawLine of raw.split('\n')) {
  // 応答そのものが \r\n を持っていて、それを printf がテキストモードで書くので
  // 行末が \r\r\n になる。まとめて落とす。
  const line = rawLine.replace(/\r+$/, '');
  if (line.startsWith('---- ')) {
    cur = { value: '', commTo: '' };
    shiori.push(cur);
    continue;
  }
  if (!cur) continue;
  if (line.startsWith('Value: ')) cur.value = line.slice(7);
  else if (line.startsWith('Reference0: ')) cur.commTo = line.slice(12);
}
if (shiori.length !== cases.length) {
  die(`栞の応答が ${shiori.length} 件で、かたまりの数 ${cases.length} と合いません。`);
}

// ------------------------------------------------------------ プレビュー を動かす
global.window = {};
for (const f of ['blocks.js', 'sim.js']) {
  (0, eval)(fs.readFileSync(path.join(root, 'ui', 'js', f), 'utf8'));
}
const Sim = global.window.NASHI.Sim;

// 栞は 1 つのプロセスの中で変数を持ちこすので、こちらも持ちこす。
// 起動していない状態にそろえる（栞は OnBoot が来るまで名前も回数も空）。
const vars = {};
const sys = { uptime: 0, boots: 0, talks: 0, ghostName: '', shellName: '' };

const sim = cases.map((s) => {
  const res = Sim.runScript(project, s, { vars, refs: s.refs || [], sys });
  Object.assign(vars, res.vars);
  return { value: res.script || '', commTo: res.commTo || '' };
});

// ---------------------------------------------------------------------- 見くらべ
let bad = 0;
cases.forEach((s, i) => {
  const a = sim[i], b = shiori[i];
  const note = (s._ || '').trim();
  if (a.value === b.value && a.commTo === b.commTo) {
    console.log(`${GREEN}  OK  ${OFF}${s.event}  ${DIM}${note}${OFF}`);
    return;
  }
  bad++;
  console.log(`${RED}  ちがう ${s.event}${OFF}  ${DIM}${note}${OFF}`);
  if (a.value !== b.value) {
    console.log(`        プレビュー: ${a.value}`);
    console.log(`        栞        : ${b.value}`);
    // 最初に食い違った場所を指す
    let k = 0;
    while (k < a.value.length && k < b.value.length && a.value[k] === b.value[k]) k++;
    console.log(`        ${DIM}${k} 文字目から違います`
      + ` (プレビュー "${a.value.slice(k, k + 12)}" / 栞 "${b.value.slice(k, k + 12)}")${OFF}`);
  }
  if (a.commTo !== b.commTo) {
    console.log(`        話しかけ先  プレビュー: "${a.commTo}" / 栞: "${b.commTo}"`);
  }
});

console.log('');
if (bad) {
  console.log(`${RED}[parity] ${cases.length} 件中 ${bad} 件がズレています。`
    + ` ui\\js\\sim.js と shiori\\src\\interp.cpp を見くらべてください。${OFF}`);
  process.exit(1);
}
console.log(`${GREEN}[parity] ${cases.length} 件すべて一致しました。${OFF}`);
