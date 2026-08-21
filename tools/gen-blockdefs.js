/* ブロック定義を C++ の表に書き出す道具
 *
 *   node tools\gen-blockdefs.js          書き出す
 *   node tools\gen-blockdefs.js --check  いまの中身と合っているか見るだけ
 *
 * ブロックの「正」は これまでどおり ui\js\blocks.js です。
 * ネイティブ版の画面（studio\src\w2k）も同じ定義で描くので、
 * 手で書き写さずに、ここから C++ の表を作ります。
 *
 * こうしておくと、ブロックを足すときに触る場所が増えません
 * （足したら .\build.ps1 が作りなおします。docs\maintenance.md を見てください）。
 */
'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const outPath = path.join(root, 'studio', 'src', 'w2k', 'blockdefs_gen.h');
const C = { red: '\x1b[31m', green: '\x1b[32m', dim: '\x1b[2m', off: '\x1b[0m' };

// ------------------------------------------------------------ blocks.js を読む
global.window = {};
(0, eval)(fs.readFileSync(path.join(root, 'ui', 'js', 'blocks.js'), 'utf8'));
const N = global.window.NASHI;
if (!N || !N.BLOCKS) {
  console.error(`${C.red}[生成] ui\\js\\blocks.js を読めませんでした。${C.off}`);
  process.exit(2);
}

// ------------------------------------------------------------------ 書き出し
/** C++ の文字リテラルにする（UTF-8 のまま出す。ソースは /utf-8 で読ませる） */
function cstr(s) {
  if (s == null) return '""';
  const out = String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"')
    .replace(/\r/g, '\\r').replace(/\n/g, '\\n').replace(/\t/g, '\\t');
  return `"${out}"`;
}

const SHAPE = { hat: 'Hat', stack: 'Stack', c: 'CBlock', cap: 'Cap', reporter: 'Reporter', boolean: 'Boolean' };
const KIND = {
  input: 'Input', dropdown: 'Dropdown', eventname: 'EventName',
  areaname: 'AreaName', funcname: 'FuncName', varname: 'VarName',
};
const MODE = { text: 'Text', number: 'Number', bool: 'Bool' };

const lines = [];
const w = (s) => lines.push(s);

w('// このファイルは tools\\gen-blockdefs.js が作ります。直接なおさないでください。');
w('// ブロックの「正」は ui\\js\\blocks.js です。そちらを直して、.\\build.ps1 を走らせてください。');
w('//');
w('// blockdefs.cpp の中（namespace nashi::w2k）に取りこまれる断片です。');
w('// 自分では名前空間を開きません。');
w('');
w('// clang-format off');

// ---- カテゴリ
w('static const CategoryDef kCategories[] = {');
for (const c of N.CATEGORIES) {
  w(`    { ${cstr(c.id)}, ${cstr(c.name)}, ${cstr(c.color)} },`);
}
w('};');
w(`static const int kCategoryCount = ${N.CATEGORIES.length};`);
w('');

// ---- えらぶ値（dropdown の中身）をひとまとめの表にして、各 arg から範囲で指す
const optRows = [];
function optRange(options) {
  if (!options || !options.length) return [0, 0];
  const start = optRows.length;
  for (const [label, value] of options) {
    optRows.push(`    { ${cstr(label)}, ${cstr(value)} },`);
  }
  return [start, options.length];
}

// ---- 引数（先に集めて、ブロックからは範囲で指す）
const argRows = [];
function argRange(def) {
  const names = Object.keys(def.args || {});
  if (!names.length) return [0, 0];
  const start = argRows.length;
  for (const name of names) {
    const a = def.args[name];
    const [oStart, oCount] = optRange(a.options);
    const defVal = typeof a.def === 'function' ? '' : (a.def == null ? '' : a.def);
    argRows.push('    { ' + [
      cstr(name),
      'ArgKind::' + (KIND[a.kind] || 'Input'),
      'ArgMode::' + (MODE[a.mode] || 'Text'),
      a.long ? 'true' : 'false',
      cstr(defVal),
      oStart, oCount,
    ].join(', ') + ' },');
  }
  return [start, names.length];
}

// ---- 見た目のならび（spec をほどいたもの）
const partRows = [];
function partRange(def) {
  const parts = def.parts || [];
  if (!parts.length) return [0, 0];
  const start = partRows.length;
  for (const p of parts) {
    partRows.push(`    { ${p.arg ? 'true' : 'false'}, ${cstr(p.arg || p.lbl)} },`);
  }
  return [start, parts.length];
}

// ---- 中に入る腕（if の「なら」など）
const subRows = [];
function subRange(def) {
  const subs = def.subs || [];
  if (!subs.length) return [0, 0];
  const start = subRows.length;
  for (const s of subs) subRows.push(`    { ${cstr(s.key)}, ${cstr(s.label || '')} },`);
  return [start, subs.length];
}

// ---- 固定であてる値（arith の op など）
const fixedRows = [];
function fixedRange(def) {
  const names = Object.keys(def.fixed || {});
  if (!names.length) return [0, 0];
  const start = fixedRows.length;
  for (const k of names) fixedRows.push(`    { ${cstr(k)}, ${cstr(def.fixed[k])} },`);
  return [start, names.length];
}

const blockRows = [];
const keys = Object.keys(N.BLOCKS);
for (const key of keys) {
  const d = N.BLOCKS[key];
  const [aStart, aCount] = argRange(d);
  const [pStart, pCount] = partRange(d);
  const [sStart, sCount] = subRange(d);
  const [fStart, fCount] = fixedRange(d);
  blockRows.push('    { ' + [
    cstr(key), cstr(d.type), cstr(d.cat),
    'Shape::' + (SHAPE[d.shape] || 'Stack'),
    cstr(d.kind || ''),
    d.hat ? 'true' : 'false',
    cstr(d.dynamic || ''),
    aStart, aCount, pStart, pCount, sStart, sCount, fStart, fCount,
  ].join(', ') + ' },');
}

w('static const OptionDef kOptions[] = {');
lines.push(...(optRows.length ? optRows : ['    { "", "" },']));
w('};');
w('');
w('static const ArgDef kArgs[] = {');
lines.push(...argRows);
w('};');
w('');
w('static const PartDef kParts[] = {');
lines.push(...partRows);
w('};');
w('');
w('static const SubDef kSubs[] = {');
lines.push(...(subRows.length ? subRows : ['    { "", "" },']));
w('};');
w('');
w('static const FixedDef kFixed[] = {');
lines.push(...(fixedRows.length ? fixedRows : ['    { "", "" },']));
w('};');
w('');
w('static const BlockDef kBlocks[] = {');
lines.push(...blockRows);
w('};');
w(`static const int kBlockCount = ${keys.length};`);
w('');

// ---- パレットのならび
w('// パレットに出す順。"@vars" は「いまある変数」に置きかわる目じるしです。');
w('static const PaletteRow kPalette[] = {');
for (const cat of N.CATEGORIES) {
  for (const item of (N.PALETTE[cat.id] || [])) {
    if (typeof item === 'string') w(`    { ${cstr(cat.id)}, ${cstr(item)} },`);
  }
}
w('};');
const paletteCount = N.CATEGORIES.reduce(
  (n, c) => n + (N.PALETTE[c.id] || []).filter((x) => typeof x === 'string').length, 0);
w(`static const int kPaletteCount = ${paletteCount};`);
w('');
// ---- イベントの名前（「〜されたとき」でえらべるもの）
w('// イベントの名前。N.EVENTS と同じです。');
w('static const OptionDef kEvents[] = {');
for (const [label, value] of N.EVENTS) w(`    { ${cstr(label)}, ${cstr(value)} },`);
w('};');
w(`static const int kEventCount = ${N.EVENTS.length};`);
w('');

// ---- マウス系のイベント（「どこを・だれを」でしぼり込めるもの）
w('// マウス系のイベント。N.MOUSE_EVENTS と同じです。');
w('static const char* const kMouseEvents[] = {');
for (const name of Object.keys(N.MOUSE_EVENTS)) w(`    ${cstr(name)},`);
w('};');
w(`static const int kMouseEventCount = ${Object.keys(N.MOUSE_EVENTS).length};`);
w('');

// ---- 当たり判定の名前と、だれの
w('// 当たり判定の名前。N.AREAS と同じです。');
w('static const OptionDef kAreas[] = {');
for (const [label, value] of N.AREAS) w(`    { ${cstr(label)}, ${cstr(value)} },`);
w('};');
w(`static const int kAreaCount = ${N.AREAS.length};`);
w('');

w('// だれの当たり判定か。N.WHO_ANY と同じです。');
w('static const OptionDef kWhoAny[] = {');
for (const [label, value] of N.WHO_ANY) w(`    { ${cstr(label)}, ${cstr(value)} },`);
w('};');
w(`static const int kWhoAnyCount = ${N.WHO_ANY.length};`);
w('');

w('// clang-format on');
w('');

const text = lines.join('\n');

// ---------------------------------------------------------------------- 出力
const check = process.argv.includes('--check');
const current = fs.existsSync(outPath) ? fs.readFileSync(outPath, 'utf8') : null;

if (check) {
  if (current === text) {
    console.log(`${C.green}[生成] blockdefs_gen.h は ui\\js\\blocks.js と合っています。${C.off}`);
    process.exit(0);
  }
  console.error(`${C.red}[生成] blockdefs_gen.h が古いです。`
    + ` node tools\\gen-blockdefs.js で作りなおしてください。${C.off}`);
  process.exit(1);
}

fs.mkdirSync(path.dirname(outPath), { recursive: true });
fs.writeFileSync(outPath, text, 'utf8');
console.log(`${C.green}[生成]${C.off} ${path.relative(root, outPath)} `
  + `${C.dim}(ブロック ${keys.length} / 引数 ${argRows.length} / ならび ${partRows.length})${C.off}`);
