/* 同じブロックを 3 つのやりかたで動かして、出てきたさくらスクリプトを突き合わせる
 *
 *   node shiori\test\parity\parity.js
 *
 *   プレビュー … ui/js/sim.js（JavaScript）              ← いずれ消します
 *   32bit の栞 … shiori/dist/test_host.exe               ← SSP に入るもの
 *   64bit の栞 … studio/test/preview_host.exe            ← スタジオの「ためす」が使うもの
 *
 * 「プレビュー」は同じ規則を JavaScript でもう一度書いたものなので、片方だけ直すと
 * 静かにズレます。それを見つけるのが、このテストのもともとの役目でした。
 *
 * いまはスタジオも栞そのものでブロックを動かせるので、ここが緑であることは
 * **JavaScript 版を消してよい**という証明にもなっています。消したあとは
 * 32bit と 64bit の見くらべだけが残り、ビット幅で結果が変わる類のズレを見つけます。
 *
 * 新しいブロックを足したら、ghost.json にも「毎回おなじ結果になる形」で足してください
 * （足し忘れは node tools\check-blocks.js が止めます）。
 */
'use strict';

const fs = require('fs');
const path = require('path');
const host = require('../lib/host');

const here = __dirname;
const projectPath = path.join(here, 'ghost.json');
const { red, green, dim, off } = host.COLOR;

const project = JSON.parse(fs.readFileSync(projectPath, 'utf8'));
const cases = (project.scripts || []).filter(
  (s) => s.kind === 'event' && /^OnP\d+$/.test(s.event || '')
);
if (!cases.length) {
  console.error(`${red}[parity] ghost.json に OnP** のかたまりがありません。${off}`);
  process.exit(2);
}
const noId = cases.filter((s) => !s.id);
if (noId.length) {
  console.error(`${red}[parity] id の無いかたまりがあります: `
    + `${noId.map((s) => s.event).join(', ')}（64bit 側は id で選びます）${off}`);
  process.exit(2);
}

function fail(msg) {
  console.error(`${red}[parity] ${msg}${off}`);
  process.exit(2);
}

// ------------------------------------------------------------ 32bit の栞 を動かす
let shiori;
try {
  shiori = host.run(here, cases.map((s) => {
    const refs = s.refs || [];
    return refs.length ? `${s.event}:${refs.join(',')}` : s.event;
  }));
} catch (e) {
  fail(e.message);
}
if (shiori.length !== cases.length) {
  fail(`32bit の栞の応答が ${shiori.length} 件で、かたまりの数 ${cases.length} と合いません。`);
}

// ------------------------------------------------------------ 64bit の栞 を動かす
let studio;
try {
  studio = host.runPreview(projectPath, cases.map((s) => {
    const refs = s.refs || [];
    return refs.length ? `${s.id}:${refs.join(',')}` : s.id;
  }));
} catch (e) {
  fail(e.message);
}
if (studio.length !== cases.length) {
  fail(`64bit の栞の応答が ${studio.length} 件で、かたまりの数 ${cases.length} と合いません。`);
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

/** 最初にちがう位置 */
function firstDiff(a, b) {
  let k = 0;
  while (k < a.length && k < b.length && a[k] === b[k]) k++;
  return k;
}

const WAYS = [
  ['プレビュー', (i) => sim[i]],
  ['32bit の栞', (i) => shiori[i]],
  ['64bit の栞', (i) => studio[i]],
];

let bad = 0;
cases.forEach((s, i) => {
  const got = WAYS.map(([name, pick]) => [name, pick(i)]);
  const [, base] = got[0];
  const note = (s._ || '').trim();

  const off1 = got.filter(([, r]) => r.value !== base.value || r.commTo !== base.commTo);
  if (!off1.length) {
    console.log(`${green}  OK  ${off}${s.event}  ${dim}${note}${off}`);
    return;
  }

  bad++;
  console.log(`${red}  ちがう ${s.event}${off}  ${dim}${note}${off}`);
  const k = Math.min(...off1.map(([, r]) => firstDiff(base.value, r.value)));
  console.log(`        ${dim}${k} 文字目から違います${off}`);
  for (const [name, r] of got) {
    console.log(`        ${name}: ${around(r.value, k)}`);
  }
  const tos = got.map(([, r]) => r.commTo);
  if (tos.some((t) => t !== tos[0])) {
    console.log(`        話しかけ先  ${got.map(([n, r]) => `${n}:"${r.commTo}"`).join(' / ')}`);
  }
});

console.log('');
if (bad) {
  console.log(`${red}[parity] ${cases.length} 件中 ${bad} 件がズレています。${off}`);
  console.log(`${dim}        プレビューだけ違う → ui\\js\\sim.js を interp.cpp に合わせる${off}`);
  console.log(`${dim}        32bit と 64bit で違う → interp.cpp に、ビット幅に頼った書きかたがある${off}`);
  process.exit(1);
}
console.log(`${green}[parity] ${cases.length} 件が、3 つのやりかたで一致しました。${off}`);
