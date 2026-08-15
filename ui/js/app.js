/* なしスタジオ - 画面全体の組み立て */
'use strict';

(function (N) {

  const Model = N.Model;
  const Render = N.Render;
  const Sim = N.Sim;
  const $ = (sel) => document.querySelector(sel);

  const App = {
    state: null,
    projectName: '',
    zoom: 1,
    category: 'events',
    sessionVars: {},
    playToken: 0,
    renderQueued: false,
  };
  N.App = App;

  // ------------------------------------------------------------------ 通知
  let toastTimer = null;
  App.toast = function (msg, isError) {
    const t = $('#toast');
    t.textContent = msg;
    t.classList.toggle('error', !!isError);
    t.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => t.classList.remove('show'), 2600);
  };

  async function api(path, options) {
    const opt = Object.assign({ headers: {} }, options || {});
    opt.headers['X-Nashi'] = '1';
    if (opt.body && typeof opt.body !== 'string') {
      opt.headers['Content-Type'] = 'application/json';
      opt.body = JSON.stringify(opt.body);
    }
    const res = await fetch(path, opt);
    const text = await res.text();
    let data = {};
    try { data = text ? JSON.parse(text) : {}; } catch { data = { raw: text }; }
    if (!res.ok) throw new Error(data.error || ('通信エラー (' + res.status + ')'));
    return data;
  }

  // ---------------------------------------------------------------- 再描画
  function scheduleRender() {
    if (App.renderQueued) return;
    App.renderQueued = true;
    requestAnimationFrame(() => {
      App.renderQueued = false;
      renderAll();
    });
  }

  function renderAll() {
    const project = Model.project;
    Render.renderWorkspace($('#canvas'), project);
    Render.renderPalette($('#palette-blocks'), App.category, project);
    $('#canvas-hint').style.display = project.scripts.length ? 'none' : '';
    renderVarList();
    renderRunTargets();
    renderCheck();
    syncMetaFields();
    $('#btn-undo').disabled = !Model.undoStack.length;
    $('#btn-redo').disabled = !Model.redoStack.length;
    updateTitle();
  }

  function updateTitle() {
    document.title = (Model.dirty ? '● ' : '') +
      (Model.project.meta.name || 'なしゴースト') + ' - なしスタジオ';
  }

  App.onLiveEdit = function () {
    updateTitle();
  };

  // ------------------------------------------------------------- パレット
  function renderCategories() {
    const list = $('#cat-list');
    list.textContent = '';
    for (const cat of N.CATEGORIES) {
      const b = Render.el('button', 'cat-btn' + (cat.id === App.category ? ' is-active' : ''));
      const dot = Render.el('span', 'cat-dot');
      dot.style.background = cat.color;
      b.appendChild(dot);
      b.appendChild(Render.el('span', null, cat.name));
      b.addEventListener('click', () => {
        App.category = cat.id;
        renderCategories();
        Render.renderPalette($('#palette-blocks'), App.category, Model.project);
      });
      list.appendChild(b);
    }
    const help = $('#help-cats');
    if (help && !help.childElementCount) {
      for (const cat of N.CATEGORIES) {
        const row = Render.div('help-cat');
        const i = Render.el('i');
        i.style.background = cat.color;
        row.appendChild(i);
        row.appendChild(Render.el('span', null, cat.name));
        help.appendChild(row);
      }
    }
  }

  // --------------------------------------------------------------- 変数UI
  function renderVarList() {
    const list = $('#var-list');
    list.textContent = '';
    if (!Model.project.variables.length) {
      list.appendChild(Render.div('hint', 'まだ変数がありません。'));
      return;
    }
    for (const v of Model.project.variables) {
      const row = Render.div('var-item');
      row.appendChild(Render.div('dot'));

      const name = Render.el('input', 'vname');
      name.value = v.name;
      let before = v.name;
      name.addEventListener('focus', () => { before = name.value; });
      name.addEventListener('change', () => {
        const ok = Model.act(() => Model.renameVariable(before, name.value.trim()));
        if (!ok) { name.value = before; App.toast('その名前は使えません', true); }
      });
      row.appendChild(name);

      const val = Render.el('input', 'vval');
      val.value = v.value;
      val.title = 'はじめの値';
      val.addEventListener('change', () => {
        Model.act(() => {
          v.value = val.value !== '' && !isNaN(Number(val.value)) ? Number(val.value) : val.value;
        });
      });
      row.appendChild(val);

      const del = Render.el('button', 'del', '✕');
      del.title = '削除';
      del.addEventListener('click', () => {
        if (!confirm(`変数「${v.name}」を削除しますか？`)) return;
        Model.act(() => Model.removeVariable(v.name));
      });
      row.appendChild(del);
      list.appendChild(row);
    }
  }

  App.promptNewVariable = function () {
    const name = prompt('あたらしい変数の名前は？', '好感度');
    if (!name) return null;
    const trimmed = name.trim();
    if (!trimmed) return null;
    let created = null;
    Model.act(() => { created = Model.addVariable(trimmed, 0); });
    if (!created) { App.toast('その名前の変数はもうあります', true); return null; }
    App.toast(`変数「${trimmed}」を作りました`);
    return trimmed;
  };

  App.promptNewFunction = function () {
    const name = prompt('あたらしいトークの名前は？', 'あたらしいトーク');
    if (!name || !name.trim()) return null;
    const trimmed = name.trim();
    if (Model.functionNames().includes(trimmed)) {
      App.toast('その名前はもう使われています', true);
      return null;
    }
    const at = viewCenter();
    Model.act(() => Model.addScript('function', { name: trimmed, x: at.x, y: at.y }));
    App.toast(`「${trimmed}」を作りました。ブロックをつなげてください`);
    return trimmed;
  };

  function viewCenter() {
    const ws = $('#workspace');
    return {
      x: Math.round((ws.scrollLeft + 80) / App.zoom),
      y: Math.round((ws.scrollTop + ws.clientHeight / 2) / App.zoom),
    };
  }

  // ------------------------------------------------------------ スクリプト
  App.duplicateScript = function (script) {
    Model.act(() => {
      const copy = Model.clone(script);
      copy.id = Model.uid('s');
      copy.x = (script.x || 0) + 40;
      copy.y = (script.y || 0) + 40;
      if (copy.kind === 'function' || copy.kind === 'talk') {
        let base = copy.name || 'コピー';
        let n = 2;
        while (Model.functionNames().includes(base + n)) n++;
        copy.name = base + n;
      }
      Model.project.scripts.push(copy);
    });
    App.toast('複製しました');
  };

  App.deleteScript = function (script) {
    const count = (script.blocks || []).length;
    if (count && !confirm(`「${Model.scriptTitle(script)}」を削除しますか？`)) return;
    Model.act(() => Model.removeScript(script));
    App.toast('削除しました');
  };

  function tidy() {
    Model.act(() => {
      let x = 60, y = 40, colW = 0;
      const order = { event: 0, talk: 1, function: 2, loose: 3 };
      const sorted = Model.project.scripts.slice().sort(
        (a, b) => (order[a.kind] || 0) - (order[b.kind] || 0)
      );
      for (const s of sorted) {
        s.x = x; s.y = y;
        y += estimateHeight(s) + 40;
        colW = Math.max(colW, 420);
        if (y > 2400) { y = 40; x += colW + 60; colW = 0; }
      }
      Model.project.scripts = sorted;
    });
  }

  function estimateHeight(script) {
    let n = 1;
    const walk = (arr) => {
      if (!Array.isArray(arr)) return;
      for (const b of arr) {
        n++;
        const d = N.getDef(b);
        if (!d) continue;
        for (const s of d.subs || []) { n += 1; walk(b[s.key]); }
        if (d.dynamic && Array.isArray(b[d.dynamic])) { b[d.dynamic].forEach((x) => { n += 1; walk(x); }); }
      }
    };
    walk(script.blocks);
    return n * 40;
  }

  // ------------------------------------------------------------- 実行/演出
  function renderRunTargets() {
    const sel = $('#run-target');
    const prev = sel.value;
    sel.textContent = '';
    for (const s of Model.project.scripts) {
      if (s.kind === 'loose') continue;
      const o = Render.el('option', null, Model.scriptTitle(s));
      o.value = s.id;
      sel.appendChild(o);
    }
    if (!sel.childElementCount) {
      const o = Render.el('option', null, '（まだブロックがありません）');
      o.value = '';
      sel.appendChild(o);
    }
    // 前に選んでいたものが残っていればそれを、なければ先頭を選ぶ
    if (prev && Model.project.scripts.some((s) => s.id === prev)) sel.value = prev;
    else sel.selectedIndex = 0;
  }

  // ためすときの Reference。マウス系はブロックで選んだ場所・相手を入れておくと、
  // 「イベントの情報」ブロックが本番と同じ値を返す。
  function refsFor(script) {
    if (!script || script.kind !== 'event') return [];
    if (script.event === 'OnCommunicate') {
      // 相手の名前・言われたこと。空欄なら、ためすとき用の仮の値を入れる。
      return [script.from || 'ほかのゴースト', script.contains || 'こんにちは'];
    }
    if (!N.MOUSE_EVENTS[script.event]) return [];
    const who = script.who >= 0 ? script.who : 0;
    return ['0', '0', '0', String(who), script.area || 'Head'];
  }

  App.runScript = function (script) {
    if (!script) return;
    $('#run-target').value = script.id;
    switchTab('run');
    const res = Sim.runScript(Model.project, script, { vars: App.sessionVars, refs: refsFor(script) });
    App.sessionVars = res.vars;
    let preview = res.script || '（なにも出力されませんでした）';
    // 話しかけ先はさくらスクリプトに出ないので、ここで見せる
    if (res.commTo) preview += `\n\n（「${res.commTo}」に届きます）`;
    $('#script-preview').textContent = preview;
    renderRunVars();
    playSakura(res.script);
  };

  function renderRunVars() {
    const box = $('#run-vars');
    box.textContent = '';
    // まだ実行していなくても、プロジェクトの初期値を見せる（何があるか分かるように）
    const shown = {};
    for (const v of Model.project.variables || []) shown[v.name] = v.value;
    Object.assign(shown, App.sessionVars);
    const names = Object.keys(shown).filter((k) => k[0] !== '@');
    if (!names.length) {
      box.appendChild(Render.div('run-var is-empty', 'まだ変数がありません'));
      return;
    }
    for (const k of names) {
      box.appendChild(Render.div('run-var', `${k}: ${Sim.toStr(shown[k])}`));
    }
  }

  /** ためすときの変数を、プロジェクトの初期値にもどす */
  function resetSessionVars() {
    App.sessionVars = {};
    renderRunVars();
    App.toast('ためすときの変数を、初期値にもどしました');
  }

  /** 書き出したゴーストが覚えている変数（nashi_save.json）を消す */
  async function resetGhostSave() {
    try {
      const r = await api('/api/save/reset', {
        method: 'POST',
        body: { project: Model.project, outDir: $('#export-dir') ? $('#export-dir').value : '' },
      });
      const n = (r.deleted || []).length;
      App.toast(n
        ? `ゴーストの記憶を消しました（${n} か所）。次に動かすと初期値からになります`
        : '消すものがありませんでした（まだ動かしていないようです）');
    } catch (e) {
      App.toast(e.message, true);
    }
  }

  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

  function balloonOf(who) {
    return document.getElementById('balloon-' + (who === 1 ? 1 : 0));
  }

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
      a.patterns.forEach((p, k) => {
        const pr = Render.div('anim-pat');
        pr.appendChild(Render.el('span', 'anim-step', `${k + 1}`));
        pr.appendChild(mk('立ち絵', p.surface, (v) => { p.surface = Number(v) || 0; }, { min: 0 }));
        pr.appendChild(mk('ミリ秒', p.wait, (v) => { p.wait = Math.max(0, Number(v) || 0); }, { min: 0 }));
        // 位置ずらし。0 のままなら基準の絵とぴったり重なります。
        pr.appendChild(mk('よこ', p.x || 0, (v) => { p.x = Number(v) || 0; }));
        pr.appendChild(mk('たて', p.y || 0, (v) => { p.y = Number(v) || 0; }));
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
    const ops = Sim.parseSakura(script);
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

  // ---------------------------------------------------------- 右クリック
  App.closeContextMenu = function () {
    $('#ctx-menu').classList.remove('open');
  };

  function openContextMenu(x, y, items) {
    const menu = $('#ctx-menu');
    menu.textContent = '';
    for (const item of items) {
      if (item === '-') { menu.appendChild(Render.div('ctx-sep')); continue; }
      const b = Render.el('button', 'ctx-item' + (item.danger ? ' danger' : ''), item.label);
      b.addEventListener('click', () => { App.closeContextMenu(); item.run(); });
      menu.appendChild(b);
    }
    menu.style.left = Math.min(x, window.innerWidth - 170) + 'px';
    menu.style.top = Math.min(y, window.innerHeight - menu.childElementCount * 34 - 20) + 'px';
    menu.classList.add('open');
  }

  function setupContextMenu() {
    document.addEventListener('contextmenu', (e) => {
      const hat = e.target.closest('#canvas .blk.hat');
      const blk = e.target.closest('#canvas .blk');
      const scriptEl = e.target.closest('#canvas .script');
      if (!scriptEl) { App.closeContextMenu(); return; }
      e.preventDefault();

      if (hat || !blk) {
        const script = scriptEl._script;
        openContextMenu(e.clientX, e.clientY, [
          { label: '▶ 実行してみる', run: () => App.runScript(script) },
          { label: '複製する', run: () => App.duplicateScript(script) },
          '-',
          { label: '削除する', danger: true, run: () => App.deleteScript(script) },
        ]);
        return;
      }

      const block = blk._blk;
      openContextMenu(e.clientX, e.clientY, [
        {
          label: '複製する',
          run: () => Model.act(() => {
            if (!Array.isArray(blk._stack)) return false;
            blk._stack.splice(blk._index + 1, 0, Model.clone(block));
          }),
        },
        {
          label: block.disabled ? '有効にする' : '一時的に無効にする',
          run: () => Model.act(() => {
            if (block.disabled) delete block.disabled;
            else block.disabled = true;
          }),
        },
        '-',
        {
          label: 'このブロックを削除',
          danger: true,
          run: () => Model.act(() => {
            if (!Array.isArray(blk._stack)) return false;
            blk._stack.splice(blk._index, 1);
          }),
        },
        {
          label: 'ここから下をすべて削除',
          danger: true,
          run: () => Model.act(() => {
            if (!Array.isArray(blk._stack)) return false;
            blk._stack.splice(blk._index);
          }),
        },
      ]);
    });
    document.addEventListener('click', (e) => {
      if (!e.target.closest('#ctx-menu')) App.closeContextMenu();
    });
  }

  // ------------------------------------------------------------ 設定パネル
  function syncMetaFields() {
    const m = Model.project.meta;
    const s = Model.project.settings;
    const sh = Model.project.shell;
    setVal('#ghost-name', m.name);
    setVal('#meta-name', m.name);
    setVal('#meta-sakura', m.sakuraName);
    setVal('#meta-kero', m.keroName);
    setVal('#meta-craftman', m.craftman);
    setVal('#meta-url', m.craftmanUrl);
    setVal('#meta-version', m.version);
    setVal('#meta-desc', m.description);
    setVal('#set-talk-interval', s.randomTalkInterval);
    setVal('#set-no-repeat', s.noRepeatCount == null ? 0 : s.noRepeatCount);
    $('#set-talk-enabled').checked = s.randomTalkEnabled !== false;
    setVal('#shell-sakura-color', sh.sakuraColor);
    setVal('#shell-sakura-cloth', sh.sakuraCloth);
    setVal('#shell-kero-color', sh.keroColor);
    setVal('#shell-kero-cloth', sh.keroCloth);
    refreshShell();
  }

  function setVal(sel, v) {
    const el = $(sel);
    if (!el) return;
    if (document.activeElement === el) return;
    el.value = v == null ? '' : v;
  }

  function bindMeta() {
    const bind = (sel, apply, onLive) => {
      const el = $(sel);
      let snap = null;
      el.addEventListener('focus', () => { snap = Model.clone(Model.project); });
      el.addEventListener('change', () => {
        if (!snap) snap = Model.clone(Model.project);
        apply(el.type === 'checkbox' ? el.checked : el.value);
        if (onLive) onLive();
        Model.pushUndo(snap);
        snap = null;
      });
      if (onLive) {
        // 色を動かしている最中もそのまま描き直す（元にもどす記録は change のときだけ）
        el.addEventListener('input', () => {
          apply(el.type === 'checkbox' ? el.checked : el.value);
          onLive();
        });
      }
    };
    bind('#ghost-name', (v) => { Model.project.meta.name = v; });
    bind('#meta-name', (v) => { Model.project.meta.name = v; });
    bind('#meta-sakura', (v) => { Model.project.meta.sakuraName = v; });
    bind('#meta-kero', (v) => { Model.project.meta.keroName = v; });
    bind('#meta-craftman', (v) => { Model.project.meta.craftman = v; });
    bind('#meta-url', (v) => { Model.project.meta.craftmanUrl = v; });
    bind('#meta-version', (v) => { Model.project.meta.version = v; });
    bind('#meta-desc', (v) => { Model.project.meta.description = v; });
    bind('#set-talk-interval', (v) => { Model.project.settings.randomTalkInterval = Number(v) || 0; });
    bind('#set-no-repeat', (v) => { Model.project.settings.noRepeatCount = Math.max(0, Number(v) || 0); });
    bind('#set-talk-enabled', (v) => { Model.project.settings.randomTalkEnabled = !!v; });
    bind('#shell-sakura-color', (v) => { Model.project.shell.sakuraColor = v; }, refreshShell);
    bind('#shell-sakura-cloth', (v) => { Model.project.shell.sakuraCloth = v; }, refreshShell);
    bind('#shell-kero-color', (v) => { Model.project.shell.keroColor = v; }, refreshShell);
    bind('#shell-kero-cloth', (v) => { Model.project.shell.keroCloth = v; }, refreshShell);
  }

  // ------------------------------------------------------------- チェック
  function renderCheck() {
    const list = $('#check-list');
    const badge = $('#check-badge');
    const issues = N.Lint.run(Model.project);
    const errors = N.Lint.countErrors(issues);

    badge.textContent = issues.length ? String(issues.length) : '';
    badge.classList.toggle('is-on', issues.length > 0);
    badge.classList.toggle('warn-only', issues.length > 0 && errors === 0);

    list.textContent = '';
    if (!issues.length) {
      list.appendChild(Render.div('check-ok', '✓ 気になるところはありません'));
      return;
    }
    for (const it of issues) {
      const item = Render.el('button', 'check-item ' + (it.level === 'error' ? 'is-error' : 'is-warn'));
      item.appendChild(Render.el('span', 'mark', it.level === 'error' ? '⛔' : '⚠'));
      const msg = Render.div('msg');
      msg.appendChild(document.createTextNode(it.message));
      if (it.hint) msg.appendChild(Render.el('span', 'why', it.hint));
      item.appendChild(msg);
      if (it.script) {
        item.title = 'クリックでその場所へ移動します';
        item.addEventListener('click', () => jumpTo(it));
      } else {
        item.style.cursor = 'default';
      }
      list.appendChild(item);
    }
  }

  /** チェックで見つけた場所までキャンバスを動かして、光らせる */
  function jumpTo(it) {
    if (!it.script) return;
    let target = null;
    let scriptEl = null;
    for (const el of document.querySelectorAll('#canvas .script')) {
      if (el._script === it.script) { scriptEl = el; break; }
    }
    if (!scriptEl) return;
    if (it.block) {
      for (const el of scriptEl.querySelectorAll('.blk')) {
        if (el._blk === it.block) { target = el; break; }
      }
    }

    const ws = $('#workspace');
    const box = (target || scriptEl).getBoundingClientRect();
    const view = ws.getBoundingClientRect();
    ws.scrollLeft += box.left - view.left - Math.max(40, (view.width - box.width) / 2);
    ws.scrollTop += box.top - view.top - Math.max(40, (view.height - box.height) / 2);

    const flashEl = target || scriptEl;
    flashEl.classList.remove('flash');
    void flashEl.offsetWidth;          // アニメーションをやり直させる
    flashEl.classList.add('flash');
    setTimeout(() => flashEl.classList.remove('flash'), 2600);
  }

  function switchTab(name) {
    for (const t of document.querySelectorAll('.side-tab')) {
      t.classList.toggle('is-active', t.dataset.tab === name);
    }
    for (const p of document.querySelectorAll('.side-panel')) {
      p.classList.toggle('is-active', p.dataset.panel === name);
    }
  }

  // ---------------------------------------------------------- 保存 / 読込
  async function saveProject(silent) {
    const name = Model.project.meta.name || 'ゴースト';
    try {
      const r = await api('/api/project', { method: 'POST', body: { name, project: Model.project } });
      App.projectName = r.saved;
      Model.dirty = false;
      updateTitle();
      if (!silent) App.toast(`「${r.saved}」を保存しました`);
      return true;
    } catch (e) {
      App.toast(e.message, true);
      return false;
    }
  }

  /** ウィンドウを閉じるとき、アプリ本体から呼ばれる（保存し終わったら合図を返す） */
  App.requestSaveAndClose = async function () {
    try { await saveProject(true); } catch (e) { /* 保存できなくても閉じる */ }
    if (window.chrome && window.chrome.webview) window.chrome.webview.postMessage('saved');
  };

  function modal(title, bodyEl, buttons) {
    const root = $('#modal-root');
    root.textContent = '';
    const box = Render.div('modal');
    box.appendChild(Render.el('h3', null, title));
    const body = Render.div('modal-body');
    body.appendChild(bodyEl);
    box.appendChild(body);
    const foot = Render.div('modal-foot');
    for (const b of buttons) {
      const btn = Render.el('button', 'btn' + (b.primary ? ' primary' : ''), b.label);
      btn.addEventListener('click', () => { if (b.run) b.run(); if (b.close !== false) closeModal(); });
      foot.appendChild(btn);
    }
    box.appendChild(foot);
    root.appendChild(box);
    root.classList.add('open');
    root.onclick = (e) => { if (e.target === root) closeModal(); };
  }
  function closeModal() { $('#modal-root').classList.remove('open'); }

  async function openProjectDialog() {
    let state;
    try { state = await api('/api/state'); } catch (e) { App.toast(e.message, true); return; }
    const list = Render.div('');
    if (!state.projects.length) list.appendChild(Render.div('hint', 'まだ保存したプロジェクトがありません。'));
    for (const p of state.projects) {
      const row = Render.div('proj-row');
      row.appendChild(Render.div('t', p.title));
      row.appendChild(Render.div('d', new Date(p.updated).toLocaleString('ja-JP')));
      const x = Render.el('button', 'x', '✕');
      x.title = '削除';
      x.addEventListener('click', async (e) => {
        e.stopPropagation();
        if (!confirm(`「${p.title}」を削除しますか？`)) return;
        await api('/api/project/delete', { method: 'POST', body: { name: p.file } });
        closeModal();
        openProjectDialog();
      });
      row.appendChild(x);
      row.addEventListener('click', async () => {
        try {
          const data = await api('/api/project?name=' + encodeURIComponent(p.file));
          Model.init(data);
          App.projectName = p.file;
          App.sessionVars = {};
          closeModal();
          App.toast(`「${p.title}」を開きました`);
        } catch (e) { App.toast(e.message, true); }
      });
      list.appendChild(row);
    }
    modal('プロジェクトを開く', list, [{ label: '閉じる' }]);
  }

  // ------------------------------------------------------------- 書き出し
  async function doExport(mode) {
    const outDir = $('#export-dir').value.trim();
    const box = $('#export-result');
    box.textContent = '書き出し中…';
    try {
      const r = await api('/api/export', {
        method: 'POST',
        body: {
          project: Model.project,
          outDir,
          mode,
          includeShell: $('#export-shell').checked,
          overwriteShell: $('#export-overwrite').checked,
        },
      });
      box.textContent = '';
      const p = Render.div('');
      if (mode === 'nar') {
        p.innerHTML = `<b>.nar を作りました</b><br><code>${escapeHtml(r.path)}</code><br>` +
          'SSP のウィンドウにドラッグ＆ドロップするとインストールできます。';
      } else {
        p.innerHTML = `<b>書き出しました</b><br><code>${escapeHtml(r.root)}</code><br>` +
          `ファイル ${r.written.length} 個` +
          (r.skipped.length ? `（既存のシェル ${r.skipped.length} 個はそのまま）` : '');
      }
      if (!r.dll) {
        p.appendChild(Render.div('status-line status-ng',
          'nashi.dll が見つかりません。shiori\\build.ps1 を実行してから書き出し直してください。'));
      }
      const open = Render.el('button', 'btn small', 'フォルダを開く');
      open.style.marginTop = '8px';
      open.addEventListener('click', () => api('/api/reveal', {
        method: 'POST', body: { path: r.root || r.path },
      }).catch(() => {}));
      p.appendChild(open);
      box.appendChild(p);
      App.toast('書き出しました');
    } catch (e) {
      box.textContent = '';
      box.appendChild(Render.div('status-line status-ng', e.message));
      App.toast(e.message, true);
    }
  }

  function escapeHtml(s) {
    return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  }

  // -------------------------------------------------------------- SSP 連携
  function currentScript() {
    const id = $('#run-target').value;
    return Model.project.scripts.find((s) => s.id === id) || null;
  }

  function describeSsp(ssp) {
    if (!ssp) return { text: 'SSP: 確認できません', ok: false };
    if (ssp.sstp) {
      return { text: 'SSP: つながっています' + (ssp.ghost ? `（${ssp.ghost}）` : ''), ok: true };
    }
    if (ssp.running) return { text: 'SSP: 起動中（SSTP の応答なし）', ok: false };
    if (ssp.exe) return { text: 'SSP: 見つかりました（停止中）', ok: false };
    return { text: 'SSP: 見つかりません', ok: false };
  }

  async function refreshSsp() {
    let ssp = null;
    try { ssp = await api('/api/ssp'); } catch (e) { /* 表示だけ更新する */ }
    App.ssp = ssp;

    const badge = $('#ssp-status');
    const info = describeSsp(ssp);
    badge.textContent = info.text;
    badge.classList.toggle('is-off', !info.ok);
    $('#btn-run-ssp').disabled = !info.ok;
    $('#btn-send-event').disabled = !info.ok;
    $('#btn-send-comm').disabled = !info.ok;

    const box = $('#ssp-info');
    if (box) {
      box.className = 'status-line ' + (info.ok ? 'status-ok' : 'status-ng');
      let text = info.text;
      if (ssp && ssp.ghostDir) text += `\n${ssp.ghostDir}`;
      else text += '\n下の欄に ssp.exe の場所を入れてください。';
      box.textContent = text;
      box.style.whiteSpace = 'pre-wrap';
    }
    const install = $('#btn-ssp-install');
    if (install) install.disabled = !(ssp && ssp.ghostDir);
    const launch = $('#btn-ssp-launch');
    if (launch) launch.disabled = !(ssp && ssp.exe) || (ssp && ssp.running);
    if (ssp && ssp.exe && !$('#ssp-path').value) $('#ssp-path').value = ssp.exe;
    return ssp;
  }

  async function sendScriptToSsp() {
    const script = currentScript();
    if (!script) { App.toast('実行するかたまりがありません', true); return; }
    const res = Sim.runScript(Model.project, script, { vars: App.sessionVars, refs: refsFor(script) });
    App.sessionVars = res.vars;
    $('#script-preview').textContent = res.script || '（なにも出力されませんでした）';
    renderRunVars();
    if (!res.script) { App.toast('出力がありませんでした', true); return; }
    try {
      await api('/api/ssp/script', { method: 'POST', body: { script: res.script } });
      App.toast('SSP のゴーストに送りました');
    } catch (e) {
      App.toast(e.message, true);
      refreshSsp();
    }
  }

  async function sendEventToSsp() {
    const script = currentScript();
    if (!script) return;
    if (script.kind !== 'event') {
      App.toast('イベントのかたまりを選んでください', true);
      return;
    }
    try {
      await api('/api/ssp/notify', { method: 'POST', body: { event: script.event, refs: refsFor(script) } });
      App.toast(`${script.event} を送りました（SSP に入れたゴーストが反応します）`);
    } catch (e) {
      App.toast(e.message, true);
      refreshSsp();
    }
  }

  // 他のゴーストのふりをして、動いているゴーストに話しかける。
  // 「話しかけられたとき」のブロックを、本物のゴーストで試すためのもの。
  async function sendCommToSsp() {
    const script = currentScript();
    const isComm = script && script.kind === 'event' && script.event === 'OnCommunicate';
    // 「話しかけられたとき」を選んでいれば、その条件どおりの内容で話しかける
    const sender = (isComm && script.from) || 'ほかのゴースト';
    const sentence = (isComm && script.contains) || 'こんにちは';
    try {
      await api('/api/ssp/communicate', { method: 'POST', body: { sentence, sender } });
      App.toast(`「${sender}」が「${sentence}」と話しかけました`);
    } catch (e) {
      App.toast(e.message, true);
      refreshSsp();
    }
  }

  async function installToSsp() {
    const box = $('#ssp-result');
    box.textContent = 'SSP に入れています…';
    try {
      const r = await api('/api/ssp/install', {
        method: 'POST',
        body: {
          project: Model.project,
          includeShell: $('#export-shell').checked,
          overwriteShell: $('#export-overwrite').checked,
          activate: true,
        },
      });
      const notes = {
        reload: '入れ直して、栞を読み込み直しました。すぐ反映されています。',
        change: 'SSP のゴーストを切り替えました。切り替わらないときは、新しいゴーストなので SSP を再起動してください。',
        none: '書き出しました。SSP を起動（または再起動）すると反映されます。',
        failed: '書き出しましたが、SSP への指示は届きませんでした。',
      };
      box.innerHTML =
        `<b>${escapeHtml(r.folder)}</b> を入れました（${r.files} ファイル）<br>` +
        `<code>${escapeHtml(r.root)}</code><br>${escapeHtml(notes[r.action] || '')}`;
      App.toast('SSP に入れました');
      refreshSsp();
    } catch (e) {
      box.textContent = '';
      box.appendChild(Render.div('status-line status-ng', e.message));
      App.toast(e.message, true);
    }
  }

  // ------------------------------------------------------------------ ズーム
  function setZoom(z) {
    App.zoom = Math.max(0.4, Math.min(1.6, z));
    $('#canvas').style.transform = 'scale(' + App.zoom + ')';
    N.Drag.setZoom(App.zoom);
    $('#btn-zoom-reset').textContent = Math.round(App.zoom * 100) + '%';
  }

  // ------------------------------------------------------------------- 起動
  async function boot() {
    renderCategories();
    bindMeta();
    setupContextMenu();

    N.Drag.init({
      workspace: $('#workspace'),
      canvas: $('#canvas'),
      layer: $('#drag-layer'),
      palette: $('#palette'),
      trash: $('#trash'),
    });

    Model.onChange(scheduleRender);

    let state = null;
    try { state = await api('/api/state'); } catch (e) { console.warn(e); }
    App.state = state;

    if (state) {
      $('#export-dir').value = state.defaultOutDir || '';
      const st = $('#dll-status');
      st.className = 'status-line ' + (state.dllFound ? 'status-ok' : 'status-ng');
      st.textContent = state.dllFound
        ? '栞 (nashi.dll) の準備ができています'
        : 'nashi.dll がまだありません。shiori\\build.ps1 を実行してください。';
      $('#ssp-hint').textContent = state.sspGhostDir
        ? 'SSP らしきフォルダを見つけました: ' + state.sspGhostDir
        : 'SSP の ghost フォルダを指定すると、そのまま追加できます。';
    }

    let loaded = null;
    if (state && state.lastProject) {
      try { loaded = await api('/api/project?name=' + encodeURIComponent(state.lastProject)); } catch { /* 新規 */ }
    }
    Model.init(loaded || sampleProject());
    setZoom(1);
    switchTab('run');
    resetStage();

    // ---- ボタン類
    $('#btn-new').addEventListener('click', () => {
      if (Model.dirty && !confirm('保存していない変更があります。新しく作りますか？')) return;
      Model.init(sampleProject());
      App.sessionVars = {};
      App.toast('新しいゴーストを作りました');
    });
    $('#btn-open').addEventListener('click', openProjectDialog);
    $('#btn-save').addEventListener('click', () => saveProject(false));
    $('#btn-undo').addEventListener('click', () => Model.undo());
    $('#btn-redo').addEventListener('click', () => Model.redo());
    $('#btn-export').addEventListener('click', () => { switchTab('export'); doExport('dir'); });
    $('#btn-export-dir').addEventListener('click', () => doExport('dir'));
    $('#btn-export-nar').addEventListener('click', () => doExport('nar'));
    $('#btn-add-var').addEventListener('click', () => App.promptNewVariable());
    $('#btn-anim-add').addEventListener('click', addAnimation);
    $('#btn-shell-add').addEventListener('click', () => {
      const v = prompt('どの番号の立ち絵にしますか？（さくらは 0〜9、うにゅうは 10 以上）', '3');
      if (v == null) return;
      const id = Math.max(0, Math.floor(Number(v)));
      if (!Number.isFinite(id)) { App.toast('番号は数字で入れてください', true); return; }
      pickShellImage(id);
    });
    $('#btn-run').addEventListener('click', () => {
      const id = $('#run-target').value;
      const s = Model.project.scripts.find((x) => x.id === id);
      if (s) App.runScript(s);
      else App.toast('実行するかたまりがありません', true);
    });
    $('#btn-copy-script').addEventListener('click', async () => {
      try {
        await navigator.clipboard.writeText($('#script-preview').textContent);
        App.toast('コピーしました');
      } catch { App.toast('コピーできませんでした', true); }
    });
    $('#btn-zoom-in').addEventListener('click', () => setZoom(App.zoom + 0.1));
    $('#btn-zoom-out').addEventListener('click', () => setZoom(App.zoom - 0.1));
    $('#btn-zoom-reset').addEventListener('click', () => setZoom(1));
    $('#btn-tidy').addEventListener('click', () => { tidy(); App.toast('整列しました'); });
    $('#btn-import').addEventListener('click', async () => {
      const p = $('#import-path').value.trim();
      if (!p) return;
      try {
        const r = await api('/api/import', { method: 'POST', body: { path: p } });
        Model.init(r.project);
        App.sessionVars = {};
        App.toast('読み込みました');
      } catch (e) { App.toast(e.message, true); }
    });

    $('#btn-run-ssp').addEventListener('click', sendScriptToSsp);
    $('#btn-send-event').addEventListener('click', sendEventToSsp);
    $('#btn-send-comm').addEventListener('click', sendCommToSsp);
    $('#btn-vars-reset').addEventListener('click', resetSessionVars);
    $('#btn-save-reset').addEventListener('click', resetGhostSave);
    $('#btn-ssp-install').addEventListener('click', installToSsp);
    $('#btn-ssp-refresh').addEventListener('click', async () => {
      await refreshSsp();
      App.toast('SSP の状態を確認しました');
    });
    $('#btn-ssp-launch').addEventListener('click', async () => {
      try {
        await api('/api/ssp/launch', { method: 'POST', body: {} });
        App.toast('SSP を起動しました');
        setTimeout(refreshSsp, 2500);
      } catch (e) { App.toast(e.message, true); }
      refreshSsp();
    });
    $('#btn-ssp-path').addEventListener('click', async () => {
      try {
        await api('/api/ssp/path', { method: 'POST', body: { path: $('#ssp-path').value.trim() } });
        await refreshSsp();
        App.toast('SSP の場所を覚えました');
      } catch (e) { App.toast(e.message, true); }
    });

    for (const t of document.querySelectorAll('.side-tab')) {
      t.addEventListener('click', () => {
        switchTab(t.dataset.tab);
        if (t.dataset.tab === 'export' || t.dataset.tab === 'run') refreshSsp();
      });
    }
    refreshSsp();
    setInterval(refreshSsp, 30000);

    // ---- キーボード
    document.addEventListener('keydown', (e) => {
      const typing = /^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement.tagName);
      if (e.ctrlKey && e.key.toLowerCase() === 's') { e.preventDefault(); saveProject(false); return; }
      if (e.ctrlKey && e.key.toLowerCase() === 'z') { e.preventDefault(); Model.undo(); return; }
      if (e.ctrlKey && (e.key.toLowerCase() === 'y' || (e.shiftKey && e.key.toLowerCase() === 'z'))) {
        e.preventDefault(); Model.redo(); return;
      }
      if (typing) return;
      if (e.key === 'Delete' || e.key === 'Backspace') {
        const sel = N.Drag.getSelected();
        if (sel && Array.isArray(sel.stack)) {
          e.preventDefault();
          Model.act(() => { sel.stack.splice(sel.index, 1); });
          N.Drag.select(null);
        }
      }
      if (e.key === 'Escape') App.closeContextMenu();
    });

    $('#workspace').addEventListener('wheel', (e) => {
      if (!e.ctrlKey) return;
      e.preventDefault();
      setZoom(App.zoom + (e.deltaY < 0 ? 0.08 : -0.08));
    }, { passive: false });

    window.addEventListener('beforeunload', (e) => {
      if (!Model.dirty) return;
      e.preventDefault();
      e.returnValue = '';
    });

    setInterval(() => { if (Model.dirty) saveProject(true); }, 60000);
  }

  // ------------------------------------------------------------ サンプル
  function sampleProject() {
    const p = Model.newProject();
    p.meta.name = 'なしゴースト';
    p.variables = [{ name: '好感度', value: 0 }];
    p.scripts = [
      {
        id: 'sample_boot', kind: 'event', event: 'OnFirstBoot', x: 60, y: 40,
        blocks: [
          { type: 'surface', who: 0, id: 0 },
          { type: 'say', who: 0, text: 'はじめまして。', nl: 1 },
          { type: 'say', who: 1, text: 'よろしくね。', nl: 1 },
        ],
      },
      {
        id: 'sample_boot2', kind: 'event', event: 'OnBoot', x: 60, y: 260,
        blocks: [
          { type: 'change', name: '好感度', value: 1 },
          { type: 'say', who: 0, text: 'おかえりなさい。', nl: 1 },
        ],
      },
      {
        id: 'sample_click', kind: 'event', event: 'OnMouseDoubleClick', x: 60, y: 440,
        blocks: [
          {
            type: 'random_one',
            branches: [
              [{ type: 'say', who: 0, text: 'なあに？', nl: 1 }],
              [{ type: 'say', who: 0, text: 'どうしたの？', nl: 1 }],
            ],
          },
        ],
      },
      {
        id: 'sample_nade', kind: 'event', event: 'OnNadeNade', area: 'Head', who: 0,
        x: 60, y: 620,
        blocks: [
          { type: 'change', name: '好感度', value: 3 },
          { type: 'surface', who: 0, id: 1 },
          { type: 'say', who: 0, text: 'えへへ、くすぐったい。', nl: 1 },
        ],
      },
      {
        id: 'sample_talk', kind: 'talk', name: '天気の話', weight: 1, x: 480, y: 40,
        blocks: [
          { type: 'say', who: 0, text: 'いい天気だね。', nl: 1 },
          { type: 'say', who: 1, text: 'そうだね。', nl: 1 },
        ],
      },
    ];
    return p;
  }

  document.addEventListener('DOMContentLoaded', boot);

})(window.NASHI);
