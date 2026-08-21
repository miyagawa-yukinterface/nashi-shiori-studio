/*
 * なしスタジオ - 絵の読み書きのテスト
 *
 * 見本の PNG は、この場で作ります（node の zlib で縮めて、chunk を手で組みます）。
 * ファイルとして置いておかないのは、「どう作ったか」がここに書いてあるほうが
 * 直しやすいからです。
 *
 * 確かめる先は png_host.exe（pngread.cpp と inflate.cpp）です。どちらにも
 * 画面まわりが出てこないので、窓を出さずに走らせられます。
 */
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const zlib = require('zlib');
const { execFileSync } = require('child_process');

const root = path.resolve(__dirname, '..', '..');
const host = path.join(root, 'studio', 'test', 'png_host.exe');
const C = { red: '\x1b[31m', green: '\x1b[32m', dim: '\x1b[2m', off: '\x1b[0m' };

if (!fs.existsSync(host)) {
  console.error(`${C.red}[絵] png_host.exe がありません。`
    + ` 先に .\\build.ps1 -Test を実行してください。${C.off}`);
  process.exit(2);
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'nashi-png-'));
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

function run(args, allowFail) {
  try {
    return execFileSync(host, args, { encoding: 'utf8' }).trim();
  } catch (e) {
    if (allowFail) return `(だめ:${e.status})`;
    if (String(e.code) === 'UNKNOWN'
        || /Application Control|アプリケーション制御/.test(String(e.message))) {
      console.log(`${C.dim}  ――  png_host.exe を起動できませんでした`
        + `（スマートアプリコントロール）。絵のテストは飛ばします。${C.off}`);
      process.exit(0);
    }
    throw e;
  }
}

// ------------------------------------------------------------------ crc32
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
  return ((c ^ 0xffffffff) >>> 0).toString(16).toUpperCase().padStart(8, '0');
}

// ------------------------------------------------------------- PNG を組む
function chunk(type, body) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(body.length);
  const head = Buffer.concat([Buffer.from(type, 'ascii'), body]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(parseInt(crc32(head), 16) >>> 0);
  return Buffer.concat([len, head, crc]);
}

const SIG = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

/**
 * rows は「1 行ぶんのバイト列」の配列（下ごしらえの種類は filters で指定、
 * 既定は 0＝なし）。palette / trns / interlace は要るときだけ。
 */
function makePng(name, opt) {
  const { width, height, bitDepth, colorType, rows } = opt;
  const filters = opt.filters || rows.map(() => 0);

  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = bitDepth;
  ihdr[9] = colorType;
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = opt.interlace ? 1 : 0;

  const raw = Buffer.concat(rows.map((r, i) => Buffer.concat([Buffer.from([filters[i]]), r])));
  const parts = [SIG, chunk('IHDR', ihdr)];
  if (opt.palette) parts.push(chunk('PLTE', Buffer.from(opt.palette)));
  if (opt.trns) parts.push(chunk('tRNS', Buffer.from(opt.trns)));
  parts.push(chunk('IDAT', zlib.deflateSync(raw, { level: opt.level == null ? 9 : opt.level })));
  parts.push(chunk('IEND', Buffer.alloc(0)));

  const file = path.join(tmp, name + '.png');
  fs.writeFileSync(file, Buffer.concat(parts));
  return file;
}

/** 期待する RGBA を組み立てる（縦横の順は上から、左から）。 */
function rgbaOf(width, height, fn) {
  const out = Buffer.alloc(width * height * 4);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const c = fn(x, y);
      out.set(c, (y * width + x) * 4);
    }
  }
  return out;
}

// ============================================================ 1. 色の入れかた
console.log(`${C.dim}-- 色の入れかた${C.off}`);

// RGBA 8bit
{
  const w = 4, h = 3;
  const pix = (x, y) => [x * 60, y * 80, (x + y) * 30, x === 0 ? 0 : 255];
  const rows = [];
  for (let y = 0; y < h; y++) {
    const r = Buffer.alloc(w * 4);
    for (let x = 0; x < w; x++) r.set(pix(x, y), x * 4);
    rows.push(r);
  }
  const f = makePng('rgba8', { width: w, height: h, bitDepth: 8, colorType: 6, rows });
  check('RGBA 8bit', run(['--hash', f]), crc32(rgbaOf(w, h, pix)));
  check('RGBA 8bit の大きさ', run(['--info', f]).split('  ')[0], '4 x 3');
  check('RGBA 8bit の画素', run(['--pixel', f, '2', '1']), '120 80 90 255');
}

// RGB 8bit（透けは 255 でうまる）
{
  const w = 3, h = 2;
  const rows = [];
  for (let y = 0; y < h; y++) {
    const r = Buffer.alloc(w * 3);
    for (let x = 0; x < w; x++) r.set([x * 100, 255 - y * 100, 7], x * 3);
    rows.push(r);
  }
  const f = makePng('rgb8', { width: w, height: h, bitDepth: 8, colorType: 2, rows });
  check('RGB 8bit', run(['--hash', f]),
    crc32(rgbaOf(w, h, (x, y) => [x * 100, 255 - y * 100, 7, 255])));
}

// 灰 8bit
{
  const w = 4, h = 1;
  const f = makePng('gray8', {
    width: w, height: h, bitDepth: 8, colorType: 0,
    rows: [Buffer.from([0, 85, 170, 255])],
  });
  check('灰 8bit', run(['--hash', f]),
    crc32(rgbaOf(w, h, (x) => { const v = [0, 85, 170, 255][x]; return [v, v, v, 255]; })));
}

// 灰＋透け 8bit
{
  const w = 2, h = 1;
  const f = makePng('graya8', {
    width: w, height: h, bitDepth: 8, colorType: 4,
    rows: [Buffer.from([10, 0, 200, 128])],
  });
  check('灰＋透け 8bit', run(['--pixel', f, '1', '0']), '200 200 200 128');
  check('灰＋透け のすきとおり', run(['--pixel', f, '0', '0']), '10 10 10 0');
}

// 色見本 8bit
{
  const w = 3, h = 1;
  const f = makePng('pal8', {
    width: w, height: h, bitDepth: 8, colorType: 3,
    palette: [255, 0, 0, 0, 255, 0, 0, 0, 255],
    rows: [Buffer.from([2, 0, 1])],
  });
  check('色見本 8bit', run(['--hash', f]),
    crc32(rgbaOf(w, h, (x) => [[0, 0, 255, 255], [255, 0, 0, 255], [0, 255, 0, 255]][x])));
}

// 色見本 4bit（1 バイトに 2 画素）
{
  const w = 3, h = 1;
  const f = makePng('pal4', {
    width: w, height: h, bitDepth: 4, colorType: 3,
    palette: [1, 1, 1, 2, 2, 2, 3, 3, 3],
    rows: [Buffer.from([0x21, 0x00])],   // 2, 1, 0
  });
  check('色見本 4bit', run(['--hash', f]),
    crc32(rgbaOf(w, h, (x) => { const v = [3, 2, 1][x]; return [v, v, v, 255]; })));
}

// 灰 1bit（1 バイトに 8 画素。0/1 は 0/255 にのばす）
{
  const w = 8, h = 1;
  const f = makePng('gray1', {
    width: w, height: h, bitDepth: 1, colorType: 0,
    rows: [Buffer.from([0b10110001])],
  });
  check('灰 1bit', run(['--hash', f]),
    crc32(rgbaOf(w, h, (x) => {
      const v = ((0b10110001 >> (7 - x)) & 1) ? 255 : 0;
      return [v, v, v, 255];
    })));
}

// RGB 16bit（上の 8 ビットだけ使う）
{
  const w = 2, h = 1;
  const r = Buffer.alloc(w * 6);
  r.writeUInt16BE(0x1234, 0); r.writeUInt16BE(0x5678, 2); r.writeUInt16BE(0x9abc, 4);
  r.writeUInt16BE(0xffff, 6); r.writeUInt16BE(0x0000, 8); r.writeUInt16BE(0x0080, 10);
  const f = makePng('rgb16', { width: w, height: h, bitDepth: 16, colorType: 2, rows: [r] });
  check('RGB 16bit は 8bit に落とす', run(['--pixel', f, '0', '0']), '18 86 154 255');
  check('RGB 16bit の 2 つめ', run(['--pixel', f, '1', '0']), '255 0 0 255');
}

// ================================================================= 2. tRNS
console.log(`${C.dim}-- 透ける色の指定（tRNS）${C.off}`);

// 色見本の透け
{
  const f = makePng('paltrns', {
    width: 2, height: 1, bitDepth: 8, colorType: 3,
    palette: [10, 20, 30, 40, 50, 60],
    trns: [0, 128],
    rows: [Buffer.from([0, 1])],
  });
  check('色見本の透け（すきとおり）', run(['--pixel', f, '0', '0']), '10 20 30 0');
  check('色見本の透け（なかば）', run(['--pixel', f, '1', '0']), '40 50 60 128');
}

// RGB の「この色は透ける」
{
  const r = Buffer.from([1, 2, 3, 9, 9, 9]);
  const f = makePng('rgbtrns', {
    width: 2, height: 1, bitDepth: 8, colorType: 2,
    trns: [0, 1, 0, 2, 0, 3],
    rows: [r],
  });
  check('RGB の透ける色', run(['--pixel', f, '0', '0']), '1 2 3 0');
  check('RGB のふつうの色', run(['--pixel', f, '1', '0']), '9 9 9 255');
}

// 灰の「この明るさは透ける」
{
  const f = makePng('graytrns', {
    width: 2, height: 1, bitDepth: 8, colorType: 0,
    trns: [0, 77],
    rows: [Buffer.from([77, 78])],
  });
  check('灰の透ける明るさ', run(['--pixel', f, '0', '0']), '77 77 77 0');
  check('灰のふつうの明るさ', run(['--pixel', f, '1', '0']), '78 78 78 255');
}

// ====================================================== 3. 行の下ごしらえ
console.log(`${C.dim}-- 行の下ごしらえ（filter 0〜4）${C.off}`);
{
  // 同じ絵を、5 とおりの下ごしらえで作って、ぜんぶ同じに読めること。
  const w = 5, h = 5;
  const pix = (x, y) => [(x * 37 + y * 11) & 0xff, (x * 5 + y * 61) & 0xff, (x ^ y) * 20, 255];
  const want = crc32(rgbaOf(w, h, pix));

  const plain = [];
  for (let y = 0; y < h; y++) {
    const r = Buffer.alloc(w * 3);
    for (let x = 0; x < w; x++) {
      const c = pix(x, y);
      r.set([c[0], c[1], c[2]], x * 3);
    }
    plain.push(r);
  }

  function paeth(a, b, c) {
    const p = a + b - c;
    const pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
  }

  for (let type = 0; type <= 4; type++) {
    const bpp = 3;
    const rows = plain.map((cur, y) => {
      const up = y > 0 ? plain[y - 1] : Buffer.alloc(cur.length);
      const out = Buffer.alloc(cur.length);
      for (let i = 0; i < cur.length; i++) {
        const a = i >= bpp ? cur[i - bpp] : 0;
        const b = up[i];
        const c = i >= bpp ? up[i - bpp] : 0;
        let v = cur[i];
        if (type === 1) v -= a;
        else if (type === 2) v -= b;
        else if (type === 3) v -= (a + b) >> 1;
        else if (type === 4) v -= paeth(a, b, c);
        out[i] = v & 0xff;
      }
      return out;
    });
    const f = makePng('filt' + type, {
      width: w, height: h, bitDepth: 8, colorType: 2, rows,
      filters: rows.map(() => type),
    });
    check(`下ごしらえ ${type}`, run(['--hash', f]), want);
  }
}

// ============================================== 4. とびとびの並べかた（Adam7）
console.log(`${C.dim}-- とびとびの並べかた（Adam7）${C.off}`);
{
  const w = 11, h = 9;   // 8 の倍数でない大きさで、はしの扱いも見る
  const pix = (x, y) => [x * 20, y * 25, (x * y) & 0xff, 255];

  const rowsPlain = [];
  for (let y = 0; y < h; y++) {
    const r = Buffer.alloc(w * 3);
    for (let x = 0; x < w; x++) {
      const c = pix(x, y);
      r.set([c[0], c[1], c[2]], x * 3);
    }
    rowsPlain.push(r);
  }
  const flat = makePng('adam7-no', { width: w, height: h, bitDepth: 8, colorType: 2, rows: rowsPlain });

  const X0 = [0, 4, 0, 2, 0, 1, 0], Y0 = [0, 0, 4, 0, 2, 0, 1];
  const DX = [8, 8, 4, 4, 2, 2, 1], DY = [8, 8, 8, 4, 4, 2, 2];
  const rowsInter = [];
  for (let p = 0; p < 7; p++) {
    const pw = Math.ceil((w - X0[p]) / DX[p]);
    const ph = Math.ceil((h - Y0[p]) / DY[p]);
    if (pw <= 0 || ph <= 0) continue;
    for (let y = 0; y < ph; y++) {
      const r = Buffer.alloc(pw * 3);
      for (let x = 0; x < pw; x++) {
        const c = pix(X0[p] + x * DX[p], Y0[p] + y * DY[p]);
        r.set([c[0], c[1], c[2]], x * 3);
      }
      rowsInter.push(r);
    }
  }
  const inter = makePng('adam7-yes', {
    width: w, height: h, bitDepth: 8, colorType: 2, rows: rowsInter, interlace: true,
  });

  check('とびとびでない', run(['--hash', flat]), crc32(rgbaOf(w, h, pix)));
  check('とびとびでも同じ絵になる', run(['--hash', inter]), run(['--hash', flat]));
}

// ==================================================== 5. 書いて、読みなおす
console.log(`${C.dim}-- 書いて、読みなおす${C.off}`);
{
  const w = 4, h = 3;
  const rows = [];
  for (let y = 0; y < h; y++) {
    const r = Buffer.alloc(w * 4);
    for (let x = 0; x < w; x++) r.set([x * 60, y * 80, 5, x === 0 ? 0 : 200], x * 4);
    rows.push(r);
  }
  const f = makePng('round', { width: w, height: h, bitDepth: 8, colorType: 6, rows });
  check('自分で書いたものを、自分で読める', run(['--round', f]).split('  ')[0], 'おなじ');
}

// 実際に画面に出しているものでも読めること（ネイティブ版が描いた絵）
{
  const rendered = path.join(root, 'studio', 'test', 'render_out', 'p05.png');
  if (fs.existsSync(rendered)) {
    check('ネイティブ版が描いた絵も読める', run(['--round', rendered]).split('  ')[0], 'おなじ');
  } else {
    console.log(`${C.dim}  ――  render_out\\p05.png が無いので飛ばします${C.off}`);
  }
}

// ================================================ 6. deflate をほどけるか
console.log(`${C.dim}-- deflate をほどく${C.off}`);
{
  // 縮めかたを変えても、同じものが出てくること。
  // level 0 は「そのまま」、それ以外はハフマン（決まりきった／その場で作った）。
  const src = Buffer.from(
    ('なしスタジオ nashi-shiori-studio ' .repeat(40)) + ' '.repeat(50), 'utf8');
  const want = crc32(src);
  for (const level of [0, 1, 6, 9]) {
    const f = path.join(tmp, `z${level}.bin`);
    fs.writeFileSync(f, zlib.deflateSync(src, { level }));
    const said = run(['--inflate', f]);
    check(`縮めかた ${level}`, (said.match(/crc32 (\S+)/) || [])[1], want);
    check(`縮めかた ${level} の長さ`, (said.match(/長さ (\d+)/) || [])[1], String(src.length));
  }

  // 長い繰りかえし（重なった写しかたを通る）
  const rep = Buffer.alloc(9000, 0);
  for (let i = 0; i < rep.length; i++) rep[i] = i % 7;
  const fr = path.join(tmp, 'zrep.bin');
  fs.writeFileSync(fr, zlib.deflateSync(rep, { level: 9 }));
  check('長い繰りかえし', (run(['--inflate', fr]).match(/crc32 (\S+)/) || [])[1], crc32(rep));
}

// ======================================================= 7. こわれたものは断る
console.log(`${C.dim}-- こわれたものは断る${C.off}`);
{
  const w = 3, h = 2;
  const rows = [Buffer.alloc(w * 3, 1), Buffer.alloc(w * 3, 2)];
  const good = fs.readFileSync(makePng('good', {
    width: w, height: h, bitDepth: 8, colorType: 2, rows,
  }));

  function tryFile(name, buf) {
    const f = path.join(tmp, name);
    fs.writeFileSync(f, buf);
    return run(['--hash', f], true);
  }

  check('印がちがう', tryFile('sig.png', Buffer.concat([Buffer.alloc(8, 0), good.slice(8)])),
    '(だめ:3)');
  check('途中で切れている', tryFile('cut.png', good.slice(0, good.length - 20)), '(だめ:3)');
  {
    // IDAT の中身を 1 バイト書きかえる（Adler32 が合わなくなる）
    const b = Buffer.from(good);
    b[b.length - 12] ^= 0xff;
    check('中身がこわれている', tryFile('rot.png', b), '(だめ:3)');
  }
  {
    // 大きさが 0
    const b = Buffer.from(good);
    b.writeUInt32BE(0, 16);
    check('幅が 0', tryFile('zero.png', b), '(だめ:3)');
  }
  check('PNG でない', tryFile('none.png', Buffer.from('これは PNG ではありません', 'utf8')),
    '(だめ:3)');
}

// ---------------------------------------------------------------------- 結果
try { fs.rmSync(tmp, { recursive: true, force: true }); } catch (e) { /* 消せなくても構わない */ }

console.log('');
if (bad) {
  console.log(`${C.red}[絵] ${bad} か所ちがいます。${C.off}`);
  process.exit(1);
}
console.log(`${C.green}[絵] PNG の読み書きは期待どおりです。${C.off}`);
