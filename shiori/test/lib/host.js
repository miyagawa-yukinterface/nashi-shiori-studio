/* テスト用の道具箱 — test_host.exe を動かして、応答をほどく
 *
 * parity.js（プレビューとの一致テスト）と behavior.js（栞のふるまいテスト）で共用します。
 */
'use strict';

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const root = path.resolve(__dirname, '..', '..', '..');
const dllSrc = path.join(root, 'shiori', 'dist', 'nashi.dll');
const testHost = path.join(root, 'shiori', 'dist', 'test_host.exe');

const COLOR = { red: '\x1b[31m', green: '\x1b[32m', dim: '\x1b[2m', off: '\x1b[0m' };

function requireBuild() {
  if (!fs.existsSync(testHost)) {
    throw new Error('test_host.exe がありません。先に .\\build.ps1 -Test を実行してください。');
  }
  if (!fs.existsSync(dllSrc)) throw new Error(`nashi.dll がありません: ${dllSrc}`);
}

/** 栞は自分と同じフォルダの ghost.json を読むので、DLL を置いて、前回の保存を消す */
function prepare(dir) {
  requireBuild();
  fs.copyFileSync(dllSrc, path.join(dir, 'nashi.dll'));
  // テスト用のおそい SAORI も、ゴーストのフォルダに置く（あれば）
  const saori = path.join(root, 'shiori', 'dist', 'slow_saori.dll');
  if (fs.existsSync(saori)) fs.copyFileSync(saori, path.join(dir, 'slow_saori.dll'));
  for (const junk of ['nashi_save.json', 'nashi_debug.txt']) {
    const p = path.join(dir, junk);
    if (fs.existsSync(p)) fs.unlinkSync(p);
  }
}

/**
 * イベントを順に投げて、応答を [{ value, commTo, status }] で返す。
 * specs は "OnBoot" か "OnMouseClick:0,0,0,0,Head" の形（test_host.exe と同じ）。
 */
function run(dir, specs) {
  prepare(dir);
  for (const spec of specs) {
    if (spec.includes(' ')) throw new Error(`Reference に空白は使えません（test_host の都合）: ${spec}`);
  }
  const raw = execFileSync(testHost, [dir, ...specs], { encoding: 'utf8', maxBuffer: 1 << 24 });

  const out = [];
  let cur = null;
  for (const rawLine of raw.split('\n')) {
    // 応答そのものが \r\n を持っていて、それを printf がテキストモードで書くので
    // 行末が \r\r\n になる。まとめて落とす。
    const line = rawLine.replace(/\r+$/, '');
    if (line.startsWith('---- ')) {
      cur = { value: '', commTo: '', status: '', age: '' };
      out.push(cur);
      continue;
    }
    if (!cur) continue;
    if (line.startsWith('SHIORI/3.0 ')) cur.status = line.slice(11);
    else if (line.startsWith('Value: ')) cur.value = line.slice(7);
    else if (line.startsWith('Reference0: ')) cur.commTo = line.slice(12);
    else if (line.startsWith('Age: ')) cur.age = line.slice(5);
  }
  return out;
}

/**
 * "*回数" を付けた spec は test_host が繰り返すが、応答が空の回は表示しない。
 * 繰り返しの結果をきちんと数えたいときは、この形で展開して投げる。
 */
function repeat(spec, times) {
  return new Array(times).fill(spec);
}

module.exports = { root, run, prepare, repeat, COLOR, testHost, dllSrc };
