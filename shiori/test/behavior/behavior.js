/* 栞のふるまいテスト
 *
 *   node shiori\test\behavior\behavior.js
 *
 * parity.js が「ブロック 1 つ 1 つの意味」を見るのに対して、こちらは
 * **栞だけが持っている判断**を見ます。プレビューには無い部分です。
 *
 *   ・同じイベントに複数あるとき、どれを選ぶか（しぼり込みの優先）
 *   ・ブロックが無いイベントの既定の反応
 *   ・OnClose に \- が必ず付くこと、\e が補われること
 *   ・「◯秒ごと」の間引き
 *   ・OnMouseMove を数えて OnNadeNade を作ること
 *   ・ゴースト間通信の届け先と、話しかけ合いの打ち切り
 *
 * ふるまいを変えたら、ここの期待値も直してください。
 */
'use strict';

const path = require('path');
const host = require('../lib/host');

const { red, green, dim, off } = host.COLOR;
const EMPTY = path.join(__dirname, 'empty');
const MAIN = path.join(__dirname, 'main');

// 応答 1 件に対する期待。文字列なら Value の完全一致。
//   { value } Value / { commTo } 話しかけ先 / { status } 応答の種類
//   { valueOneOf: [...] } どれか 1 つに当たればよい（既定の反応がランダムなもの）
//   null なら見ない
const scenarios = [
  {
    name: 'ブロックが無いゴーストの、既定の反応',
    dir: EMPTY,
    steps: [
      ['OnFirstBoot', '\\0\\s[0]はじめまして。\\nなしこです。\\nよろしくね。\\e'],
      ['OnBoot', '\\0\\s[0]おかえりなさい。\\e'],
      ['OnMouseDoubleClick:0,0,0,0,Head', {
        valueOneOf: ['\\0\\s[0]なに？\\e', '\\0\\s[0]呼んだ？\\e', '\\0\\s[0]どうしたの？\\e'],
      }],
      ['OnMinuteChange', { status: '204 No Content', value: '' }],
      ['OnClose', '\\0\\s[0]またね。\\-'],
    ],
  },
  {
    name: '終了のとき \\- が必ず付く／ふつうのイベントには \\e が付く',
    dir: MAIN,
    steps: [
      ['OnBoot', '\\0おきた\\e'],
      ['OnClose', '\\0ばいばい\\-'],
    ],
  },
  {
    name: 'しぼり込み — あたま用があれば、条件なしより優先される',
    dir: MAIN,
    steps: [
      ['OnNadeNade:0,0,0,0,Head', '\\0あたま\\e'],
      ['OnNadeNade:0,0,0,0,Bust', '\\0どこか\\e'],
    ],
  },
  {
    name: 'ゴースト間通信 — 細かい条件ほど先に選ばれる',
    dir: MAIN,
    steps: [
      // 相手＋言葉（いちばん細かい）
      ['OnCommunicate:みかん,ねえねえ', { value: '\\0みかんだ\\e', commTo: 'みかん' }],
      // 言葉だけ（相手が違うので、いちばん細かいものは外れる）
      ['OnCommunicate:さくら,こんにちは', { value: '\\0あいさつ\\e', commTo: '' }],
      // どれにも当てはまらないので、条件なし
      ['OnCommunicate:だれか,やあ', { value: '\\0だれか\\e', commTo: '' }],
    ],
  },
  {
    name: '話しかけ合いは 8 往復で打ち切る（しゃべりは続く）',
    dir: MAIN,
    steps: (() => {
      const out = [];
      for (let i = 1; i <= 10; i++) {
        out.push(['OnCommunicate:みかん,ねえ', {
          value: '\\0みかんだ\\e',
          commTo: i <= 8 ? 'みかん' : '',
          // 世代数は初回が 0 で、返すたびに増える。打ち切ったあとは付かない。
          age: i <= 8 ? String(i - 1) : '',
        }]);
      }
      return out;
    })(),
  },
  {
    name: '「◯秒ごと」は間引かれる（3 秒ごとなら 7 回たたいて 2 回）',
    dir: MAIN,
    steps: [
      ['OnSecondChange:0,1,1', { status: '204 No Content' }],
      ['OnSecondChange:0,1,2', { status: '204 No Content' }],
      ['OnSecondChange:0,1,3', { value: '\\01\\e' }],
      ['OnSecondChange:0,1,4', { status: '204 No Content' }],
      ['OnSecondChange:0,1,5', { status: '204 No Content' }],
      ['OnSecondChange:0,1,6', { value: '\\02\\e' }],
      ['OnSecondChange:0,1,7', { status: '204 No Content' }],
    ],
  },
  {
    name: 'マウスが同じ場所で 8 回続いたら、なでられたことにする',
    dir: MAIN,
    steps: (() => {
      const out = [];
      for (let i = 1; i <= 8; i++) {
        out.push(['OnMouseMove:0,0,0,0,Head',
          i < 8 ? { status: '204 No Content' } : { value: '\\0あたま\\e' }]);
      }
      return out;
    })(),
  },
  {
    name: '当たり判定の外は、なでたことにしない',
    dir: MAIN,
    steps: (() => {
      const out = [];
      for (let i = 1; i <= 10; i++) out.push(['OnMouseMove:0,0,0,0,', { status: '204 No Content' }]);
      return out;
    })(),
  },
  {
    name: '何も出力しないブロックは 204／選択肢の行き先が呼ばれる',
    dir: MAIN,
    steps: [
      ['OnEmptyOut', { status: '204 No Content', value: '' }],
      ['OnChoiceTest', '\\q[はい,こたえ]\\e'],
      ['OnChoiceSelect:こたえ', '\\0えらばれた\\e'],
    ],
  },
];

// ---------------------------------------------------------------------- 実行
let failed = 0;
let checked = 0;

for (const sc of scenarios) {
  let got;
  try {
    got = host.run(sc.dir, sc.steps.map((s) => s[0]));
  } catch (e) {
    console.log(`${red}  だめ ${sc.name}${off}\n        ${e.message}`);
    failed++;
    continue;
  }

  const problems = [];
  sc.steps.forEach(([spec, want], i) => {
    const res = got[i];
    if (!res) { problems.push(`${spec}: 応答がありません`); return; }
    const exp = typeof want === 'string' ? { value: want } : want;
    if (!exp) return;
    checked++;

    if (exp.valueOneOf) {
      if (!exp.valueOneOf.includes(res.value)) {
        problems.push(`${spec}\n          出た  : ${res.value}`
          + `\n          どれか: ${exp.valueOneOf.join(' / ')}`);
      }
      return;
    }
    if (exp.value !== undefined && res.value !== exp.value) {
      problems.push(`${spec}\n          出た    : ${res.value}\n          ほしい  : ${exp.value}`);
    }
    if (exp.commTo !== undefined && res.commTo !== exp.commTo) {
      problems.push(`${spec}  話しかけ先  出た: "${res.commTo}" / ほしい: "${exp.commTo}"`);
    }
    if (exp.status !== undefined && res.status !== exp.status) {
      problems.push(`${spec}  応答の種類  出た: "${res.status}" / ほしい: "${exp.status}"`);
    }
    if (exp.age !== undefined && res.age !== exp.age) {
      problems.push(`${spec}  世代数  出た: "${res.age}" / ほしい: "${exp.age}"`);
    }
  });

  if (problems.length) {
    failed++;
    console.log(`${red}  だめ ${sc.name}${off}`);
    for (const p of problems) console.log(`        ${p}`);
  } else {
    console.log(`${green}  OK  ${off}${sc.name}  ${dim}(${sc.steps.length} 回)${off}`);
  }
}

console.log('');
if (failed) {
  console.log(`${red}[behavior] ${scenarios.length} 場面中 ${failed} 場面がだめでした。${off}`);
  process.exit(1);
}
console.log(`${green}[behavior] ${scenarios.length} 場面・${checked} か所すべて期待どおりです。${off}`);
