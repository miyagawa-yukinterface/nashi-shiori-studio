/* なしスタジオ - ブロック定義
 * ここで定義した形が、そのまま ghost.json（栞が読むファイル）の形になります。
 */
'use strict';
window.NASHI = window.NASHI || {};

(function (N) {

  // ------------------------------------------------------------ categories
  N.CATEGORIES = [
    { id: 'events',    name: 'イベント', color: '#ffbf00' },
    { id: 'talk',      name: 'セリフ',   color: '#ff6680' },
    { id: 'looks',     name: '見た目',   color: '#9966ff' },
    { id: 'control',   name: '制御',     color: '#ffab19' },
    { id: 'variables', name: '変数',     color: '#ff8c1a' },
    { id: 'operators', name: '演算',     color: '#59c059' },
    { id: 'info',      name: '情報',     color: '#5cb1d6' },
  ];

  N.EVENTS = [
    ['起動したとき', 'OnBoot'],
    ['はじめて起動したとき', 'OnFirstBoot'],
    ['終了するとき', 'OnClose'],
    ['ダブルクリックされたとき', 'OnMouseDoubleClick'],
    ['クリックされたとき', 'OnMouseClick'],
    ['なでられたとき', 'OnNadeNade'],
    ['マウスが動いたとき', 'OnMouseMove'],
    ['ホイールを回されたとき', 'OnMouseWheel'],
    ['1時間ごと', 'OnHourChange'],
    ['1分ごと', 'OnMinuteChange'],
    ['ずっとくりかえす', 'OnSecondChange'],
    ['選択肢がえらばれたとき', 'OnChoiceSelect'],
    ['話しかけられたとき', 'OnCommunicate'],
    ['他のゴーストから来たとき', 'OnGhostChanged'],
    ['他のゴーストへ行くとき', 'OnGhostChanging'],
    ['キーが押されたとき', 'OnKeyPress'],
    ['サーフェスがもどったとき', 'OnSurfaceRestore'],
    ['最小化されたとき', 'OnWindowStateMinimize'],
    ['最小化からもどったとき', 'OnWindowStateRestore'],
    ['その他（自分で書く）', '__custom__'],
  ];

  // マウス系＝「どこを・だれを」でしぼり込めるイベント
  N.MOUSE_EVENTS = {
    OnMouseClick: 1, OnMouseDoubleClick: 1, OnMouseMove: 1,
    OnMouseWheel: 1, OnNadeNade: 1,
  };

  // 当たり判定の名前。仮シェルはこの名前で書き出しています。
  N.AREAS = [
    ['どこか', ''], ['あたま', 'Head'], ['かお', 'Face'],
    ['むね', 'Bust'], ['スカート', 'Skirt'], ['て', 'Hand'],
  ];
  N.WHO_ANY = [['どちらか', -1], ['さくら', 0], ['うにゅう', 1]];

  // Reference の意味（実行パネルのヒント用）
  N.EVENT_REFS = {
    OnBoot: ['シェル名'],
    OnFirstBoot: ['消滅回数'],
    OnMouseDoubleClick: ['X座標', 'Y座標', 'ホイール', '相手(0=本体/1=相方)', '当たった場所'],
    OnMouseClick: ['X座標', 'Y座標', 'ホイール', '相手', '当たった場所', 'ボタン'],
    OnMouseMove: ['X座標', 'Y座標', 'ホイール', '相手', '当たった場所'],
    OnNadeNade: ['X座標', 'Y座標', 'ホイール', '相手', 'なでられた場所'],
    OnMouseWheel: ['X座標', 'Y座標', '回した量', '相手', '当たった場所'],
    OnSecondChange: ['ウィンドウ', 'しゃべってよいか', '累計秒数'],
    OnChoiceSelect: ['えらばれたID'],
    OnKeyPress: ['押されたキー'],
    OnGhostChanged: ['前のゴースト名'],
    OnCommunicate: ['相手の名前(通信ボックスなら user)', '言われたこと'],
  };

  const WHO = [['さくら', 0], ['うにゅう', 1]];
  const WHO_ANY = N.WHO_ANY;
  const AREAS = N.AREAS;
  const YESNO_NL = [['改行する', 1], ['改行しない', 0]];

  // --------------------------------------------------------------- helpers
  const DEFS = {};
  N.BLOCKS = DEFS;

  function def(o) {
    DEFS[o.key || o.type] = o;
    o.key = o.key || o.type;
    o.parts = parseSpec(o.spec || '');
    return o;
  }

  // "%who が %text と話す" -> [{lbl}, {arg:'who'}, ...]
  function parseSpec(spec) {
    const parts = [];
    let buf = '';
    for (let i = 0; i < spec.length; i++) {
      if (spec[i] === '%' && /[a-zA-Z_]/.test(spec[i + 1] || '')) {
        let j = i + 1;
        while (j < spec.length && /[a-zA-Z0-9_]/.test(spec[j])) j++;
        if (buf) { parts.push({ lbl: buf }); buf = ''; }
        parts.push({ arg: spec.slice(i + 1, j) });
        i = j - 1;
      } else {
        buf += spec[i];
      }
    }
    if (buf) parts.push({ lbl: buf });
    return parts;
  }

  /** 同じ type でも op ちがいで見た目が変わるブロック */
  const OP_KEYED = { arith: 1, compare: 1, logic: 1 };
  N.defKey = function (block) {
    if (!block || !block.type) return null;
    if (OP_KEYED[block.type] && block.op != null) return block.type + '#' + block.op;
    return block.type;
  };
  N.getDef = function (block) { return DEFS[N.defKey(block)] || null; };

  N.createBlock = function (key) {
    const d = DEFS[key];
    if (!d) return null;
    const b = { type: d.type };
    if (d.fixed) Object.assign(b, d.fixed);
    for (const name in (d.args || {})) {
      const a = d.args[name];
      if (a.kind === 'sub') continue;
      b[name] = typeof a.def === 'function' ? a.def() : a.def;
    }
    for (const s of d.subs || []) b[s.key] = [];
    if (d.dynamic) b[d.dynamic] = [[], []];
    return b;
  };

  N.isReporter = function (block) {
    const d = N.getDef(block);
    return !!d && (d.shape === 'reporter' || d.shape === 'boolean');
  };

  // =========================================================== イベント(hat)
  def({
    type: '@event', cat: 'events', shape: 'hat',
    spec: '%event',
    args: { event: { kind: 'eventname', def: 'OnBoot' } },
    hat: true, kind: 'event',
  });
  // マウス系イベントを選ぶと、この形に変わる（どこを・だれを、で絞り込める）
  def({
    key: '@event.touch', type: '@event', cat: 'events', shape: 'hat',
    spec: '%who の %area が %event',
    args: {
      who: { kind: 'dropdown', options: WHO_ANY, def: -1 },
      area: { kind: 'dropdown', options: AREAS, def: '' },
      event: { kind: 'eventname', def: 'OnNadeNade' },
    },
    hat: true, kind: 'event',
  });
  // 「話しかけられたとき」を選ぶと、相手と内容でしぼり込める形に変わる
  def({
    key: '@event.comm', type: '@event', cat: 'events', shape: 'hat',
    spec: '%from に %contains と %event',
    args: {
      from: { kind: 'input', mode: 'text', def: '', long: true },
      contains: { kind: 'input', mode: 'text', def: '', long: true },
      event: { kind: 'eventname', def: 'OnCommunicate' },
    },
    hat: true, kind: 'event',
  });
  // 「ずっとくりかえす」を選ぶと、間隔を指定できる形に変わる
  def({
    key: '@event.every', type: '@event', cat: 'events', shape: 'hat',
    spec: '%event %everySec 秒ごと',
    args: {
      event: { kind: 'eventname', def: 'OnSecondChange' },
      everySec: { kind: 'input', mode: 'number', def: 5 },
    },
    hat: true, kind: 'event',
  });
  def({
    type: '@talk', cat: 'events', shape: 'hat',
    spec: 'ランダムトーク「%name」 えらばれやすさ %weight',
    args: {
      name: { kind: 'input', mode: 'text', def: 'トーク', long: true },
      weight: { kind: 'input', mode: 'number', def: 1 },
    },
    hat: true, kind: 'talk',
  });
  def({
    type: '@function', cat: 'events', shape: 'hat',
    spec: '「%name」がよばれたとき',
    args: { name: { kind: 'input', mode: 'text', def: 'あたらしいトーク', long: true } },
    hat: true, kind: 'function',
  });

  // ============================================================== セリフ
  def({
    type: 'say', cat: 'talk', shape: 'stack',
    spec: '%who が %text と話す %nl',
    args: {
      who: { kind: 'dropdown', options: WHO, def: 0 },
      text: { kind: 'input', mode: 'text', def: 'こんにちは。', long: true },
      nl: { kind: 'dropdown', options: YESNO_NL, def: 1 },
    },
  });
  def({
    type: 'newline', cat: 'talk', shape: 'stack',
    spec: '%count 回 改行する',
    args: { count: { kind: 'input', mode: 'number', def: 1 } },
  });
  def({ type: 'click_wait', cat: 'talk', shape: 'stack', spec: 'クリックされるまで待つ' });
  def({ type: 'clear', cat: 'talk', shape: 'stack', spec: 'バルーンの文字を消す' });
  def({
    type: 'choice', cat: 'talk', shape: 'stack',
    spec: '選択肢「%label」→ %target を出す',
    args: {
      label: { kind: 'input', mode: 'text', def: 'うん', long: true },
      target: { kind: 'funcname', def: '' },
    },
  });
  def({
    type: 'link', cat: 'talk', shape: 'stack',
    spec: 'リンク「%label」→ %url を出す',
    args: {
      label: { kind: 'input', mode: 'text', def: 'ホームページ', long: true },
      url: { kind: 'input', mode: 'text', def: 'https://', long: true },
    },
  });
  def({
    type: 'communicate', cat: 'talk', shape: 'stack',
    spec: '%who が %to に %text と話しかける',
    args: {
      who: { kind: 'dropdown', options: WHO, def: 0 },
      to: { kind: 'input', mode: 'text', def: '', long: true },
      text: { kind: 'input', mode: 'text', def: 'こんにちは。', long: true },
    },
  });
  def({
    type: 'raw', cat: 'talk', shape: 'stack',
    spec: 'さくらスクリプト %text をそのまま出す',
    args: { text: { kind: 'input', mode: 'text', def: '\\![raise,OnTest]', long: true } },
  });

  // ============================================================== 見た目
  def({
    type: 'surface', cat: 'looks', shape: 'stack',
    spec: '%who の表情を %id 番にする',
    args: {
      who: { kind: 'dropdown', options: WHO, def: 0 },
      id: { kind: 'input', mode: 'number', def: 0 },
    },
  });
  def({
    type: 'balloon', cat: 'looks', shape: 'stack',
    spec: 'バルーンを %id 番にする',
    args: { id: { kind: 'input', mode: 'number', def: 0 } },
  });
  def({
    type: 'sound', cat: 'looks', shape: 'stack',
    spec: '音 %file を鳴らす',
    args: { file: { kind: 'input', mode: 'text', def: 'sound.wav', long: true } },
  });

  // ================================================================ 制御
  def({
    type: 'wait', cat: 'control', shape: 'stack',
    spec: '%ms ミリ秒待つ',
    args: { ms: { kind: 'input', mode: 'number', def: 500 } },
  });
  def({
    type: 'if', cat: 'control', shape: 'c',
    spec: 'もし %cond なら',
    args: { cond: { kind: 'input', mode: 'bool' } },
    subs: [{ key: 'then' }],
  });
  def({
    type: 'if_else', cat: 'control', shape: 'c',
    spec: 'もし %cond なら',
    args: { cond: { kind: 'input', mode: 'bool' } },
    subs: [{ key: 'then' }, { key: 'else', label: 'そうでなければ' }],
  });
  def({
    type: 'repeat', cat: 'control', shape: 'c',
    spec: '%count 回くりかえす',
    args: { count: { kind: 'input', mode: 'number', def: 3 } },
    subs: [{ key: 'body' }],
  });
  def({
    type: 'while', cat: 'control', shape: 'c',
    spec: '%cond の間くりかえす',
    args: { cond: { kind: 'input', mode: 'bool' } },
    subs: [{ key: 'body' }],
  });
  def({
    type: 'random_one', cat: 'control', shape: 'c',
    spec: 'つぎのどれかをランダムに',
    dynamic: 'branches',
  });
  def({
    type: 'call', cat: 'control', shape: 'stack',
    spec: '%name をよぶ',
    args: { name: { kind: 'funcname', def: '' } },
  });
  def({
    type: 'talk_interval', cat: 'control', shape: 'stack',
    spec: 'ランダムトークの間隔を %sec 秒にする',
    args: { sec: { kind: 'input', mode: 'number', def: 180 } },
  });
  def({ type: 'end', cat: 'control', shape: 'cap', spec: 'ここでトークをおわる' });
  def({ type: 'close', cat: 'control', shape: 'cap', spec: 'ゴーストを終了する' });

  // ================================================================ 変数
  def({
    type: 'set', cat: 'variables', shape: 'stack',
    spec: '%name を %value にする',
    args: {
      name: { kind: 'varname', def: '' },
      value: { kind: 'input', mode: 'text', def: 0 },
    },
  });
  def({
    type: 'change', cat: 'variables', shape: 'stack',
    spec: '%name を %value ずつ変える',
    args: {
      name: { kind: 'varname', def: '' },
      value: { kind: 'input', mode: 'number', def: 1 },
    },
  });
  def({
    type: 'var', cat: 'variables', shape: 'reporter',
    spec: '%name',
    args: { name: { kind: 'varname', def: '' } },
  });

  // ================================================================ 演算
  const arith = (op, label) => def({
    key: 'arith#' + op, type: 'arith', cat: 'operators', shape: 'reporter',
    spec: '%a ' + label + ' %b', fixed: { op },
    args: {
      a: { kind: 'input', mode: 'number', def: '' },
      b: { kind: 'input', mode: 'number', def: '' },
    },
  });
  arith('+', '＋'); arith('-', '−'); arith('*', '×'); arith('/', '÷'); arith('%', 'のあまり');

  const compare = (op, label) => def({
    key: 'compare#' + op, type: 'compare', cat: 'operators', shape: 'boolean',
    spec: '%a ' + label + ' %b', fixed: { op },
    args: {
      a: { kind: 'input', mode: 'text', def: '' },
      b: { kind: 'input', mode: 'text', def: 50 },
    },
  });
  compare('<', '＜'); compare('=', '＝'); compare('>', '＞'); compare('!=', '≠');

  def({
    key: 'logic#and', type: 'logic', cat: 'operators', shape: 'boolean',
    spec: '%a かつ %b', fixed: { op: 'and' },
    args: { a: { kind: 'input', mode: 'bool' }, b: { kind: 'input', mode: 'bool' } },
  });
  def({
    key: 'logic#or', type: 'logic', cat: 'operators', shape: 'boolean',
    spec: '%a または %b', fixed: { op: 'or' },
    args: { a: { kind: 'input', mode: 'bool' }, b: { kind: 'input', mode: 'bool' } },
  });
  def({
    type: 'not', cat: 'operators', shape: 'boolean',
    spec: '%a ではない',
    args: { a: { kind: 'input', mode: 'bool' } },
  });
  def({
    type: 'random', cat: 'operators', shape: 'reporter',
    spec: '%min から %max までのランダムな数',
    args: {
      min: { kind: 'input', mode: 'number', def: 1 },
      max: { kind: 'input', mode: 'number', def: 10 },
    },
  });
  def({
    type: 'chance', cat: 'operators', shape: 'boolean',
    spec: '%a ％のかくりつ',
    args: { a: { kind: 'input', mode: 'number', def: 50 } },
  });
  def({
    type: 'join', cat: 'operators', shape: 'reporter',
    spec: '%a と %b をつなげる',
    args: {
      a: { kind: 'input', mode: 'text', def: 'こんにちは', long: true },
      b: { kind: 'input', mode: 'text', def: 'さん', long: true },
    },
  });
  def({
    type: 'contains', cat: 'operators', shape: 'boolean',
    spec: '%a に %b がふくまれる',
    args: {
      a: { kind: 'input', mode: 'text', def: '', long: true },
      b: { kind: 'input', mode: 'text', def: '', long: true },
    },
  });
  def({
    type: 'length', cat: 'operators', shape: 'reporter',
    spec: '%a の文字数',
    args: { a: { kind: 'input', mode: 'text', def: '', long: true } },
  });
  def({
    type: 'round', cat: 'operators', shape: 'reporter',
    spec: '%a を %op',
    args: {
      a: { kind: 'input', mode: 'number', def: '' },
      op: {
        kind: 'dropdown', def: 'round',
        options: [['四捨五入', 'round'], ['切り捨て', 'floor'], ['切り上げ', 'ceil'], ['絶対値に', 'abs']],
      },
    },
  });

  // ================================================================ 情報
  def({
    type: 'sys', cat: 'info', shape: 'reporter',
    spec: '%key',
    args: {
      key: {
        kind: 'dropdown', def: 'hour',
        options: [
          ['いまの時', 'hour'], ['いまの分', 'minute'], ['いまの秒', 'second'],
          ['いまの年', 'year'], ['いまの月', 'month'], ['いまの日', 'day'],
          ['いまの曜日(0=日)', 'weekday'],
          ['起動してからの秒数', 'uptime'], ['起動してからの分数', 'uptimemin'],
          ['起動した回数', 'boots'], ['今回のトーク回数', 'talks'],
          ['ゴースト名', 'ghostname'], ['シェル名', 'shellname'],
          ['話しかけてきた相手', 'commfrom'], ['言われたこと', 'commtext'],
        ],
      },
    },
  });
  def({
    type: 'ref', cat: 'info', shape: 'reporter',
    spec: 'イベントの情報 %index',
    args: {
      index: {
        kind: 'dropdown', def: 0,
        options: [['0番目', 0], ['1番目', 1], ['2番目', 2], ['3番目', 3], ['4番目', 4], ['5番目', 5]],
      },
    },
  });

  // ------------------------------------------------------------- palette
  N.PALETTE = {
    events: [
      { note: 'ゴーストの動きは、ここからはじまります。キャンバスにドラッグしてください。' },
      '@event', '@event.touch', '@event.every', '@event.comm', '@talk', '@function',
    ],
    talk: ['say', 'newline', 'click_wait', 'clear', 'choice', 'link', 'communicate', 'raw'],
    looks: ['surface', 'balloon', 'sound'],
    control: ['wait', 'if', 'if_else', 'repeat', 'while', 'random_one', 'call',
      'talk_interval', 'end', 'close'],
    variables: [{ button: 'add-var', label: '＋ 変数をつくる' }, '@vars', 'set', 'change'],
    operators: ['arith#+', 'arith#-', 'arith#*', 'arith#/', 'arith#%',
      'compare#<', 'compare#=', 'compare#>', 'compare#!=',
      'logic#and', 'logic#or', 'not', 'random', 'chance',
      'join', 'contains', 'length', 'round'],
    info: [
      { note: 'イベントの情報＝ダブルクリックの座標など、イベントごとの追加データです。' },
      'sys', 'ref',
    ],
  };

})(window.NASHI);
