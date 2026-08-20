/* 同じブロックを 32bit と 64bit の栞で動かして、出てきたさくらスクリプトを突き合わせる
 *
 *   node shiori\test\parity\parity.js
 *
 *   32bit の栞 … shiori/dist/test_host.exe        SSP に入るもの（nashi.dll）
 *   64bit の栞 … studio/test/preview_host.exe     なしスタジオの「ためす」が使うもの
 *
 * 中身は同じ shiori/src/interp.cpp です。**ビット幅だけが違います。**
 * ここがズレたら、interp.cpp のどこかが int や size_t の大きさに頼っています
 * （エディタで見た結果と、SSP に入れた結果が食いちがう、という形で表に出ます）。
 *
 * もともとは、同じ規則を JavaScript でもう一度書いた ui/js/sim.js との
 * 見くらべでした。スタジオが栞そのものを積んだので、JavaScript 版は消しました。
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

function fail(msg) {
  console.error(`${red}[parity] ${msg}${off}`);
  process.exit(2);
}

if (!cases.length) fail('ghost.json に OnP** のかたまりがありません。');
const noId = cases.filter((s) => !s.id);
if (noId.length) {
  fail(`id の無いかたまりがあります: ${noId.map((s) => s.event).join(', ')}`
    + '（64bit 側は id で選びます）');
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
  const a = shiori[i], b = studio[i];
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
    console.log(`        32bit の栞: ${around(a.value, k)}`);
    console.log(`        64bit の栞: ${around(b.value, k)}`);
  }
  if (a.commTo !== b.commTo) {
    console.log(`        話しかけ先  32bit: "${a.commTo}" / 64bit: "${b.commTo}"`);
  }
});

console.log('');
if (bad) {
  console.log(`${red}[parity] ${cases.length} 件中 ${bad} 件がズレています。${off}`);
  console.log(`${dim}        同じ interp.cpp なので、ビット幅に頼った書きかたを探してください`
    + `（int と size_t、ポインタの大きさ、double の丸め）。${off}`);
  process.exit(1);
}
console.log(`${green}[parity] ${cases.length} 件が、32bit と 64bit で一致しました。${off}`);
