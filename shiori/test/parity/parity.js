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
const host = require('../lib/host');

const here = __dirname;
const { red, green, dim, off } = host.COLOR;

const project = JSON.parse(fs.readFileSync(path.join(here, 'ghost.json'), 'utf8'));
const cases = (project.scripts || []).filter(
  (s) => s.kind === 'event' && /^OnP\d+$/.test(s.event || '')
);
if (!cases.length) {
  console.error(`${red}[parity] ghost.json に OnP** のかたまりがありません。${off}`);
  process.exit(2);
}

// ------------------------------------------------------------------ 栞 を動かす
let shiori;
try {
  shiori = host.run(here, cases.map((s) => {
    const refs = s.refs || [];
    return refs.length ? `${s.event}:${refs.join(',')}` : s.event;
  }));
} catch (e) {
  console.error(`${red}[parity] ${e.message}${off}`);
  process.exit(2);
}
if (shiori.length !== cases.length) {
  console.error(`${red}[parity] 栞の応答が ${shiori.length} 件で、`
    + `かたまりの数 ${cases.length} と合いません。${off}`);
  process.exit(2);
}

// ------------------------------------------------------------ プレビュー を動かす
global.window = {};
for (const f of ['blocks.js', 'sim.js']) {
  (0, eval)(fs.readFileSync(path.join(host.root, 'ui', 'js', f), 'utf8'));
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

/** 長い出力は、ちがいはじめたところの前後だけ見せる（画面が流れてしまわないように） */
function around(s, k) {
  const from = Math.max(0, k - 24);
  const to = Math.min(s.length, k + 40);
  return (from > 0 ? '…' : '') + s.slice(from, to)
    + (to < s.length ? `…（ぜんぶで ${s.length} 文字）` : '');
}

let bad = 0;
cases.forEach((s, i) => {
  const a = sim[i], b = shiori[i];
  const note = (s._ || '').trim();
  if (a.value === b.value && a.commTo === b.commTo) {
    console.log(`${green}  OK  ${off}${s.event}  ${dim}${note}${off}`);
    return;
  }
  bad++;
  console.log(`${red}  ちがう ${s.event}${off}  ${dim}${note}${off}`);
  if (a.value !== b.value) {
    let k = 0;
    while (k < a.value.length && k < b.value.length && a.value[k] === b.value[k]) k++;
    console.log(`        ${dim}${k} 文字目から違います${off}`);
    console.log(`        プレビュー: ${around(a.value, k)}`);
    console.log(`        栞        : ${around(b.value, k)}`);
  }
  if (a.commTo !== b.commTo) {
    console.log(`        話しかけ先  プレビュー: "${a.commTo}" / 栞: "${b.commTo}"`);
  }
});

console.log('');
if (bad) {
  console.log(`${red}[parity] ${cases.length} 件中 ${bad} 件がズレています。`
    + ` ui\\js\\sim.js と shiori\\src\\interp.cpp を見くらべてください。${off}`);
  process.exit(1);
}
console.log(`${green}[parity] ${cases.length} 件すべて一致しました。${off}`);
