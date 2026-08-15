/* エディタの中身のテスト（データの整形とチェックタブ）
 *
 *   node ui\test\editor.js
 *
 * 画面を出さずに、ui/js の中の「判断しているところ」だけを動かします。
 *
 *   model.js … 読みこんだプロジェクトの整形（normalize）と、かたまりの名前
 *   lint.js  … チェックタブが出す注意
 *
 * sim.js（プレビュー）は shiori/test/parity で栞と突き合わせているので、ここでは見ません。
 */
'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..', '..');
const COLOR = { red: '\x1b[31m', green: '\x1b[32m', dim: '\x1b[2m', off: '\x1b[0m' };
const { red, green, dim, off } = COLOR;

// 画面のスクリプトをそのまま読みこむ（window があるつもりで動きます）
global.window = {};
for (const f of ['blocks.js', 'model.js', 'lint.js']) {
  (0, eval)(fs.readFileSync(path.join(root, 'ui', 'js', f), 'utf8'));
}
const N = global.window.NASHI;
const { Model, Lint } = N;

let failed = 0;
let checked = 0;
let group = '';

function section(name) { group = name; console.log(`  ---- ${name}`); }

function check(what, got, want) {
  checked++;
  const a = JSON.stringify(got);
  const b = JSON.stringify(want);
  if (a === b) {
    console.log(`${green}  OK  ${off}${what}`);
    return;
  }
  failed++;
  console.log(`${red}  だめ ${what}${off}`);
  console.log(`        ほしい: ${b}`);
  console.log(`        出た  : ${a}`);
}

/** チェックタブの結果から、message に文字列をふくむものを探す */
function issuesOf(project, part) {
  Model.project = Model.normalize(project);
  return Lint.run(Model.project)
    .filter((i) => i.message.includes(part) || (i.hint || '').includes(part))
    .map((i) => i.level);
}

// ===================================================== プロジェクトの整形
section('読みこんだプロジェクトの整形 (model.normalize)');

{
  const p = Model.normalize({});
  check('空でも既定値でうまる', {
    name: p.meta.name, homeUrl: p.meta.homeUrl,
    interval: p.settings.randomTalkInterval,
    scripts: p.scripts.length, vars: p.variables.length, anims: p.animations.length,
  }, { name: 'なしゴースト', homeUrl: '', interval: 180, scripts: 0, vars: 0, anims: 0 });
}

{
  const p = Model.normalize({
    variables: [{ name: 'すき', value: 3 }, { name: '', value: 1 }, { value: 9 }],
  });
  check('名前のない変数は落とす', p.variables, [{ name: 'すき', value: 3 }]);
}

{
  const p = Model.normalize({
    animations: [{
      id: 3, base: 0, interval: 'talk', every: 2,
      patterns: [
        { method: 'interpolate', surface: 1, wait: 50, x: -2, y: 3 },
        { method: 'しらない', surface: 2 },
        { method: 'import', file: 'a.png', wait: 10 },
      ],
      collisions: [
        { name: 'Wing', shape: 'polygon', points: '1,2 3,4 5,6' },
        { name: 'Odd', shape: 'へんな形' },
        { name: '   ', shape: 'rect' },
      ],
    }],
  });
  const a = p.animations[0];
  check('知らない重ねかたは base に倒す', a.patterns.map((x) => x.method),
    ['interpolate', 'base', 'import']);
  check('こまの既定値がうまる', a.patterns[1],
    { surface: 2, wait: 200, method: 'base', x: 0, y: 0, file: '' });
  check('差しこむファイル名は残る', a.patterns[2].file, 'a.png');
  check('名前のない当たり判定は落とし、知らない形は四角に倒す',
    a.collisions.map((c) => [c.name, c.shape]), [['Wing', 'polygon'], ['Odd', 'rect']]);
  check('多角形のかどはそのまま持つ', a.collisions[0].points, '1,2 3,4 5,6');
}

{
  const p = Model.normalize({
    scripts: [
      { kind: 'event', event: 'OnNadeNade', filter: { area: 'Head', who: 1 } },
      { kind: 'event', event: 'OnCommunicate', filter: { from: 'みかん', contains: 'ねえ' } },
      { kind: 'talk' },
      { kind: 'function' },
    ],
  });
  check('マウスのしぼり込みをほどく',
    [p.scripts[0].area, p.scripts[0].who], ['Head', 1]);
  check('通信のしぼり込みをほどく',
    [p.scripts[1].from, p.scripts[1].contains], ['みかん', 'ねえ']);
  check('トークには名前とえらばれやすさが付く',
    [p.scripts[2].name, p.scripts[2].weight], ['トーク', 1]);
  check('名前のないトークにも名前が付く', p.scripts[3].name, 'なまえのないトーク');
  check('置き場所が決まる', typeof p.scripts[0].x, 'number');
}

section('かたまりの名前 (scriptTitle)');
{
  Model.project = Model.normalize({
    animations: [{ id: 0, base: 0, collisions: [{ name: 'Wing', shape: 'rect' }] }],
  });
  check('ふつうのイベント',
    Model.scriptTitle({ kind: 'event', event: 'OnBoot' }), '起動したとき');
  check('◯秒ごと',
    Model.scriptTitle({ kind: 'event', event: 'OnSecondChange', everySec: 5 }),
    '5秒ごとにくりかえす');
  check('なでなで（仮シェルの場所）',
    Model.scriptTitle({ kind: 'event', event: 'OnNadeNade', area: 'Head', who: 0 }),
    'さくらのあたまがなでられたとき');
  check('なでなで（うごきで足した場所）',
    Model.scriptTitle({ kind: 'event', event: 'OnNadeNade', area: 'Wing', who: -1 }),
    'どちらかのWingがなでられたとき');
  check('話しかけられたとき',
    Model.scriptTitle({ kind: 'event', event: 'OnCommunicate', from: 'みかん', contains: 'やあ' }),
    'みかんに「やあ」と話しかけられたとき');
  check('知らないイベントはそのまま',
    Model.scriptTitle({ kind: 'event', event: 'OnUnknownThing' }), 'OnUnknownThing');
}

// =========================================================== チェックタブ
section('チェックタブ (lint)');

const withScript = (blocks, extra) => Object.assign({
  meta: {}, variables: [{ name: 'こたえ', value: '' }],
  scripts: [{ id: 's1', kind: 'event', event: 'OnBoot', blocks }],
}, extra || {});

check('なにも言わないセリフ',
  issuesOf(withScript([{ type: 'say', who: 0, text: '' }]), 'なにも言わないセリフ'), ['warn']);

check('話しかける相手が空',
  issuesOf(withScript([{ type: 'communicate', who: 0, to: '', text: 'やあ' }]), '話しかける相手が空'),
  ['error']);

check('無い変数を使っている',
  issuesOf(withScript([{ type: 'set', name: 'ないよ', value: 1 }]), '「ないよ」がありません'),
  ['error']);

check('知らないブロック',
  issuesOf(withScript([{ type: 'なぞ' }]), '知らないブロック'), ['error']);

// --- ネットワーク更新
check('更新のありかが空のまま、更新ブロックを置いている',
  issuesOf(withScript([{ type: 'update' }]), 'ネットワーク更新のブロック'), ['warn']);

check('更新のありかを決めていれば言わない',
  issuesOf(withScript([{ type: 'update' }], { meta: { homeUrl: 'https://example.com/' } }),
    'ネットワーク更新のブロック'), []);

// --- 待たない SAORI
check('よぶファイルが空',
  issuesOf(withScript([{ type: 'saori_call', file: '', into: 'こたえ' }]), 'よぶファイルが空'),
  ['error']);

check('答えの入れ先が空',
  issuesOf(withScript([{ type: 'saori_call', file: 'a.dll', into: '' }]), '答えの入れ先が空'),
  ['warn']);

check('答えの入れ先が無い変数',
  issuesOf(withScript([{ type: 'saori_call', file: 'a.dll', into: 'ないよ' }]),
    '「ないよ」がありません'), ['error']);

check('答えの入れ先も「使っている」に数える',
  issuesOf(withScript([{ type: 'saori_call', file: 'a.dll', into: 'こたえ' }]),
    'どこでも使われていません'), []);

check('どこでも使われていない変数は言う',
  issuesOf(withScript([{ type: 'say', who: 0, text: 'やあ' }]), 'どこでも使われていません'),
  ['warn']);

// ------------------------------------------------------------------ おしまい
if (failed) {
  console.log(`${red}[editor] ${failed} か所ちがいます（${checked} か所中）。${off}`);
  process.exit(1);
}
console.log(`${green}[editor] ${checked} か所すべて期待どおりです。${off}`);
