/* なしスタジオ - 立ち絵とうごき（「ゴースト」タブ）と、ステージの再生
 *
 * シェルの絵の割りあて、SERIKO のうごきの編集、
 * さくらスクリプトを受けとって吹き出しと立ち絵を動かすところ。
 *
 * app.js より**あとに**読みこまれるので、app.js の中身は N.App 経由で使います。
 * 逆に app.js からここを呼ぶときは、読みこみ順の都合で N.Shell.xxx() の形になります。
 */
'use strict';

(function (N) {

  const Model = N.Model;
  const Render = N.Render;
  const App = N.App;
  const $ = N.$;
  const Player = N.Player;
  const api = App.api;
  const sleep = App.sleep;
  const balloonOf = App.balloonOf;

  // ------------------------------------------------------------ 立ち絵の絵
  // 標準の 6 枚。ここに無い番号も「追加」でふやせる。
  const SHELL_SLOTS = [
    [0, 'さくら／通常'], [1, 'さくら／笑顔'], [2, 'さくら／驚き'],
    [10, 'うにゅう／通常'], [11, 'うにゅう／笑顔'], [12, 'うにゅう／驚き'],
  ];

  function shellImages() {
    const sh = Model.project.shell || {};
    if (!Array.isArray(sh.images)) sh.images = [];
    return sh.images;
  }

  function shellImageOf(surfaceId) {
    return shellImages().find((x) => Number(x.id) === Number(surfaceId)) || null;
  }

  // 仮シェルは exe 側で描いてもらう。色を URL に入れてあるので、
  // 色を変えれば URL も変わり、そのまま描き直しになる。
  function shellUrl(surfaceId) {
    const img = shellImageOf(surfaceId);
    if (img && img.path) {
      return '/api/shell/file?path=' + encodeURIComponent(img.path) +
             '&v=' + encodeURIComponent(img.stamp || '');
    }
    const s = Model.project.shell || {};
    const kero = surfaceId >= 10;
    const hair = (kero ? s.keroColor : s.sakuraColor) || (kero ? '#8fd18a' : '#f08cae');
    const cloth = (kero ? s.keroCloth : s.sakuraCloth) || (kero ? '#e8b45c' : '#6e82c8');
    return '/api/shell?id=' + surfaceId +
           '&hair=' + encodeURIComponent(hair) +
           '&cloth=' + encodeURIComponent(cloth);
  }

  function setSurface(who, surfaceId) {
    const img = document.getElementById('chara-' + (who === 1 ? 1 : 0));
    if (!img) return;
    // 用意した画像があればその番号をそのまま使う。
    // 仮シェルしか無い番号は 0/1/2（さくら）と 10/11/12（うにゅう）に丸める。
    let id = Number(surfaceId) || 0;
    if (!shellImageOf(id)) {
      if (who === 1 && id < 10) id += 10;
      if ([0, 1, 2, 10, 11, 12].indexOf(id) < 0) id = who === 1 ? 10 : 0;
    }
    const url = shellUrl(id);
    if (img.getAttribute('src') !== url) img.setAttribute('src', url);
  }

  // ゴーストタブの見本と、ためすタブの立ち絵をまとめて描き直す
  function refreshShell() {
    setSurface(0, 0);
    setSurface(1, 10);
    for (const id of [0, 1, 2, 10]) {
      const img = document.getElementById('shell-prev-' + id);
      if (img) img.setAttribute('src', shellUrl(id));
    }
    renderShellSlots();
    renderAnimList();
  }

  // うごき（SERIKO のアニメーション）
  const ANIM_INTERVALS = [
    ['呼んだときだけ', 'never'],
    ['いつも', 'always'],
    ['ときどき', 'sometimes'],
    ['まれに', 'rarely'],
    ['◯秒に一度くらい', 'random'],
    ['◯回しゃべるごと', 'talk'],
    ['起動時に一度だけ', 'runonce'],
  ];

  // こまの重ねかた（SERIKO の描画メソッド）。名前は surfaces.txt にそのまま出ます。
  const ANIM_METHODS = [
    ['かさねる', 'overlay'],
    ['かさねる（速い）', 'overlayfast'],
    ['土台をとりかえる', 'base'],
    ['すきとおりごと上書き', 'replace'],
    ['すきまだけ重ねる', 'interpolate'],
    ['すきとおりを無視する', 'asis'],
    ['すきとおりをけずる', 'reduce'],
    ['位置だけ動かす', 'move'],
    ['かけあわせ', 'blend-multiply'],
    ['スクリーン', 'blend-screen'],
    ['オーバーレイ', 'blend-overlay'],
    ['足しあわせ', 'blend-add'],
    ['ほかのうごきを動かす', 'start'],
    ['ほかのうごきを止める', 'stop'],
    ['動く絵を差しこむ', 'import'],
  ];

  // 当たり判定のかたち
  const ANIM_SHAPES = [
    ['四角', 'rect'],
    ['だ円', 'ellipse'],
    ['丸', 'circle'],
    ['多角形', 'polygon'],
  ];

  function renderAnimList() {
    const box = $('#anim-list');
    if (!box) return;
    const anims = Model.project.animations || [];
    box.textContent = '';
    if (!anims.length) {
      box.appendChild(Render.div('hint', 'まだうごきがありません。下のボタンで作れます。'));
      return;
    }

    anims.forEach((a, idx) => {
      const row = Render.div('anim-row');

      const head = Render.div('anim-head');
      head.appendChild(Render.el('span', 'anim-id', `うごき ${a.id}`));

      const mk = (label, value, onInput, opts) => {
        const f = Render.el('label', 'field row');
        f.appendChild(Render.el('span', null, label));
        const inp = Render.el('input');
        inp.type = (opts && opts.type) || 'number';
        inp.value = value;
        if (opts && opts.min != null) inp.min = String(opts.min);
        inp.addEventListener('change', () => {
          const snap = Model.clone(Model.project);
          onInput(inp.value);
          Model.pushUndo(snap);
          renderAnimList();
        });
        f.appendChild(inp);
        return f;
      };

      head.appendChild(mk('番号', a.id, (v) => { a.id = Math.max(0, Math.min(127, Number(v) || 0)); },
        { min: 0 }));
      head.appendChild(mk('どの立ち絵に付ける', a.base, (v) => { a.base = Number(v) || 0; }, { min: 0 }));

      const sel = Render.el('select');
      for (const [label, value] of ANIM_INTERVALS) {
        const o = Render.el('option', null, label);
        o.value = value;
        sel.appendChild(o);
      }
      sel.value = a.interval;
      sel.addEventListener('change', () => {
        const snap = Model.clone(Model.project);
        a.interval = sel.value;
        Model.pushUndo(snap);
        renderAnimList();
      });
      const selField = Render.el('label', 'field row');
      selField.appendChild(Render.el('span', null, 'いつ動く'));
      selField.appendChild(sel);
      head.appendChild(selField);

      if (a.interval === 'random' || a.interval === 'talk') {
        head.appendChild(mk(a.interval === 'random' ? '秒' : '回', a.every,
          (v) => { a.every = Math.max(1, Number(v) || 1); }, { min: 1 }));
      }

      const del = Render.el('button', 'btn tiny', '消す');
      del.addEventListener('click', () => {
        Model.act(() => { Model.project.animations.splice(idx, 1); });
        renderAnimList();
      });
      head.appendChild(del);
      row.appendChild(head);

      // パラパラの中身（どの絵を何ミリ秒）
      const pats = Render.div('anim-pats');
      // えらんだものを owner[key] に入れる小さなドロップダウン
      const pick = (label, list, value, onPick) => {
        const f = Render.el('label', 'field row');
        f.appendChild(Render.el('span', null, label));
        const s = Render.el('select');
        for (const [text, v] of list) {
          const o = Render.el('option', null, text);
          o.value = v;
          s.appendChild(o);
        }
        s.value = value;
        s.addEventListener('change', () => {
          const snap = Model.clone(Model.project);
          onPick(s.value);
          Model.pushUndo(snap);
          renderAnimList();
        });
        f.appendChild(s);
        return f;
      };

      a.patterns.forEach((p, k) => {
        const pr = Render.div('anim-pat');
        pr.appendChild(Render.el('span', 'anim-step', `${k + 1}`));
        pr.appendChild(pick('重ねかた', ANIM_METHODS, p.method || 'base',
          (v) => { p.method = v; }));
        // 重ねかたによって、必要な欄が変わる
        if (p.method === 'start' || p.method === 'stop') {
          pr.appendChild(mk('うごき番号', p.surface,
            (v) => { p.surface = Math.max(0, Math.min(127, Number(v) || 0)); }, { min: 0 }));
        } else {
          if (p.method === 'import') {
            pr.appendChild(mk('動く絵', p.file || '', (v) => { p.file = String(v).trim(); },
              { type: 'text' }));
          } else {
            pr.appendChild(mk('立ち絵', p.surface, (v) => { p.surface = Number(v) || 0; }, { min: 0 }));
          }
          pr.appendChild(mk('ミリ秒', p.wait, (v) => { p.wait = Math.max(0, Number(v) || 0); },
            { min: 0 }));
          // 位置ずらし。0 のままなら基準の絵とぴったり重なります。
          pr.appendChild(mk('よこ', p.x || 0, (v) => { p.x = Number(v) || 0; }));
          pr.appendChild(mk('たて', p.y || 0, (v) => { p.y = Number(v) || 0; }));
        }
        const rm = Render.el('button', 'btn tiny', '−');
        rm.title = 'このこまを消す';
        rm.addEventListener('click', () => {
          Model.act(() => { a.patterns.splice(k, 1); });
          renderAnimList();
        });
        pr.appendChild(rm);
        pats.appendChild(pr);
      });
      const addPat = Render.el('button', 'btn tiny', '＋ こまを足す');
      addPat.addEventListener('click', () => {
        Model.act(() => { a.patterns.push({ surface: a.base, wait: 200, method: 'base', x: 0, y: 0 }); });
        renderAnimList();
      });
      pats.appendChild(addPat);
      row.appendChild(pats);

      // このうごきの間だけ有効な当たり判定（animationN.collisionM）
      const cols = Render.div('anim-cols');
      (a.collisions || []).forEach((c, k) => {
        const cr = Render.div('anim-pat');
        cr.appendChild(Render.el('span', 'anim-step', 'あたり'));
        cr.appendChild(mk('名前', c.name, (v) => { c.name = String(v).trim(); }, { type: 'text' }));
        cr.appendChild(pick('かたち', ANIM_SHAPES, c.shape || 'rect', (v) => { c.shape = v; }));
        if (c.shape === 'circle') {
          cr.appendChild(mk('中心よこ', c.x1, (v) => { c.x1 = Number(v) || 0; }));
          cr.appendChild(mk('中心たて', c.y1, (v) => { c.y1 = Number(v) || 0; }));
          cr.appendChild(mk('半径', c.x2, (v) => { c.x2 = Math.max(0, Number(v) || 0); }, { min: 0 }));
        } else if (c.shape === 'polygon') {
          cr.appendChild(mk('かどの位置', c.points || '', (v) => { c.points = String(v); },
            { type: 'text' }));
          cr.appendChild(Render.el('span', 'hint', 'x,y を空白でならべます（3 つ以上）'));
        } else {
          cr.appendChild(mk('左', c.x1, (v) => { c.x1 = Number(v) || 0; }));
          cr.appendChild(mk('上', c.y1, (v) => { c.y1 = Number(v) || 0; }));
          cr.appendChild(mk('右', c.x2, (v) => { c.x2 = Number(v) || 0; }));
          cr.appendChild(mk('下', c.y2, (v) => { c.y2 = Number(v) || 0; }));
        }
        const rm = Render.el('button', 'btn tiny', '−');
        rm.title = 'この当たり判定を消す';
        rm.addEventListener('click', () => {
          Model.act(() => { a.collisions.splice(k, 1); });
          renderAnimList();
        });
        cr.appendChild(rm);
        cols.appendChild(cr);
      });
      const addCol = Render.el('button', 'btn tiny', '＋ 当たり判定を足す');
      addCol.title = 'このうごきが動いている間だけ、ここを触れるようにします';
      addCol.addEventListener('click', () => {
        Model.act(() => {
          if (!Array.isArray(a.collisions)) a.collisions = [];
          a.collisions.push({
            name: 'Anim' + a.id, shape: 'rect',
            x1: 0, y1: 0, x2: 60, y2: 60, points: '',
          });
        });
        renderAnimList();
      });
      cols.appendChild(addCol);
      row.appendChild(cols);

      if (!a.patterns.length) {
        row.appendChild(Render.div('hint', 'こまが無いので、書き出しても動きません。'));
      }
      box.appendChild(row);
    });
  }

  function addAnimation() {
    Model.act(() => {
      const anims = Model.project.animations;
      let id = 0;
      while (anims.some((a) => a.id === id)) id++;
      anims.push({
        id, base: 0, interval: 'sometimes', every: 4,
        patterns: [
          { surface: 1, wait: 200, method: 'base', x: 0, y: 0 },
          { surface: 0, wait: 200, method: 'base', x: 0, y: 0 },
        ],
      });
    });
    renderAnimList();
  }

  function renderShellSlots() {
    const box = $('#shell-slots');
    if (!box) return;
    const images = shellImages();
    const extra = images
      .map((x) => Number(x.id))
      .filter((id) => !SHELL_SLOTS.some(([sid]) => sid === id))
      .sort((a, b) => a - b);
    const rows = SHELL_SLOTS.concat(extra.map((id) => [id, id >= 10 ? 'うにゅう' : 'さくら']));

    box.textContent = '';
    for (const [id, label] of rows) {
      const img = shellImageOf(id);
      const row = Render.div('shell-slot');

      const thumb = Render.div('thumb');
      const pic = Render.el('img');
      pic.src = shellUrl(id);
      pic.alt = label;
      thumb.appendChild(pic);
      row.appendChild(thumb);

      const info = Render.div('info');
      info.appendChild(Render.div('slot-name', `${label}（${id} 番）`));
      const file = Render.el('span', 'slot-file' + (img ? '' : ' is-auto'),
        img ? (img.name || img.path) : '自動生成');
      if (img) {
        file.title = img.path;
        // ファイルが動いた・消えたときは、ここで気づけるようにする
        pic.addEventListener('error', () => {
          file.textContent = 'ファイルが見つかりません';
          file.style.color = '#e2664a';
          thumb.textContent = '？';
        });
      }
      info.appendChild(file);
      row.appendChild(info);

      const pickBtn = Render.el('button', 'btn tiny', img ? '変える' : 'えらぶ');
      pickBtn.addEventListener('click', () => pickShellImage(id));
      row.appendChild(pickBtn);

      if (img) {
        const clear = Render.el('button', 'btn tiny', 'もどす');
        clear.title = '自動生成にもどす';
        clear.addEventListener('click', () => {
          Model.act(() => {
            const list = shellImages();
            const i = list.findIndex((x) => Number(x.id) === Number(id));
            if (i >= 0) list.splice(i, 1);
          });
          refreshShell();
        });
        row.appendChild(clear);
      }
      box.appendChild(row);
    }
  }

  async function pickShellImage(surfaceId) {
    let r;
    try {
      r = await api('/api/shell/pick', { method: 'POST', body: {} });
    } catch (e) {
      App.toast(e.message, true);
      return;
    }
    const files = r.files || [];
    if (!files.length) return;

    // 複数えらんだときは、この番号から順にあてはめる（0,1,2 / 10,11,12 の並び）
    const order = SHELL_SLOTS.map(([id]) => id);
    let at = order.indexOf(Number(surfaceId));
    Model.act(() => {
      const list = shellImages();
      files.forEach((f, n) => {
        let id = Number(surfaceId);
        if (n > 0) {
          if (at < 0 || at + n >= order.length) id = Number(surfaceId) + n;
          else id = order[at + n];
        }
        const one = { id, path: f.path, name: f.name, w: f.width, h: f.height, stamp: String(Date.now()) };
        const i = list.findIndex((x) => Number(x.id) === id);
        if (i >= 0) list[i] = one; else list.push(one);
      });
      list.sort((a, b) => Number(a.id) - Number(b.id));
    });
    refreshShell();
    App.toast(files.length > 1 ? `${files.length} 枚を割り当てました` : '画像を割り当てました');
  }

  function resetStage() {
    for (const id of ['balloon-0', 'balloon-1']) {
      const b = document.getElementById(id);
      b.querySelector('.balloon-text').textContent = '';
      b.classList.add('is-empty');
    }
    $('#balloon-0 .balloon-name').textContent = Model.project.meta.sakuraName || 'さくら';
    $('#balloon-1 .balloon-name').textContent = Model.project.meta.keroName || 'うにゅう';
    refreshShell();
  }

  async function playSakura(script) {
    const token = ++App.playToken;
    resetStage();
    if (!script) return;
    const ops = Player.parseSakura(script);
    let who = 0;
    let target = balloonOf(0);

    for (const op of ops) {
      if (token !== App.playToken) return;
      switch (op.op) {
        case 'scope':
          who = op.who;
          target = balloonOf(who);
          target.classList.remove('is-empty');
          break;
        case 'surface':
          setSurface(who, op.id);
          break;
        case 'text': {
          target.classList.remove('is-empty');
          const box = target.querySelector('.balloon-text');
          for (const ch of Array.from(op.text)) {
            if (token !== App.playToken) return;
            box.appendChild(document.createTextNode(ch));
            target.scrollTop = target.scrollHeight;
            await sleep(22);
          }
          break;
        }
        case 'newline':
          target.querySelector('.balloon-text').appendChild(document.createTextNode('\n'));
          break;
        case 'clear':
          target.querySelector('.balloon-text').textContent = '';
          break;
        case 'wait':
          await sleep(Math.min(1200, op.ms));
          break;
        case 'clickwait': {
          const mark = Render.el('span', 'balloon-wait', ' ▼');
          target.querySelector('.balloon-text').appendChild(mark);
          await Promise.race([sleep(2500), waitForStageClick()]);
          mark.remove();
          break;
        }
        case 'choice': {
          const chip = Render.el('span', 'balloon-choice', op.label);
          chip.addEventListener('click', () => {
            const fn = Model.project.scripts.find(
              (s) => (s.kind === 'function' || s.kind === 'talk') && s.name === op.id
            );
            if (fn) App.runScript(fn);
            else App.toast(`「${op.id}」というトークが見つかりません`, true);
          });
          target.querySelector('.balloon-text').appendChild(chip);
          break;
        }
        case 'end':
        case 'close':
          return;
        default:
          break;
      }
    }
  }

  function waitForStageClick() {
    return new Promise((resolve) => {
      const stage = $('#stage');
      const fn = () => { stage.removeEventListener('click', fn); resolve(); };
      stage.addEventListener('click', fn);
    });
  }

  N.Shell = { refreshShell, addAnimation, pickShellImage, resetStage, playSakura };

})(window.NASHI);
