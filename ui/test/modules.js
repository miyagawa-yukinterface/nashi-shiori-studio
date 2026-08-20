/* 画面のコードが、読みこんだだけで壊れていないかを見る
 *
 *   node ui\test\modules.js
 *
 * ui/js/ は exe に埋め込まれるので、書きまちがえても
 * 「起動して、そのボタンを押すまで分からない」ことになります。
 * ここでは画面を出さずに、index.html と同じ順に全部読みこんで、
 *
 *   1. index.html と ui/js/ の中身が食いちがっていないか（足し忘れ・消し忘れ）
 *   2. 読みこむだけで落ちないか（構文まちがい・読みこみ時に触るもの）
 *   3. `N.Xxx.yyy(...)` と呼んでいる先が、ほんとうにあるか
 *
 * を見ます。3 は、ファイルを分けたときの**移し忘れ**を捕まえるためのものです。
 * 中身が正しいかまでは見ません（それは動かして確かめてください）。
 */
'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..', '..');
const jsDir = path.join(root, 'ui', 'js');
const C = { red: '\x1b[31m', green: '\x1b[32m', dim: '\x1b[2m', off: '\x1b[0m' };

const problems = [];
function bad(title, lines) { problems.push({ title, lines: lines || [] }); }

// ------------------------------------------------ 1. index.html と ui/js/ の照合
const html = fs.readFileSync(path.join(root, 'ui', 'index.html'), 'utf8');
const listed = [...html.matchAll(/<script src="js\/([^"]+)"><\/script>/g)].map((m) => m[1]);
const onDisk = fs.readdirSync(jsDir).filter((f) => f.endsWith('.js')).sort();

const missing = listed.filter((f) => !onDisk.includes(f));
if (missing.length) {
  bad('index.html が読もうとしているのに、ファイルがありません',
    missing.map((f) => `ui\\js\\${f} … 消したなら index.html の <script> も消してください`));
}
const unlisted = onDisk.filter((f) => !listed.includes(f));
if (unlisted.length) {
  bad('ui/js/ にあるのに、index.html が読んでいません',
    unlisted.map((f) => `ui\\js\\${f} … <script src="js/${f}"></script> を足してください`));
}

// ------------------------------------------------------------ 2. 読みこんでみる
// 読みこむだけなら DOM はほとんど要らない。触られたものだけ用意する。
const noop = () => {};
const elStub = new Proxy({}, {
  get: (t, k) => (k === 'style' || k === 'classList' || k === 'dataset' ? elStub : noop),
  set: () => true,
});
global.document = {
  addEventListener: noop,
  removeEventListener: noop,
  querySelector: () => null,
  querySelectorAll: () => [],
  createElement: () => elStub,
  body: elStub,
};
global.window = { NASHI: {}, addEventListener: noop, location: { href: '' } };
// navigator は node が持っていて書きかえられないので、足りないところだけ補う
if (!globalThis.navigator) global.navigator = { userAgent: 'node' };
global.fetch = () => Promise.reject(new Error('テストでは通信しません'));

for (const f of listed) {
  const p = path.join(jsDir, f);
  if (!fs.existsSync(p)) continue;
  try {
    (0, eval)(fs.readFileSync(p, 'utf8'));
  } catch (e) {
    bad(`ui\\js\\${f} を読みこめませんでした`, [String(e && e.message || e)]);
  }
}
const N = global.window.NASHI || {};

// -------------------------------------------- 3. N.Xxx.yyy(...) の行き先を確かめる
// 呼び出し（うしろに括弧があるもの）だけを見ます。あとから代入する値は見ません。
const CALL = /\b(?:N|window\.NASHI|NASHI)\.([A-Z][A-Za-z0-9_$]*)\.([A-Za-z_$][\w$]*)\s*\(/g;

/** 説明文に書いた「N.Shell.xxx() の形で呼びます」を拾わないよう、コメント行を空にする。
 *  行はずらさない（何行目かを言えるように、中身だけ消す）。 */
function withoutComments(src) {
  let inBlock = false;
  return src.split('\n').map((line) => {
    const t = line.trim();
    if (inBlock) {
      if (t.includes('*/')) inBlock = false;
      return '';
    }
    if (t.startsWith('//') || t.startsWith('*')) return '';
    if (t.startsWith('/*')) { if (!t.includes('*/')) inBlock = true; return ''; }
    return line;
  }).join('\n');
}

const dangling = [];
for (const f of listed) {
  const p = path.join(jsDir, f);
  if (!fs.existsSync(p)) continue;
  const src = withoutComments(fs.readFileSync(p, 'utf8'));
  for (const m of src.matchAll(CALL)) {
    const [, ns, member] = m;
    const line = src.slice(0, m.index).split('\n').length;
    if (!N[ns]) { dangling.push(`ui\\js\\${f}:${line}  N.${ns} がありません`); continue; }
    if (typeof N[ns][member] !== 'function') {
      dangling.push(`ui\\js\\${f}:${line}  N.${ns}.${member} がありません`
        + `（${ns} が公開しているのは: ${Object.keys(N[ns]).filter((k) => typeof N[ns][k] === 'function').join(', ') || 'なし'}）`);
    }
  }
}
if (dangling.length) bad('呼んでいる先がありません（分けたときの移し忘れ）', dangling);

// ---------------------------------------------------------------------- 表示
console.log('');
for (const p of problems) {
  console.log(`${C.red}  ちがう ${p.title}${C.off}`);
  for (const l of p.lines) console.log(`        ${l}`);
}
if (problems.length) {
  console.log('');
  console.log(`${C.red}[modules] ${problems.length} 件おかしいところがあります。${C.off}`);
  process.exit(1);
}

const spaces = Object.keys(N).filter((k) => N[k] && typeof N[k] === 'object' && !Array.isArray(N[k]));
console.log(`${C.green}  OK  ${C.off}${listed.length} ファイルを読みこみました `
  + `${C.dim}(${listed.join(' → ')})${C.off}`);
console.log(`${C.green}  OK  ${C.off}呼び出し先はすべてそろっています `
  + `${C.dim}(${spaces.join(' / ')})${C.off}`);
console.log(`${C.green}[modules] 画面のコードは、読みこみのかぎり問題ありません。${C.off}`);
