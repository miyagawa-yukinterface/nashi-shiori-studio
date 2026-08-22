/*
 * なしスタジオ - チェック（lint.cpp）のテスト
 *
 * 同じ ghost.json を通して、出た順・段階（まちがい／気になる）・言葉・説明が
 * 前と同じかを 1 件ずつくらべます。期待するものは studio\test\expected\lint\ に
 * 置いてあります。
 *
 * 言いかたを直したときは、出てきたものを**目で確かめてから**
 *   node studio\test\lint.js --update
 * で置きかえてください。
 *
 * 見本のゴーストだけでは通らない道が多いので、わざと変なところのある
 * ghost.json をこの場で組み立てて、それも通します。
 */
'use strict';

const fs = require('fs');
const path = require('path');
const os = require('os');
const { execFileSync } = require('child_process');

const root = path.resolve(__dirname, '..', '..');
const host = path.join(root, 'studio', 'test', 'render_host.exe');
const C = { red: '\x1b[31m', green: '\x1b[32m', dim: '\x1b[2m', off: '\x1b[0m' };

if (!fs.existsSync(host)) {
  console.error(`${C.red}[チェック] render_host.exe がありません。`
    + ` 先に .\\build.ps1 -Test を実行してください。${C.off}`);
  process.exit(2);
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'nashi-lint-'));
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
    // うまくいかなかったときも、言い分は返してもらう（何が起きたか見えるように）
    if (e.stdout) return String(e.stdout);
    if (String(e.code) === 'UNKNOWN'
        || /Application Control|アプリケーション制御/.test(String(e.message))) {
      console.log(`${C.dim}  ――  render_host.exe を起動できませんでした`
        + `（スマートアプリコントロール）。チェックのテストは飛ばします。${C.off}`);
      process.exit(0);
    }
    throw e;
  }
}

// ---------------------------------------------- 期待するものの置き場所
const expectedDir = path.join(root, 'studio', 'test', 'expected', 'lint');
const updating = process.argv.includes('--update');
if (updating) fs.mkdirSync(expectedDir, { recursive: true });

/** チェックの結果を、1 件 1 行にして返す。 */
function lintOf(file) {
  const said = run([file, '--lint']).split(/\r?\n/);
  const out = [];
  for (let i = 0; i + 2 < said.length; i += 3) {
    if (said[i].startsWith('---- ')) break;
    out.push(`${said[i]}\t${said[i + 1]}\t${said[i + 2]}`);
  }
  return out;
}

/** ファイル名に使えない字を、使える字に。 */
function safeName(name) {
  return name.replace(/[\\/:*?"<>|]/g, '_');
}

function compare(name, project) {
  const file = path.join(tmp, safeName(name) + '.json');
  fs.writeFileSync(file, JSON.stringify(project), 'utf8');
  const got = lintOf(file);

  const expected = path.join(expectedDir, safeName(name) + '.txt');
  if (updating) {
    fs.writeFileSync(expected, got.join('\n') + (got.length ? '\n' : ''), 'utf8');
    console.log(`${C.dim}  書きました  ${name}（${got.length} 件）${C.off}`);
    return;
  }
  if (!fs.existsSync(expected)) {
    check(`${name}（期待するものがありません）`,
      `studio\\test\\expected\\lint\\${safeName(name)}.txt`, 'あるはず');
    return;
  }
  const want = fs.readFileSync(expected, 'utf8').split(/\r?\n/).filter((l) => l.length);

  let diff = '';
  for (let i = 0; i < Math.max(want.length, got.length); i++) {
    if (want[i] === got[i]) continue;
    diff = `${i} 件め\n        いま  : ${got[i]}\n        まえ  : ${want[i]}`;
    break;
  }
  check(`${name}（${got.length} 件）`, diff || '同じ', '同じ');
}

// ------------------------------------------------ 1. 見本のゴーストで
console.log(`${C.dim}-- 見本のゴーストで${C.off}`);
for (const [name, file] of [
  ['drag_fixture', path.join(root, 'studio', 'test', 'drag_fixture.json')],
  ['parity', path.join(root, 'shiori', 'test', 'parity', 'ghost.json')],
  ['behavior', path.join(root, 'shiori', 'test', 'behavior', 'main', 'ghost.json')],
]) {
  if (!fs.existsSync(file)) continue;
  compare(name, JSON.parse(fs.readFileSync(file, 'utf8')));
}

// お手本のゴーストも通す（ui\samples）
const samples = path.join(root, 'ui', 'samples');
if (fs.existsSync(samples)) {
  for (const f of fs.readdirSync(samples).filter((x) => x.endsWith('.json') && x !== 'index.json')) {
    compare('お手本 ' + f, JSON.parse(fs.readFileSync(path.join(samples, f), 'utf8')));
  }
}

// ---------------------------------- 2. わざと変なところを作って、道をぜんぶ通す
console.log(`${C.dim}-- わざと変なところを作って${C.off}`);

const say = (text) => ({ type: 'say', who: 0, text, nl: '1' });

compare('からっぽ', { scripts: [], variables: [] });

compare('名前のかぶり', {
  scripts: [
    { kind: 'talk', name: 'あいさつ', weight: 1, blocks: [say('やあ')] },
    { kind: 'talk', name: 'あいさつ', weight: 1, blocks: [say('やあ')] },
    { kind: 'function', name: '', blocks: [say('なまえがない')] },
  ],
  variables: [],
});

compare('空の欄いろいろ', {
  scripts: [{
    kind: 'event', event: 'OnBoot',
    blocks: [
      say(''),
      { type: 'raw', text: '' },
      { type: 'communicate', to: '', text: '' },
      { type: 'ask', text: 'なまえは？', into: '' },
      { type: 'call_group', group: '' },
      { type: 'later', sec: 3, name: '' },
      { type: 'raise', event: '' },
      { type: 'open_browser', url: '' },
      { type: 'change_ghost', name: '' },
      { type: 'saori_call', file: '', into: '' },
      { type: 'update' },
      { type: 'set', name: '', value: 1 },
      { type: 'call', name: '' },
      { type: 'choice', label: '', target: '' },
      { type: 'link', label: 'こちら', url: 'https://' },
      { type: 'wait', ms: 90000 },
    ],
  }],
  variables: [],
});

compare('無いものを指している', {
  scripts: [{
    kind: 'event', event: 'OnBoot',
    blocks: [
      { type: 'set', name: 'ないへんすう', value: 1 },
      { type: 'ask', text: 'なに？', into: 'ないへんすう' },
      { type: 'saori_call', file: 'x.dll', into: 'ないへんすう' },
      { type: 'call', name: 'ないトーク' },
      { type: 'later', sec: 1, name: 'ないトーク' },
      { type: 'call_group', group: 'ないまとまり' },
      { type: 'choice', label: 'はい', target: 'ないトーク' },
      { type: 'なぞのブロック' },
    ],
  }],
  variables: [],
});

compare('たずねる先の名前が変', {
  scripts: [{
    kind: 'event', event: 'OnBoot',
    blocks: [{ type: 'ask', text: 'なに？', into: 'あ,い' }],
  }],
  variables: [{ name: 'あ,い', value: 0 }],
});

compare('毎秒しゃべる', {
  scripts: [{
    kind: 'event', event: 'OnSecondChange', everySec: 1,
    blocks: [say('まいびょう')],
  }],
  variables: [],
});

compare('毎秒たくさん動く', {
  scripts: [{
    kind: 'event', event: 'OnSecondChange', everySec: 1,
    blocks: [{ type: 'repeat', count: 3, body: [{ type: 'wait', ms: 10 }] }],
  }],
  variables: [],
});

compare('くりかえしの中で細かく待つ', {
  scripts: [{
    kind: 'event', event: 'OnBoot',
    blocks: [{ type: 'repeat', count: 100, body: [{ type: 'wait', ms: 5 }, say('あ')] }],
  }],
  variables: [],
});

compare('えらばれやすさがマイナス', {
  scripts: [{ kind: 'talk', name: 'まいなす', weight: -1, blocks: [say('あ')] }],
  variables: [],
});

compare('使われていない変数', {
  scripts: [{ kind: 'talk', name: 'つかわない', weight: 1, blocks: [say('あ')] }],
  variables: [{ name: 'つかわれない', value: 0 }, { name: 'これも', value: '' }],
});

compare('ランダムトークが無い', {
  scripts: [{ kind: 'event', event: 'OnBoot', blocks: [say('あ')] }],
  variables: [],
  settings: { randomTalkEnabled: true, randomTalkInterval: 180 },
});

compare('しゃべる間隔が 0', {
  scripts: [{ kind: 'talk', name: 'あ', weight: 1, blocks: [say('あ')] }],
  variables: [],
  settings: { randomTalkEnabled: true, randomTalkInterval: 0 },
});

compare('自動でしゃべらない', {
  scripts: [{ kind: 'event', event: 'OnBoot', blocks: [say('あ')] }],
  variables: [],
  settings: { randomTalkEnabled: false, randomTalkInterval: 0 },
});

compare('更新のありかがある', {
  meta: { homeUrl: 'https://example.com/ghost/' },
  scripts: [{ kind: 'event', event: 'OnBoot', blocks: [{ type: 'update' }] }],
  variables: [],
});

compare('入れ子の中もみる', {
  scripts: [{
    kind: 'event', event: 'OnBoot',
    blocks: [{
      type: 'if',
      cond: { type: 'compare', op: '==', a: { type: 'var', name: 'ないへんすう' }, b: 1 },
      then: [{ type: 'repeat', count: 2, body: [say('')] }],
    }],
  }],
  variables: [],
});

compare('どれかランダムに', {
  scripts: [{
    kind: 'event', event: 'OnBoot',
    blocks: [{ type: 'random_one', branches: [[say('')], [{ type: 'call', name: 'ない' }]] }],
  }],
  variables: [],
});

// ----------------------------------------- 3. まちがいの数と、ならびかた
console.log(`${C.dim}-- まちがいを先に出すか${C.off}`);
{
  const project = {
    scripts: [{
      kind: 'event', event: 'OnBoot',
      blocks: [say(''), { type: 'call', name: 'ない' }, { type: 'raw', text: '' }],
    }],
    variables: [],
  };
  const file = path.join(tmp, 'order.json');
  fs.writeFileSync(file, JSON.stringify(project), 'utf8');

  const said = run([file, '--lint']);
  const levels = said.split(/\r?\n/).filter((l) => l === 'error' || l === 'warn');
  const firstWarn = levels.indexOf('warn');
  const lastError = levels.lastIndexOf('error');
  check('まちがいが先にならぶ',
    (firstWarn < 0 || lastError < firstWarn) ? 'はい' : levels.join(','), 'はい');
  check('数えかたが合っている',
    (said.match(/---- (\d+) 件（まちがい (\d+)）/) || []).slice(1).join('/'),
    `${levels.length}/${levels.filter((l) => l === 'error').length}`);
}

// -------------------------------------------------- 4. チェックのたなに出るか
console.log(`${C.dim}-- チェックのたなに出るか${C.off}`);
{
  const project = {
    scripts: [{ kind: 'event', event: 'OnBoot', blocks: [say('')] }],
    variables: [],
  };
  const file = path.join(tmp, 'tab.json');
  fs.writeFileSync(file, JSON.stringify(project), 'utf8');

  const said = run([file, '--panel', '6']);
  check('たなの名前', (said.match(/^たな (.*)$/m) || [])[1], 'チェック');
  check('見つけたものが行になる',
    (said.match(/^row check\.hit\.\d+ /gm) || []).length >= 1 ? 'はい' : 'いいえ', 'はい');
  check('数えたものが出る',
    /まちがい \d+ \/ 気になる \d+/.test(said) ? 'はい' : 'いいえ', 'はい');

  const clean = path.join(tmp, 'clean.json');
  fs.writeFileSync(clean, JSON.stringify({
    meta: {}, variables: [],
    settings: { randomTalkEnabled: false },
    scripts: [{ kind: 'event', event: 'OnBoot', blocks: [say('こんにちは')] }],
  }), 'utf8');
  const ok = run([clean, '--panel', '6']);
  check('何も無ければ、そう言う',
    /気になるところはありません/.test(ok) ? 'はい' : 'いいえ', 'はい');
}

// ---------------------------------------------------------------------- 結果
try { fs.rmSync(tmp, { recursive: true, force: true }); } catch (e) { /* 消せなくても構わない */ }

console.log('');
if (bad) {
  console.log(`${C.red}[チェック] ${bad} か所ちがいます。`
    + ` わざと変えたのなら、node studio\\test\\lint.js --update${C.off}`);
  process.exit(1);
}
console.log(`${C.green}[チェック] 前と同じことを言います。${C.off}`);
