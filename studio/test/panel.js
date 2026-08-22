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
const os = require('os');
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
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'nashi-panel-'));

// PNG の chunk を組むのに使います（studio\test\image.js と同じもの）
const crcTable = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    t[n] = c >>> 0;
  }
  return t;
})();
function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = crcTable[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
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
 * 押したあとの ghost.json を取り出す。
 * 見つからないときは、空のものを返します（落とさずに「ちがう」と言わせるため）。
 */
function jsonOf(said) {
  const cut = said.indexOf('---- ghost.json');
  if (cut < 0) return { _なし: said.trim() };
  try {
    return JSON.parse(said.slice(cut + '---- ghost.json'.length));
  } catch (e) {
    return { _よめない: String(e.message) };
  }
}

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
  const said = run([fixture, '--panel', '4']);
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
  const said = run([fixture, '--panel', '4', '--click', 'var.add']);
  const json = jsonOf(said);
  check('＋ でふえる', json.variables?.length, 3);
  check('あたらしい名前がかぶらない',
    new Set((json.variables || []).map((v) => v.name)).size, 3);

  // けすと 1 つへる
  const said2 = run([fixture, '--panel', '4', '--click', 'var.del.0']);
  const json2 = jsonOf(said2);
  check('けすとへる', (json2.variables || []).map((v) => v.name).join(','), 'きぶん');

  // 名前を打ちかえる
  const said3 = run([fixture, '--panel', '4', '--click', 'var.name.0', 'すきど']);
  const json3 = jsonOf(said3);
  check('名前を打ちかえられる', json3.variables?.[0]?.name, 'すきど');

  // はじめの値を打ちかえる
  const said4 = run([fixture, '--panel', '4', '--click', 'var.value.1', 'ごきげん']);
  const json4 = jsonOf(said4);
  check('はじめの値を打ちかえられる', json4.variables?.[1]?.value, 'ごきげん');
}

// ---------------------------------------------------- 3. さがすたな
console.log(`${C.dim}-- さがすたな${C.off}`);
{
  const empty = run([fixture, '--panel', '5']);
  check('言葉が空なら、うながす', /さがす言葉を入れてください/.test(empty) ? 'はい' : 'いいえ', 'はい');

  const said = run([fixture, '--panel', '5', '--q', 'はじめ']);
  const rows = (said.match(/^row search\.hit\.\d+ .*$/gm) || []);
  check('見つかる', rows.length, 1);
  const first = rows[0] || '（1 つも見つかりませんでした）';
  check('見つけたものの字',
    (first.match(/\d+x\d+\s+(.*?) \/ /) || [])[1], 'sakura が はじめ と話す 改行する');
  check('どのかたまりか出る', /\/ 起動したとき$/.test(first) ? 'はい' : first, 'はい');

  const none = run([fixture, '--panel', '5', '--q', 'あるはずのない言葉']);
  check('無いときは、そう言う', /見つかりませんでした/.test(none) ? 'はい' : 'いいえ', 'はい');

  // かたまりの見出しでも見つかる
  const title = run([fixture, '--panel', '5', '--q', 'ランダムトーク']);
  check('かたまりの見出しでも見つかる',
    (title.match(/^row search\.hit\.\d+ /gm) || []).length >= 1 ? 'はい' : 'いいえ', 'はい');
}

// ---------------------------------------------------- 4. ためすたな
console.log(`${C.dim}-- ためすたな${C.off}`);
{
  const parity = path.join(root, 'shiori', 'test', 'parity', 'ghost.json');
  const said = run([parity, '--panel', '0']);
  check('かたまりがならぶ',
    (said.match(/^row run\.go\.\d+ /gm) || []).length >= 10 ? 'はい' : 'いいえ', 'はい');
  check('まだ動かしていなければ、何も出ない',
    /さくらスクリプト/.test(said) ? 'いいえ' : 'はい', 'はい');

  // 押すと、栞そのものでブロックが動きます。
  // 同じものを preview_host.exe でも動かして、出てきたものをくらべます。
  const ran = run([parity, '--panel', '0', '--click', 'run.go.0']);
  const lines = ran.split(/\r?\n/);
  const head = lines.findIndex((l) => /^head .* さくらスクリプト$/.test(l));
  check('動かすと、さくらスクリプトが出る', head >= 0 ? 'はい' : 'いいえ', 'はい');

  const out = [];
  for (let i = head + 2; i < lines.length; i++) {
    const m = lines[i].match(/^text - \([^)]*\)\s+\d+x\d+\s+(.*)$/);
    if (!m) break;
    out.push(m[1]);
  }
  const got = out.join('');

  const previewHost = path.join(root, 'studio', 'test', 'preview_host.exe');
  if (fs.existsSync(previewHost)) {
    let want = '';
    try {
      const said2 = execFileSync(previewHost, [parity, 'p01'], { encoding: 'utf8' });
      want = ((said2.match(/^Value: (.*)$/m) || [])[1] || '').replace(/\r$/, '');
    } catch (e) { want = '(preview_host を動かせませんでした)'; }
    check('栞そのもので動かした結果と同じ', got, want);
  } else {
    check('それらしいものが出る', /いちぎょうめ/.test(got) ? 'はい' : got, 'はい');
  }
}

// ------------------------------------------- 4.5 SSP に送るところ
// ここを走らせる機械で SSP が動いているとはかぎらないので、
// 「動いていないときに、ちゃんとそう言うか」を見ます。
console.log(`${C.dim}-- SSP に送るところ${C.off}`);
{
  const said = run([fixture, '--panel', '0']);
  check('SSP のボタンがそろう',
    ['ssp.check', 'ssp.say', 'ssp.event', 'ssp.comm', 'ssp.forget']
      .filter((id) => said.includes(`button ${id} `)).length, 5);

  const look = run([fixture, '--panel', '0', '--click', 'ssp.check']);
  check('様子を言う', /hint - .*SSP (が|は)/.test(look) ? 'はい' : 'いいえ', 'はい');

  // SSP が動いていないときは、送らずにそう言うこと
  const say = run([fixture, '--panel', '0', '--click', 'ssp.say']);
  const line = (say.match(/^text - .*?\d+x\d+\s+(.*)$/m) || [])[1] || '';
  check('動いていなければ、送らずに言う',
    /受けつけていません|先に、かたまりを動かして|送れませんでした/.test(line) ? 'はい' : line,
    'はい');

  // 「記憶を消す」は、本当に消すので、名前のかぶらないゴーストで見ます
  const lonely = path.join(tmp, 'lonely.json');
  const project = JSON.parse(fs.readFileSync(fixture, 'utf8'));
  project.meta = { name: 'なしスタジオのテスト用ゴースト' };
  fs.writeFileSync(lonely, JSON.stringify(project), 'utf8');
  const forget = run([lonely, '--panel', '0', '--click', 'ssp.forget']);
  check('消すものが無ければ、そう言う',
    /消すものはありませんでした/.test(forget) ? 'はい' : 'いいえ', 'はい');
}

// ---------------------------------------------------- 5. ゴーストのたな
console.log(`${C.dim}-- ゴーストのたな${C.off}`);
{
  const said = run([fixture, '--panel', '1']);
  check('ゴーストの欄がならぶ',
    (said.match(/^field meta\.\w+ /gm) || []).length, 8);
  check('ランダムトークの欄もある',
    (said.match(/^field settings\.\w+ /gm) || []).length, 2);
  check('自動でしゃべるの入り切り',
    /button settings\.randomTalkEnabled .*自動でしゃべる：する/.test(said) ? 'する' : said,
    'する');

  const named = run([fixture, '--panel', '1', '--click', 'meta.name', 'ためしゴースト']);
  const j1 = jsonOf(named);
  check('ゴースト名を打ちかえられる', j1.meta?.name, 'ためしゴースト');

  const secs = run([fixture, '--panel', '1', '--click', 'settings.randomTalkInterval', '300']);
  const j2 = jsonOf(secs);
  check('しゃべる間隔は数で入る', typeof j2.settings?.randomTalkInterval, 'number');
  check('しゃべる間隔の中身', j2.settings?.randomTalkInterval, 300);

  const off = run([fixture, '--panel', '1', '--click', 'settings.randomTalkEnabled']);
  const j3 = jsonOf(off);
  check('自動でしゃべるを切れる', j3.settings?.randomTalkEnabled, false);
  check('切ったら見た目も変わる',
    /button settings\.randomTalkEnabled .*自動でしゃべる：しない/.test(off) ? 'はい' : 'いいえ',
    'はい');
}

// ---------------------------------------------------- 6. 書き出しのたな
console.log(`${C.dim}-- 書き出しのたな${C.off}`);
{
  const said = run([fixture, '--panel', '7']);
  check('SSP に入れるボタンもある',
    /button ssp\.install /.test(said) ? 'はい' : 'いいえ', 'はい');
  check('出す先とボタンがある',
    /field export\.dir /.test(said) && /button export\.folder /.test(said)
      && /button export\.nar /.test(said) ? 'はい' : 'いいえ', 'はい');

  // 書き出しには栞が要ります。となりに置いてから動かします。
  const dll = path.join(root, 'shiori', 'dist', 'nashi.dll');
  const beside = path.join(root, 'studio', 'test', 'nashi.dll');
  if (fs.existsSync(dll)) {
    fs.copyFileSync(dll, beside);
    const out = fs.mkdtempSync(path.join(os.tmpdir(), 'nashi-out-'));
    try {
      const done = run([fixture, '--panel', '7', '--dir', out, '--click', 'export.folder']);
      check('書き出せた', /書き出しました/.test(done) ? 'はい' : done.trim(), 'はい');

      const folder = fs.readdirSync(out)[0];
      const files = fs.readdirSync(path.join(out, folder, 'ghost', 'master'));
      check('栞と設計図が入っている',
        files.includes('nashi.dll') && files.includes('ghost.json')
          && files.includes('descript.txt') ? 'はい' : files.join(','), 'はい');

      const nar = run([fixture, '--panel', '7', '--dir', out, '--click', 'export.nar']);
      check('.nar にもまとめられる', /書き出しました/.test(nar) ? 'はい' : nar.trim(), 'はい');
      check('.nar ができている',
        fs.readdirSync(out).some((f) => f.endsWith('.nar')) ? 'はい' : 'いいえ', 'はい');
    } finally {
      fs.rmSync(out, { recursive: true, force: true });
      fs.rmSync(beside, { force: true });
    }
  } else {
    console.log(`${C.dim}  ――  nashi.dll が無いので、書き出しは飛ばします${C.off}`);
  }
}

// ---------------------------------------------------- 7. 立ち絵のたな
console.log(`${C.dim}-- 立ち絵のたな${C.off}`);
{
  const said = run([fixture, '--panel', '2']);
  check('立ち絵の枠が 6 つ',
    (said.match(/^image shell\.pick\.\d+ /gm) || []).length, 6);
  check('番号のならび',
    (said.match(/^image shell\.pick\.(\d+) /gm) || [])
      .map((l) => l.match(/(\d+)/)[1]).join(','), '0,1,2,10,11,12');
  check('色の欄が 5 つ', (said.match(/^color shell\.\w+ /gm) || []).length, 5);
  check('はじめは、どれも色から作る',
    /button shell\.clear\./.test(said) ? 'いいえ' : 'はい', 'はい');

  // 色を打ちかえる
  const dyed = run([fixture, '--panel', '2', '--click', 'shell.sakuraColor', '#123456']);
  const j1 = jsonOf(dyed);
  check('髪の色を打ちかえられる', j1.shell?.sakuraColor, '#123456');

  // バルーンの入り切り
  const bal = run([fixture, '--panel', '2', '--click', 'shell.balloonEnabled']);
  const j2 = jsonOf(bal);
  check('バルーンを作るようにできる', j2.shell?.balloonEnabled, true);
  check('入れたら見た目も変わる',
    /バルーンも作る：する/.test(bal) ? 'はい' : 'いいえ', 'はい');
}

// 用意した絵を割りあてたときは、それを出す
{
  const withImage = path.join(tmp, 'shell.json');
  const png = path.join(tmp, 'sakura.png');

  // 見本の PNG は、その場で作ります（png_host が読めるものと同じ組みかた）
  const zlib = require('zlib');
  const chunk = (type, body) => {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(body.length);
    const head = Buffer.concat([Buffer.from(type, 'ascii'), body]);
    const crc = Buffer.alloc(4);
    crc.writeInt32BE(crc32(head) | 0);
    return Buffer.concat([len, head, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(4, 0);
  ihdr.writeUInt32BE(3, 4);
  ihdr[8] = 8; ihdr[9] = 6;
  const rows = [];
  for (let y = 0; y < 3; y++) {
    const r = Buffer.alloc(1 + 4 * 4);
    for (let x = 0; x < 4; x++) r.set([200, 30, 40, 255], 1 + x * 4);
    rows.push(r);
  }
  fs.writeFileSync(png, Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(Buffer.concat(rows))),
    chunk('IEND', Buffer.alloc(0)),
  ]));

  const project = JSON.parse(fs.readFileSync(fixture, 'utf8'));
  project.shell = { images: [{ id: 0, path: png, name: 'sakura.png' }] };
  fs.writeFileSync(withImage, JSON.stringify(project), 'utf8');

  const said = run([withImage, '--panel', '2']);
  check('割りあてた絵の道が出る',
    /^image shell\.pick\.0 .* = /m.test(said) ? 'はい' : 'いいえ', 'はい');
  check('はずすボタンが出る',
    (said.match(/^button shell\.clear\.\d+ /gm) || []).length, 1);

  const off = run([withImage, '--panel', '2', '--click', 'shell.clear.0']);
  const j = jsonOf(off);
  check('はずせる', (j.shell.images || []).length, 0);

  // 絵が読めているか。読んで、描いて、また読む、まで通します。
  // （見本の PNG は「まっ赤」1 色なので、その色が画面に出ていれば通っています）
  const shot = path.join(tmp, 'shell.png');
  const shotSaid = run([withImage, '--window', shot, '900', '760', '2']);
  check('絵つきでも描ける', /画面\s+900 x 760/.test(shotSaid) ? 'はい' : shotSaid.trim(), 'はい');

  const pngHost = path.join(root, 'studio', 'test', 'png_host.exe');
  if (fs.existsSync(pngHost) && fs.existsSync(shot)) {
    // 立ち絵の枠は、たなの左はしから 4px のところ。窓は 900 幅、たなは 320 幅。
    const spot = (said.match(/^image shell\.pick\.0 \(\s*\d+,\s*(\d+)\)/m) || [])[1];
    const x = 900 - 320 + 12 + 8;          // たなの左 ＋ 余白 ＋ 枠の中
    const y = Number(spot) + 20;
    let pixel = '';
    try {
      pixel = execFileSync(pngHost, ['--pixel', shot, String(x), String(y)],
        { encoding: 'utf8' }).trim();
    } catch (e) {
      // 作りたての exe は、スマートアプリコントロールに止められることがあります
      if (String(e.code) === 'UNKNOWN'
          || /Application Control|アプリケーション制御/.test(String(e.message))) {
        console.log(`${C.dim}  ――  png_host.exe を起動できませんでした`
          + `（スマートアプリコントロール）。色の確かめは飛ばします。${C.off}`);
        pixel = '';
      } else {
        pixel = String(e.message);
      }
    }
    if (pixel) check('割りあてた絵の色が、画面に出ている', pixel, '200 30 40 255');
  }
}

// ---------------------------------------------------- 8. うごきのたな
console.log(`${C.dim}-- うごきのたな${C.off}`);
{
  const empty = run([fixture, '--panel', '3']);
  check('はじめは、うごきが無い',
    /まだ、うごきはありません/.test(empty) ? 'はい' : 'いいえ', 'はい');

  const one = run([fixture, '--panel', '3', '--click', 'anim.add']);
  const j1 = jsonOf(one);
  check('うごきを作れる', j1.animations?.length, 1);
  check('作ったうごきの中身',
    `${j1.animations?.[0]?.id}/${j1.animations?.[0]?.interval}/${j1.animations?.[0]?.every}`,
    '0/never/4');
  check('きっかけは、えらぶ欄',
    /choice anim\.0\.interval .* = 呼んだときだけ/.test(one) ? 'はい' : 'いいえ', 'はい');
  check('こまと当たり判定を足すボタンがある',
    /button anim\.0\.pattern\.add /.test(one) && /button anim\.0\.area\.add /.test(one)
      ? 'はい' : 'いいえ', 'はい');

  // うごきが 1 つある ghost.json を作って、その先を見ます
  const withAnim = path.join(tmp, 'anim.json');
  const project = JSON.parse(fs.readFileSync(fixture, 'utf8'));
  project.animations = [{
    id: 3, base: 0, interval: 'always', every: 4,
    patterns: [
      { surface: 5, wait: 120, method: 'overlay', x: 10, y: 20 },
      { surface: 6, wait: 80, method: 'base', x: 0, y: 0 },
    ],
    collisions: [{ name: 'Head', shape: 'rect', x1: 1, y1: 2, x2: 3, y2: 4 }],
  }];
  fs.writeFileSync(withAnim, JSON.stringify(project), 'utf8');

  const said = run([withAnim, '--panel', '3']);
  check('こまがならぶ',
    (said.match(/^field anim\.0\.pattern\.\d+\.surface /gm) || []).length, 2);
  check('重ねかたは、えらぶ欄で見出しが出る',
    /choice anim\.0\.pattern\.0\.method .* = かさねる$/m.test(said) ? 'はい' : 'いいえ', 'はい');
  check('きっかけの見出し',
    (said.match(/^choice anim\.0\.interval .* = (.*)$/m) || [])[1], 'いつも');
  check('当たり判定がならぶ',
    (said.match(/^field anim\.0\.area\.0\.\w+ /gm) || []).length, 5);

  // 打ちかえ
  const w = run([withAnim, '--panel', '3', '--click', 'anim.0.pattern.0.wait', '250']);
  const j2 = jsonOf(w);
  check('こまの待ち時間を打ちかえられる', j2.animations?.[0]?.patterns?.[0]?.wait, 250);
  check('数として入る', typeof j2.animations?.[0]?.patterns?.[0]?.wait, 'number');

  const m = run([withAnim, '--panel', '3', '--click', 'anim.0.pattern.0.method', 'reduce']);
  const j3 = jsonOf(m);
  check('重ねかたをえらべる', j3.animations?.[0]?.patterns?.[0]?.method, 'reduce');

  const nm = run([withAnim, '--panel', '3', '--click', 'anim.0.area.0.name', 'かお']);
  const j4 = jsonOf(nm);
  check('当たり判定の名前を打ちかえられる', j4.animations?.[0]?.collisions?.[0]?.name, 'かお');

  // 足す・けす
  const addP = run([withAnim, '--panel', '3', '--click', 'anim.0.pattern.add']);
  const j5 = jsonOf(addP);
  check('こまを足せる', j5.animations?.[0]?.patterns?.length, 3);

  const delP = run([withAnim, '--panel', '3', '--click', 'anim.0.pattern.0.del']);
  const j6 = jsonOf(delP);
  check('こまをけせる', (j6.animations?.[0]?.patterns || []).map((x) => x.surface).join(','), '6');

  const addA = run([withAnim, '--panel', '3', '--click', 'anim.0.area.add']);
  const j7 = jsonOf(addA);
  check('当たり判定を足せる', j7.animations?.[0]?.collisions?.length, 2);

  const del = run([withAnim, '--panel', '3', '--click', 'anim.0.del']);
  const j8 = jsonOf(del);
  check('うごきをけせる', j8.animations?.length, 0);

  // うごきが 2 つ以上あるとき、目じるしがちゃんと分かれているか
  {
    const two = JSON.parse(JSON.stringify(project));
    two.animations.push({
      id: 9, base: 0, interval: 'never', every: 4,
      patterns: [{ surface: 99, wait: 999, method: 'base', x: 0, y: 0 }],
      collisions: [],
    });
    const twoFile = path.join(tmp, 'two.json');
    fs.writeFileSync(twoFile, JSON.stringify(two), 'utf8');

    const said2 = run([twoFile, '--panel', '3']);
    check('うごきが 2 つならぶ',
      (said2.match(/^head - .* うごき \d+$/gm) || []).length, 2);
    check('2 つめの目じるしは anim.1',
      /field anim\.1\.pattern\.0\.surface .* = 99/.test(said2) ? 'はい' : 'いいえ', 'はい');

    // 2 つめのこまを打ちかえても、1 つめは変わらないこと
    const w2 = run([twoFile, '--panel', '3', '--click', 'anim.1.pattern.0.wait', '111']);
    const j = jsonOf(w2);
    check('2 つめのこまだけが変わる',
      `${j.animations?.[0]?.patterns?.[0]?.wait}/${j.animations?.[1]?.patterns?.[0]?.wait}`, '120/111');

    // 2 つめをけしても、1 つめは残ること
    const d2 = run([twoFile, '--panel', '3', '--click', 'anim.1.del']);
    const jd = jsonOf(d2);
    check('けすのも、えらんだほうだけ', (jd.animations || []).map((a) => a.id).join(','), '3');
  }

  // 多角形にすると、かどのならびを打ちこむ欄になる
  const poly = JSON.parse(JSON.stringify(project));
  poly.animations[0].collisions[0].shape = 'polygon';
  poly.animations[0].collisions[0].points = '0,0 10,0 10,10';
  const polyFile = path.join(tmp, 'poly.json');
  fs.writeFileSync(polyFile, JSON.stringify(poly), 'utf8');
  const polySaid = run([polyFile, '--panel', '3']);
  check('多角形なら、かどのならびを出す',
    /field anim\.0\.area\.0\.points .* = 0,0 10,0 10,10/.test(polySaid) ? 'はい' : 'いいえ',
    'はい');
}

// ---------------------------------------------------- 9. たなの切りかえ
console.log(`${C.dim}-- たなの切りかえ${C.off}`);
{
  const names = [];
  for (let i = 0; i < 9; i++) {
    names.push((run([fixture, '--panel', String(i)]).match(/^たな (.*)$/m) || [])[1]);
  }
  check('たなは 9 つ', names.join(','),
    'ためす,ゴースト,立ち絵,うごき,変数,さがす,チェック,書き出し,ヘルプ');

  const help = run([fixture, '--panel', '8']);
  check('ヘルプに、つかいかたが出る',
    /つかいかた/.test(help) && /Ctrl\+S/.test(help) ? 'はい' : 'いいえ', 'はい');
  check('ヘルプに、たなの案内が出る',
    /チェック あぶないところを言います/.test(help) ? 'はい' : 'いいえ', 'はい');
}

// ---------------------------------------------------------------------- 結果
try { fs.rmSync(tmp, { recursive: true, force: true }); } catch (e) { /* 消せなくても構わない */ }

console.log('');
if (bad) {
  console.log(`${C.red}[たな] ${bad} か所ちがいます。${C.off}`);
  process.exit(1);
}
console.log(`${C.green}[たな] 右の作業だなは期待どおりです。${C.off}`);
