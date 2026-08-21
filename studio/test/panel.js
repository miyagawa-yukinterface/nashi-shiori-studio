/*
 * なしスタジオ - 右の作業だな（ネイティブ版）のテスト
 *
 * ここは 2 つのことを見ます。
 *
 *  1. **言いあらわしが JavaScript 版と同じか**
 *     ブロックを字にする blockSummary と、かたまりの見出し scriptTitle は、
 *     いま JavaScript（ui\js）と C++（studio\src\w2k\panel.cpp）の両方にあります。
 *     WebView2 版を外すまでの間だけ二重になるので、そのあいだは
 *     **同じ ghost.json を両方に通して、字が一致するか**を見張ります。
 *     （WebView2 版を外したら JavaScript 側が消えるので、この見張りも要らなくなります）
 *
 *  2. 作業だなが組み立てられて、押したとおりに ghost.json が変わるか
 */
'use strict';

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const root = path.resolve(__dirname, '..', '..');
const host = path.join(root, 'studio', 'test', 'render_host.exe');
const fixture = path.join(root, 'studio', 'test', 'drag_fixture.json');
const C = { red: '\x1b[31m', green: '\x1b[32m', dim: '\x1b[2m', off: '\x1b[0m' };

if (!fs.existsSync(host)) {
  console.error(`${C.red}[たな] render_host.exe がありません。`
    + ` 先に .\\build.ps1 -Test を実行してください。${C.off}`);
  process.exit(2);
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

function run(args) {
  try {
    return execFileSync(host, args, { encoding: 'utf8', maxBuffer: 1 << 24 });
  } catch (e) {
    if (String(e.code) === 'UNKNOWN'
        || /Application Control|アプリケーション制御/.test(String(e.message))) {
      console.log(`${C.dim}  ――  render_host.exe を起動できませんでした`
        + `（スマートアプリコントロール）。たなのテストは飛ばします。${C.off}`);
      process.exit(0);
    }
    throw e;
  }
}

// ------------------------------------------- ui\js を、画面を出さずに読みこむ
// ui\test\modules.js と同じやりかたです。
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
if (!globalThis.navigator) global.navigator = { userAgent: 'node' };
global.fetch = () => Promise.reject(new Error('テストでは通信しません'));

const html = fs.readFileSync(path.join(root, 'ui', 'index.html'), 'utf8');
const listed = [...html.matchAll(/<script src="js\/([^"]+)"><\/script>/g)].map((m) => m[1]);
for (const f of listed) {
  (0, eval)(fs.readFileSync(path.join(root, 'ui', 'js', f), 'utf8'));
}
const N = global.window.NASHI;

/**
 * JavaScript 版で、同じならびを作る（--summary と同じ形）。
 * 見る順は ui\js\search.js の walkBlocks と同じにしてあります。
 */
function summaryFromJs(project) {
  const lines = [];
  const visit = (b) => lines.push('block\t' + N.App.blockSummary(b));

  const stack = (arr) => {
    if (!Array.isArray(arr)) return;
    for (const b of arr) {
      if (!b || typeof b !== 'object') continue;
      visit(b);
      inner(b);
    }
  };
  const inner = (b) => {
    const d = N.getDef(b);
    if (!d) return;
    for (const sub of d.subs || []) stack(b[sub.key]);
    if (d.dynamic && Array.isArray(b[d.dynamic])) b[d.dynamic].forEach(stack);
    for (const name in (d.args || {})) {
      const v = b[name];
      if (v && typeof v === 'object' && v.type) { visit(v); inner(v); }
    }
  };

  for (const s of project.scripts) {
    lines.push('title\t' + N.Model.scriptTitle(s));
    stack(s.blocks);
  }
  return lines;
}

// ---------------------------------------- 1. JavaScript 版と字が一致するか
console.log(`${C.dim}-- 言いあらわしが JavaScript 版と同じか${C.off}`);
for (const name of ['drag_fixture', 'parity', 'behavior']) {
  const file = name === 'drag_fixture' ? fixture
    : name === 'parity' ? path.join(root, 'shiori', 'test', 'parity', 'ghost.json')
      : path.join(root, 'shiori', 'test', 'behavior', 'main', 'ghost.json');
  if (!fs.existsSync(file)) continue;

  const project = N.Model.normalize(JSON.parse(fs.readFileSync(file, 'utf8')));
  const wantLines = summaryFromJs(project);
  const gotLines = run([file, '--summary']).split(/\r?\n/).filter((l) => l.length);

  let firstDiff = '';
  for (let i = 0; i < Math.max(wantLines.length, gotLines.length); i++) {
    if (wantLines[i] === gotLines[i]) continue;
    firstDiff = `${i} 行め\n        C++ : ${gotLines[i]}\n        JS  : ${wantLines[i]}`;
    break;
  }
  check(`${name}.json の言いあらわし（${gotLines.length} 行）`,
    firstDiff || '同じ', '同じ');
}

// ---------------------------------------------------- 2. 変数のたな
console.log(`${C.dim}-- 変数のたな${C.off}`);
{
  const said = run([fixture, '--panel', '2']);
  check('たなの名前', (said.match(/^たな (.*)$/m) || [])[1], '変数');
  check('変数がならぶ',
    (said.match(/^field var\.name\.\d+ .* = (.*)$/gm) || [])
      .map((l) => l.replace(/^.* = /, '')).join(','), 'カウンタ,きぶん');
  check('つくるボタンがある', /button var\.add /.test(said) ? 'はい' : 'いいえ', 'はい');
  check('けすボタンが変数の数だけある',
    (said.match(/^button var\.del\.\d+ /gm) || []).length, 2);
}

{
  // ＋ を押すと 1 つふえる
  const said = run([fixture, '--panel', '2', '--click', 'var.add']);
  const json = JSON.parse(said.slice(said.indexOf('---- ghost.json') + 15));
  check('＋ でふえる', json.variables.length, 3);
  check('あたらしい名前がかぶらない',
    new Set(json.variables.map((v) => v.name)).size, 3);

  // けすと 1 つへる
  const said2 = run([fixture, '--panel', '2', '--click', 'var.del.0']);
  const json2 = JSON.parse(said2.slice(said2.indexOf('---- ghost.json') + 15));
  check('けすとへる', json2.variables.map((v) => v.name).join(','), 'きぶん');

  // 名前を打ちかえる
  const said3 = run([fixture, '--panel', '2', '--click', 'var.name.0', 'すきど']);
  const json3 = JSON.parse(said3.slice(said3.indexOf('---- ghost.json') + 15));
  check('名前を打ちかえられる', json3.variables[0].name, 'すきど');

  // はじめの値を打ちかえる
  const said4 = run([fixture, '--panel', '2', '--click', 'var.value.1', 'ごきげん']);
  const json4 = JSON.parse(said4.slice(said4.indexOf('---- ghost.json') + 15));
  check('はじめの値を打ちかえられる', json4.variables[1].value, 'ごきげん');
}

// ---------------------------------------------------- 3. さがすたな
console.log(`${C.dim}-- さがすたな${C.off}`);
{
  const empty = run([fixture, '--panel', '3']);
  check('言葉が空なら、うながす', /さがす言葉を入れてください/.test(empty) ? 'はい' : 'いいえ', 'はい');

  const said = run([fixture, '--panel', '3', '--q', 'はじめ']);
  const rows = (said.match(/^row search\.hit\.\d+ .*$/gm) || []);
  check('見つかる', rows.length, 1);
  const first = rows[0] || '（1 つも見つかりませんでした）';
  check('見つけたものの字',
    (first.match(/\d+x\d+\s+(.*?) \/ /) || [])[1], 'sakura が はじめ と話す 改行する');
  check('どのかたまりか出る', /\/ 起動したとき$/.test(first) ? 'はい' : first, 'はい');

  const none = run([fixture, '--panel', '3', '--q', 'あるはずのない言葉']);
  check('無いときは、そう言う', /見つかりませんでした/.test(none) ? 'はい' : 'いいえ', 'はい');

  // かたまりの見出しでも見つかる
  const title = run([fixture, '--panel', '3', '--q', 'ランダムトーク']);
  check('かたまりの見出しでも見つかる',
    (title.match(/^row search\.hit\.\d+ /gm) || []).length >= 1 ? 'はい' : 'いいえ', 'はい');
}

// ---------------------------------------------------- 4. たなの切りかえ
console.log(`${C.dim}-- たなの切りかえ${C.off}`);
{
  const names = [];
  for (let i = 0; i < 7; i++) {
    names.push((run([fixture, '--panel', String(i)]).match(/^たな (.*)$/m) || [])[1]);
  }
  check('たなは 7 つ', names.join(','),
    'ためす,ゴースト,変数,さがす,チェック,書き出し,ヘルプ');

  const notYet = run([fixture, '--panel', '0']);
  check('まだ作っていないたなは、そう言う',
    /まだ作っていません/.test(notYet) ? 'はい' : 'いいえ', 'はい');
}

// ---------------------------------------------------------------------- 結果
console.log('');
if (bad) {
  console.log(`${C.red}[たな] ${bad} か所ちがいます。${C.off}`);
  process.exit(1);
}
console.log(`${C.green}[たな] 右の作業だなは期待どおりです。${C.off}`);
