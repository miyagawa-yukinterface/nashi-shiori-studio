/*
 * なしスタジオ - 読みこんだときの下ごしらえ（NormalizeProject）と、かたまりの見出し
 *
 * ゴーストの ghost.json は、人が手で書いたものも、古い版で作ったものも来ます。
 * 読みこんだあとで**編集できる形にそろえる**のが下ごしらえです。
 * ここが抜けると、画面に出ないだけでなく、書き出したものまで変わってしまいます。
 *
 * もとは ui\test\editor.js が JavaScript 版（model.js）で見ていたところです。
 * ネイティブ版に移したので、こちらで見ます。
 */
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');

const root = path.resolve(__dirname, '..', '..');
const host = path.join(root, 'studio', 'test', 'render_host.exe');
const C = { red: '\x1b[31m', green: '\x1b[32m', dim: '\x1b[2m', off: '\x1b[0m' };

if (!fs.existsSync(host)) {
  console.error(`${C.red}[下ごしらえ] render_host.exe がありません。`
    + ` 先に .\\build.ps1 -Test を実行してください。${C.off}`);
  process.exit(2);
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'nashi-norm-'));
let bad = 0;

function check(name, got, want) {
  const a = JSON.stringify(got);
  const b = JSON.stringify(want);
  if (a === b) {
    console.log(`${C.green}  OK  ${C.off}${name}  ${C.dim}${a}${C.off}`);
    return;
  }
  bad++;
  console.log(`${C.red}  ちがう ${name}${C.off}`);
  console.log(`        いま  : ${a}`);
  console.log(`        ほしい: ${b}`);
}

function run(args) {
  try {
    return execFileSync(host, args, { encoding: 'utf8', maxBuffer: 1 << 24 });
  } catch (e) {
    if (e.stdout) return String(e.stdout);
    if (String(e.code) === 'UNKNOWN'
        || /Application Control|アプリケーション制御/.test(String(e.message))) {
      console.log(`${C.dim}  ――  render_host.exe を起動できませんでした`
        + `（スマートアプリコントロール）。下ごしらえのテストは飛ばします。${C.off}`);
      process.exit(0);
    }
    throw e;
  }
}

let seq = 0;
/** その ghost.json を下ごしらえして、返ってきたものを読む。 */
function normalize(project) {
  const file = path.join(tmp, `p${seq++}.json`);
  fs.writeFileSync(file, JSON.stringify(project), 'utf8');
  const said = run([file, '--normalize']);
  try {
    return JSON.parse(said);
  } catch (e) {
    // 落とさずに「ちがう」と言わせるため、読めなかったことを返します
    return { _よめない: said.trim() };
  }
}

/** かたまりの見出しをならべる。 */
function titles(scripts) {
  const file = path.join(tmp, `t${seq++}.json`);
  fs.writeFileSync(file, JSON.stringify({ scripts }), 'utf8');
  return run([file, '--titles']).split(/\r?\n/).filter((l) => l.length);
}

// ==================================================== 1. 空でも形になるか
console.log(`${C.dim}-- 空でも形になるか${C.off}`);
{
  const p = normalize({});
  check('既定値でうまる', {
    name: p.meta?.name, homeUrl: p.meta?.homeUrl,
    interval: p.settings?.randomTalkInterval,
    scripts: p.scripts?.length, vars: p.variables?.length, anims: p.animations?.length,
  }, {
    name: 'なしゴースト', homeUrl: '', interval: 180,
    scripts: 0, vars: 0, anims: 0,
  });
  check('仮シェルの色も入る',
    [p.shell?.sakuraColor, p.shell?.keroColor, p.shell?.balloonEnabled],
    ['#f08cae', '#8fd18a', false]);
}

// ======================================================== 2. 変数
console.log(`${C.dim}-- 変数${C.off}`);
{
  const p = normalize({
    variables: [{ name: 'すき', value: 3 }, { name: '', value: 1 }, { value: 9 }],
  });
  check('名前のない変数は落とす', p.variables, [{ name: 'すき', value: 3 }]);
}
{
  const p = normalize({ variables: [{ name: 'から' }] });
  check('値が無ければ 0', p.variables?.[0]?.value, 0);
}

// ======================================================== 3. うごき
console.log(`${C.dim}-- うごき${C.off}`);
{
  const p = normalize({
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
  const a = p.animations?.[0] || {};
  check('知らない重ねかたは base に倒す', (a.patterns || []).map((x) => x.method),
    ['interpolate', 'base', 'import']);
  check('こまの既定値がうまる', a.patterns?.[1],
    { surface: 2, wait: 200, method: 'base', x: 0, y: 0, file: '' });
  check('差しこむファイル名は残る', a.patterns?.[2]?.file, 'a.png');
  check('名前のない当たり判定は落とし、知らない形は四角に倒す',
    (a.collisions || []).map((c) => [c.name, c.shape]),
    [['Wing', 'polygon'], ['Odd', 'rect']]);
  check('多角形のかどはそのまま持つ', a.collisions?.[0]?.points, '1,2 3,4 5,6');
  check('きっかけと間かくは残る', [a.interval, a.every, a.id], ['talk', 2, 3]);
}
{
  const p = normalize({ animations: [{}] });
  check('からのうごきも形になる', p.animations?.[0],
    { id: 0, base: 0, interval: 'never', every: 4, patterns: [], collisions: [] });
}

// ==================================================== 4. かたまりのしぼり込み
console.log(`${C.dim}-- かたまり${C.off}`);
{
  const p = normalize({
    scripts: [
      { kind: 'event', event: 'OnNadeNade', filter: { area: 'Head', who: 1 } },
      { kind: 'event', event: 'OnCommunicate', filter: { from: 'みかん', contains: 'ねえ' } },
      { kind: 'talk' },
      { kind: 'function' },
    ],
  });
  const ss = p.scripts || [];
  check('マウスのしぼり込みをほどく', [ss[0]?.area, ss[0]?.who], ['Head', 1]);
  check('ほどいたあと、filter は残さない', ss[0]?.filter, undefined);
  check('通信のしぼり込みをほどく', [ss[1]?.from, ss[1]?.contains], ['みかん', 'ねえ']);
  check('トークには名前とえらばれやすさが付く', [ss[2]?.name, ss[2]?.weight], ['トーク', 1]);
  check('名前のないトークにも名前が付く', ss[3]?.name, 'なまえのないトーク');
  check('置き場所が決まる', typeof ss[0]?.x, 'number');
  check('たてにならぶ', ss.map((s) => s.y), [40, 220, 400, 580]);
  check('id が付く', ss.map((s) => typeof s.id), ['string', 'string', 'string', 'string']);
  check('id はかぶらない', new Set(ss.map((s) => s.id)).size, 4);
}
{
  const p = normalize({ scripts: [{ id: 's0' }, {}] });
  check('もとからある id は変えない', p.scripts?.[0]?.id, 's0');
  check('あとの id はかぶらない', p.scripts?.[1]?.id !== 's0', true);
}

// ==================================================== 5. かたまりの見出し
console.log(`${C.dim}-- かたまりの見出し${C.off}`);
{
  const got = titles([
    { kind: 'event', event: 'OnBoot' },
    { kind: 'event', event: 'OnSecondChange', everySec: 5 },
    { kind: 'event', event: 'OnNadeNade', area: 'Head', who: 0 },
    { kind: 'event', event: 'OnNadeNade', area: 'Wing', who: -1 },
    { kind: 'event', event: 'OnCommunicate', from: 'みかん', contains: 'やあ' },
    { kind: 'event', event: 'OnUnknownThing' },
    { kind: 'talk', name: 'あさ', group: 'あいさつ' },
    { kind: 'function', name: 'そとがわ' },
  ]);
  check('ふつうのイベント', got[0], '起動したとき');
  check('◯秒ごと', got[1], '5秒ごとにくりかえす');
  check('なでなで（仮シェルの場所）', got[2], 'さくらのあたまがなでられたとき');
  check('なでなで（うごきで足した場所）', got[3], 'どちらかのWingがなでられたとき');
  check('話しかけられたとき', got[4], 'みかんに「やあ」と話しかけられたとき');
  check('知らないイベントはそのまま', got[5], 'OnUnknownThing');
  check('ランダムトーク', got[6], 'ランダムトーク「あさ」（あいさつ）');
  check('よばれるトーク', got[7], '「そとがわ」');
}

// ============================================ 6. 二度かけても同じか
console.log(`${C.dim}-- 二度かけても同じか${C.off}`);
{
  const once = normalize({
    scripts: [{ kind: 'event', event: 'OnNadeNade', filter: { area: 'Head' } }],
    variables: [{ name: 'すき', value: 1 }],
    animations: [{ id: 1, patterns: [{ surface: 2 }] }],
  });
  const twice = normalize(once);
  check('もう一度かけても変わらない', twice, once);
}

// ---------------------------------------------------------------------- 結果
try { fs.rmSync(tmp, { recursive: true, force: true }); } catch (e) { /* 消せなくても構わない */ }

console.log('');
if (bad) {
  console.log(`${C.red}[下ごしらえ] ${bad} か所ちがいます。${C.off}`);
  process.exit(1);
}
console.log(`${C.green}[下ごしらえ] 読みこんだあとの形は期待どおりです。${C.off}`);
