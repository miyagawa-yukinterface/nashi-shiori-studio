/* ブロックの「そろい」を見はる道具
 *
 *   node tools\check-blocks.js
 *
 * ブロックを 1 つ足すときは、いくつものファイルに同じことを書きます。
 * どれか 1 つ忘れても、ふつうのテストは緑のまま通ってしまいます
 * （書き忘れたブロックは、誰もためしていないので、誰も気づきません）。
 *
 * この道具は ui\js\blocks.js を「正」として、ほかのファイルがついてきているかを
 * 機械的に見ます。ビルドは要らないので、ブロックを足したら、まずこれを走らせてください。
 * くわしい手順は docs\maintenance.md にあります。
 *
 * ブロックを動かす規則は shiori/src/interp.cpp の 1 か所だけです
 * （画面のプレビューも、そこを呼んでいます）。ここで見るのは「書き忘れ」だけで、
 * 中身が合っているかは一致テストとふるまいテストが見ます。
 */
'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const C = { red: '\x1b[31m', green: '\x1b[32m', yellow: '\x1b[33m', dim: '\x1b[2m', off: '\x1b[0m' };

const CPP = 'shiori\\src\\interp.cpp';
const DOC = 'docs\\blocks.md';
const DEFS = 'ui\\js\\blocks.js';

// ------------------------------------------------------------------ 承知ずみ
// 「そろっていない」けれど、そういうものだと決めたもの。
// ここに足すときは、なぜそうなのかを必ず書いてください。
const KNOWN = {
  // 手書きの ghost.json だけで使える type（エディタのパレットには出していない）
  types: {
    num: 'ブロックではなく、数そのもの（EvalExpr / evalExpr が直に読む）',
    text: 'ブロックではなく、文字そのもの',
    bool: 'ブロックではなく、はい／いいえそのもの',
    stop: '「ここでトークをおわる」(end) があるので、パレットには出していない',
  },
  // 実装だけが知っている演算。手書きの ghost.json むけの別名・おまけ。
  ops: {
    'arith#min': 'おまけ（パレットには出していない）',
    'arith#max': 'おまけ（パレットには出していない）',
    'compare#==': '= の別名',
    'compare#<>': '!= の別名',
    'compare#<=': 'パレットには出していないが、手書きなら使える',
    'compare#>=': 'パレットには出していないが、手書きなら使える',
  },
  // 「その他」の分岐で処理していて、名前が出てこないもの
  implicit: {
    'logic#or': 'and でなければ or、と書いてある',
    'round#round': '四捨五入が既定なので、名前を見ていない',
  },
};

// パレットの目じるしが、実際にはどのブロックを並べるか（render.js が展開します）
const PALETTE_EXPANDS = { '@vars': ['var'] };

// ------------------------------------------------------------------- 読みこみ
function read(rel) {
  return fs.readFileSync(path.join(root, rel.replace(/\\/g, path.sep)), 'utf8');
}

const src = { cpp: read(CPP), doc: read(DOC) };

global.window = {};
(0, eval)(read(DEFS));
const N = global.window.NASHI;
if (!N || !N.BLOCKS) {
  console.error(`${C.red}[そろい] ${DEFS} を読めませんでした。${C.off}`);
  process.exit(2);
}

// ------------------------------------------------------------------ 見つけかた
// どのファイルでも「名前を書いた 1 行」を探すだけにしています。
// 中身の書きかたを変えても壊れないように、関数の場所は当てにしません。
const has = {
  cppType: (name) => src.cpp.includes(`type == "${name}"`),
  cppKey: (name) => src.cpp.includes(`key == "${name}"`),
  cppOp: (name) => src.cpp.includes(`op == "${name}"`),
  doc: (name) => src.doc.includes(`"type":"${name}"`) || src.doc.includes(`"type": "${name}"`),
};

// ------------------------------------------------- テストのゴーストが使っている型
const FIXTURES = [
  'shiori\\test\\parity\\ghost.json',
  'shiori\\test\\behavior\\main\\ghost.json',
  'shiori\\test\\behavior\\empty\\ghost.json',
];

const tested = { types: new Set(), ops: new Set(), keys: new Set() };
for (const rel of FIXTURES) {
  let data;
  try {
    data = JSON.parse(read(rel));
  } catch (e) {
    console.error(`${C.red}[そろい] ${rel} が JSON として読めません: ${e.message}${C.off}`);
    process.exit(2);
  }
  (function walk(v) {
    if (Array.isArray(v)) { v.forEach(walk); return; }
    if (!v || typeof v !== 'object') return;
    if (typeof v.type === 'string') {
      tested.types.add(v.type);
      if (typeof v.op === 'string') tested.ops.add(v.type + '#' + v.op);
      if (v.type === 'sys' && typeof v.key === 'string') tested.keys.add(v.key);
    }
    for (const k in v) walk(v[k]);
  })(data);
}

// ------------------------------------------------------------------- 結果ため
const problems = [];
const notes = [];
function bad(title, lines) { problems.push({ title, lines }); }

/** えらべる値の一覧を取り出す。取り出せなければ、どこが変わったのか言って止まる。 */
function options(defKey, argName) {
  const list = (((N.BLOCKS[defKey] || {}).args || {})[argName] || {}).options;
  if (!Array.isArray(list)) {
    console.error(`${C.red}[そろい] ${DEFS} の ${defKey} に ${argName} の options がありません。`
      + ` 形を変えたなら tools\\check-blocks.js も直してください。${C.off}`);
    process.exit(2);
  }
  return list;
}

// ============================================================ 1. ブロックの型
const defs = N.BLOCKS;
const blockTypes = [...new Set(
  Object.values(defs).map((d) => d.type).filter((t) => t && !t.startsWith('@'))
)].sort();

for (const t of blockTypes) {
  const miss = [];
  if (!has.cppType(t)) miss.push(`${CPP} に  type == "${t}"  がありません（動きません）`);
  if (!has.doc(t)) miss.push(`${DOC} に  "type":"${t}"  の行がありません`);
  if (!tested.types.has(t)) {
    miss.push(`どのテストのゴーストにも出てきません`
      + `（shiori\\test\\parity\\ghost.json に「毎回おなじ結果になる形」で足してください）`);
  }
  if (miss.length) bad(`ブロック「${t}」がそろっていません`, miss);
}

// 逆むき: 実装にあって、ブロック定義に無いもの。
// 演算の名前（min など）と情報の key は、それぞれ別の見出しで見ているので外します。
const implTypes = new Set();
for (const m of src.cpp.matchAll(/type == "([a-z_][a-z0-9_]+)"/g)) implTypes.add(m[1]);
const implKeys = new Set();
for (const m of src.cpp.matchAll(/key == "([a-z_][a-z0-9_]*)"/g)) implKeys.add(m[1]);
const implOps = new Set();
for (const m of src.cpp.matchAll(/op == "([a-z_][a-z0-9_]*)"/g)) implOps.add(m[1]);
const strays = [...implTypes].filter(
  (t) => !blockTypes.includes(t) && !implKeys.has(t) && !implOps.has(t) && !KNOWN.types[t]
).sort();
if (strays.length) {
  bad('実装にあるのに、ブロック定義がありません', strays.map(
    (t) => `"${t}" … ${DEFS} に def({ type: '${t}', … }) が無いので、エディタから置けません`
      + `（わざとなら tools\\check-blocks.js の KNOWN.types に理由つきで足してください）`
  ));
}

// ==================================================== 2. 情報ブロックの key
const sysKeys = options('sys', 'key').map((o) => o[1]);
for (const k of sysKeys) {
  if (!has.cppKey(k)) {
    bad(`情報ブロックの「${k}」がそろっていません`,
      [`${CPP} の SysValue に  key == "${k}"  がありません`]);
  }
}

// 逆むき: 両方が知っているのに、えらべない key
const hiddenKeys = [...implKeys].filter((k) => !sysKeys.includes(k)).sort();
if (hiddenKeys.length) {
  bad('栞は知っているのに、エディタでえらべない情報があります', hiddenKeys.map(
    (k) => `"${k}" … ${DEFS} の sys ブロックの options に足してください`
  ));
}

// ============================================================== 3. 演算の種類
const opKeys = Object.keys(defs).filter((k) => k.includes('#'));
for (const key of opKeys) {
  const [type, op] = key.split('#');
  if (KNOWN.implicit[key]) continue;
  if (!has.cppOp(op)) {
    bad(`演算「${key}」がそろっていません`, [`${CPP} の ${type} に  op == "${op}"  がありません`]);
  }
}
// dropdown で選ぶ op（round）も同じように見る
for (const o of options('round', 'op')) {
  const op = o[1];
  const key = 'round#' + op;
  if (KNOWN.implicit[key]) continue;
  if (!has.cppOp(op)) {
    bad(`演算「${key}」がそろっていません`, [`${CPP} の round に  op == "${op}"  がありません`]);
  }
}

// ============================================================== 4. パレット
const palette = new Set();
for (const cat in N.PALETTE) {
  for (const item of N.PALETTE[cat]) {
    if (typeof item !== 'string') continue;
    if (PALETTE_EXPANDS[item]) { PALETTE_EXPANDS[item].forEach((k) => palette.add(k)); continue; }
    palette.add(item);
  }
}
const paletteMiss = [...palette].filter((k) => !defs[k]).sort();
if (paletteMiss.length) {
  bad('パレットに、定義の無いブロックが並んでいます', paletteMiss.map(
    (k) => `"${k}" … ${DEFS} の N.PALETTE から消すか、def({ … }) を足してください`
  ));
}
const notInPalette = Object.keys(defs).filter(
  (k) => !palette.has(k) && !defs[k].hat && !KNOWN.ops[defs[k].type + '#' + (defs[k].fixed || {}).op]
).sort();
if (notInPalette.length) {
  bad('定義したのに、パレットに出していないブロックがあります', notInPalette.map(
    (k) => `"${k}" … ${DEFS} の N.PALETTE に足してください（置けないので誰にも使えません）`
  ));
}

// ==================================================== 5. イベント名のつじつま
const eventNames = new Set(N.EVENTS.map((e) => e[1]).filter((e) => e !== '__custom__'));
const refOnly = Object.keys(N.EVENT_REFS).filter(
  (e) => !eventNames.has(e) && !e.includes('.')
).sort();
if (refOnly.length) {
  bad('EVENT_REFS にあるのに、えらべないイベントがあります', refOnly.map(
    (e) => `"${e}" … ${DEFS} の N.EVENTS に足すか、N.EVENT_REFS から消してください`
  ));
}

// ------------------------------------------------------------------ お知らせ
// 止めはしないけれど、知っておくと良いこと。
const untestedKeys = sysKeys.filter((k) => !tested.keys.has(k));
if (untestedKeys.length) {
  notes.push(`テストで動かしていない情報ブロック: ${untestedKeys.join(' / ')}`
    + `（時計まわりは毎回ちがう答えになるので、一致テストには乗せられません）`);
}
const untestedOps = opKeys.filter((k) => !tested.ops.has(k));
if (untestedOps.length) {
  notes.push(`テストで動かしていない演算: ${untestedOps.join(' / ')}`);
}

// ---------------------------------------------------------------------- 表示
console.log('');
for (const p of problems) {
  console.log(`${C.red}  ちがう ${p.title}${C.off}`);
  for (const l of p.lines) console.log(`        ${l}`);
}
for (const n of notes) console.log(`${C.yellow}  お知らせ${C.off} ${C.dim}${n}${C.off}`);

if (problems.length) {
  console.log('');
  console.log(`${C.red}[そろい] ${problems.length} 件そろっていません。`
    + ` docs\\maintenance.md の「ブロックを 1 つ足す」を見てください。${C.off}`);
  process.exit(1);
}

console.log(`${C.green}  OK  ${C.off}ブロック ${blockTypes.length} 種・`
  + `情報 ${sysKeys.length} 種・演算 ${opKeys.length} 種が、`
  + `栞・説明・テストにそろっています`);
console.log(`${C.green}[そろい] ぜんぶそろっています。${C.off}`);
