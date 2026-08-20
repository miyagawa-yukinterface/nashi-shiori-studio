/* なしスタジオ - 画面全体の組み立て */
'use strict';

(function (N) {

  const Model = N.Model;
  const Render = N.Render;
  const Player = N.Player;
  const $ = (sel) => document.querySelector(sel);
  N.$ = $;   // あとから読みこむ画面ファイルも使います

  const App = {
    state: null,
    projectName: '',
    zoom: 1,
    category: 'events',
    sessionVars: {},
    clipboard: null,          // コピーしたブロック（Ctrl+C / Ctrl+V）
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
    N.Search.renderCheck();
    N.Search.renderSearch();
    syncMetaFields();
    $('#btn-undo').disabled = !Model.undoStack.length;
    $('#btn-redo').disabled = !Model.redoStack.length;
    updateTitle();
  }

  function updateTitle() {
    document.title = (Model.dirty ? '● ' : '') +
      (Model.project.meta.name || 'なしゴースト') + ' - なしスタジオ';
  }

  function switchTab(name) {
    for (const t of document.querySelectorAll('.side-tab')) {
      t.classList.toggle('is-active', t.dataset.tab === name);
    }
    for (const p of document.querySelectorAll('.side-panel')) {
      p.classList.toggle('is-active', p.dataset.panel === name);
    }
  }

  function escapeHtml(s) {
    return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
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

  /**
   * ブロックを動かして、さくらスクリプトをもらう。
   * 動かすのは栞そのもの（studio/src/preview.cpp → shiori/src/interp.cpp）です。
   * 画面側で同じ規則をもう一度書くと、片方だけ直したときに静かにズレるためです。
   */
  async function runPreview(script) {
    const meta = Model.project.meta || {};
    return api('/api/preview', {
      method: 'POST',
      body: {
        project: Model.project,
        scriptId: script.id,
        refs: refsFor(script),
        vars: App.sessionVars,
        sys: { ghostName: meta.name || '', shellName: 'master' },
      },
    });
  }

  App.runScript = async function (script) {
    if (!script) return;
    $('#run-target').value = script.id;
    switchTab('run');
    let res;
    try {
      res = await runPreview(script);
    } catch (e) {
      $('#script-preview').textContent = '（ためせませんでした: ' + e.message + '）';
      App.toast(e.message, true);
      return;
    }
    App.sessionVars = res.vars || {};
    let preview = res.script || '（なにも出力されませんでした）';
    // 話しかけ先はさくらスクリプトに出ないので、ここで見せる
    if (res.commTo) preview += `\n\n（「${res.commTo}」に届きます）`;
    $('#script-preview').textContent = preview;
    renderRunVars();
    N.Shell.playSakura(res.script);
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
      box.appendChild(Render.div('run-var', `${k}: ${Player.toStr(shown[k])}`));
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

  /** 栞の記録（nashi_debug.txt）を読む・はじめる・やめる
   *
   * 栞は「ファイルがあるときだけ」書くので、記録をはじめる＝空ファイルを作る、です。
   * SAORI が呼べなかった理由など、画面に出ない事情はここに残ります。
   */
  async function ghostLog(action) {
    const box = $('#ghost-log');
    try {
      const r = await api('/api/ghost/log', {
        method: 'POST',
        body: {
          project: Model.project,
          outDir: $('#export-dir') ? $('#export-dir').value : '',
          action: action || '',
        },
      });
      if (!(r.places || []).length) {
        box.textContent = 'まだ書き出していないようです。先に「SSP に入れて動かす」を押してください。';
        return;
      }
      if (action === 'clear') {
        box.textContent = '記録をやめました。';
        App.toast('記録をやめました');
        return;
      }
      if (!r.recording) {
        box.textContent = 'いまは記録していません。「記録をはじめる」を押すと、'
          + 'つぎに動かしたときから残ります。';
        return;
      }
      box.textContent = r.text && r.text.trim()
        ? r.text
        : `記録中です（${r.path}）。まだ何も書かれていません。\n`
          + 'ゴーストを動かしてから、もう一度「読む」を押してください。';
      box.scrollTop = box.scrollHeight;
      if (action === 'start') App.toast('記録をはじめました');
    } catch (e) {
      box.textContent = e.message;
      App.toast(e.message, true);
    }
  }

  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

  function balloonOf(who) {
    return document.getElementById('balloon-' + (who === 1 ? 1 : 0));
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
    setVal('#meta-homeurl', m.homeUrl);
    setVal('#meta-version', m.version);
    setVal('#meta-desc', m.description);
    setVal('#set-talk-interval', s.randomTalkInterval);
    setVal('#set-no-repeat', s.noRepeatCount == null ? 0 : s.noRepeatCount);
    $('#set-talk-enabled').checked = s.randomTalkEnabled !== false;
    setVal('#shell-balloon-color', sh.balloonColor);
    $('#shell-balloon-on').checked = !!sh.balloonEnabled;
    setVal('#shell-sakura-color', sh.sakuraColor);
    setVal('#shell-sakura-cloth', sh.sakuraCloth);
    setVal('#shell-kero-color', sh.keroColor);
    setVal('#shell-kero-cloth', sh.keroCloth);
    N.Shell.refreshShell();
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
    bind('#meta-homeurl', (v) => { Model.project.meta.homeUrl = v.trim(); });
    bind('#meta-version', (v) => { Model.project.meta.version = v; });
    bind('#meta-desc', (v) => { Model.project.meta.description = v; });
    bind('#set-talk-interval', (v) => { Model.project.settings.randomTalkInterval = Number(v) || 0; });
    bind('#set-no-repeat', (v) => { Model.project.settings.noRepeatCount = Math.max(0, Number(v) || 0); });
    bind('#set-talk-enabled', (v) => { Model.project.settings.randomTalkEnabled = !!v; });
    bind('#shell-balloon-on', (v) => { Model.project.shell.balloonEnabled = !!v; });
    bind('#shell-balloon-color', (v) => { Model.project.shell.balloonColor = v; });
    bind('#shell-sakura-color', (v) => { Model.project.shell.sakuraColor = v; }, N.Shell.refreshShell);
    bind('#shell-sakura-cloth', (v) => { Model.project.shell.sakuraCloth = v; }, N.Shell.refreshShell);
    bind('#shell-kero-color', (v) => { Model.project.shell.keroColor = v; }, N.Shell.refreshShell);
    bind('#shell-kero-cloth', (v) => { Model.project.shell.keroCloth = v; }, N.Shell.refreshShell);
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
    N.Shell.resetStage();

    // ---- ボタン類
    $('#btn-new').addEventListener('click', () => {
      if (Model.dirty && !confirm('保存していない変更があります。新しく作りますか？')) return;
      Model.init(sampleProject());
      App.sessionVars = {};
      App.toast('新しいゴーストを作りました');
    });
    $('#btn-open').addEventListener('click', N.Dialog.openProjectDialog);
    $('#btn-save').addEventListener('click', () => N.Dialog.saveProject(false));
    $('#btn-undo').addEventListener('click', () => Model.undo());
    $('#btn-redo').addEventListener('click', () => Model.redo());
    $('#btn-export').addEventListener('click', () => { switchTab('export'); N.Dialog.doExport('dir'); });
    $('#btn-export-dir').addEventListener('click', () => N.Dialog.doExport('dir'));
    $('#btn-export-nar').addEventListener('click', () => N.Dialog.doExport('nar'));
    $('#btn-add-var').addEventListener('click', () => App.promptNewVariable());
    $('#btn-anim-add').addEventListener('click', N.Shell.addAnimation);
    $('#btn-shell-add').addEventListener('click', () => {
      const v = prompt('どの番号の立ち絵にしますか？（さくらは 0〜9、うにゅうは 10 以上）', '3');
      if (v == null) return;
      const id = Math.max(0, Math.floor(Number(v)));
      if (!Number.isFinite(id)) { App.toast('番号は数字で入れてください', true); return; }
      N.Shell.pickShellImage(id);
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

    $('#btn-run-ssp').addEventListener('click', N.Ssp.sendScriptToSsp);
    $('#btn-send-event').addEventListener('click', N.Ssp.sendEventToSsp);
    $('#btn-send-comm').addEventListener('click', N.Ssp.sendCommToSsp);
    $('#btn-vars-reset').addEventListener('click', resetSessionVars);
    $('#btn-save-reset').addEventListener('click', resetGhostSave);
    $('#btn-log-show').addEventListener('click', () => ghostLog(''));
    $('#btn-log-start').addEventListener('click', () => ghostLog('start'));
    $('#btn-log-clear').addEventListener('click', () => ghostLog('clear'));
    $('#btn-ssp-install').addEventListener('click', N.Ssp.installToSsp);
    $('#btn-ssp-refresh').addEventListener('click', async () => {
      await N.Ssp.refreshSsp();
      App.toast('SSP の状態を確認しました');
    });
    $('#btn-ssp-launch').addEventListener('click', async () => {
      try {
        await api('/api/ssp/launch', { method: 'POST', body: {} });
        App.toast('SSP を起動しました');
        setTimeout(N.Ssp.refreshSsp, 2500);
      } catch (e) { App.toast(e.message, true); }
      N.Ssp.refreshSsp();
    });
    $('#btn-ssp-path').addEventListener('click', async () => {
      try {
        await api('/api/ssp/path', { method: 'POST', body: { path: $('#ssp-path').value.trim() } });
        await N.Ssp.refreshSsp();
        App.toast('SSP の場所を覚えました');
      } catch (e) { App.toast(e.message, true); }
    });

    for (const t of document.querySelectorAll('.side-tab')) {
      t.addEventListener('click', () => {
        switchTab(t.dataset.tab);
        if (t.dataset.tab === 'export' || t.dataset.tab === 'run') N.Ssp.refreshSsp();
      });
    }
    N.Ssp.refreshSsp();
    setInterval(N.Ssp.refreshSsp, 30000);

    const searchInput = $('#search-input');
    if (searchInput) {
      searchInput.addEventListener('input', N.Search.renderSearch);
      searchInput.addEventListener('keydown', (e) => { if (e.key === 'Escape') searchInput.blur(); });
    }

    // ---- キーボード
    document.addEventListener('keydown', (e) => {
      const typing = /^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement.tagName);
      if (e.ctrlKey && e.key.toLowerCase() === 's') { e.preventDefault(); N.Dialog.saveProject(false); return; }
      if (e.ctrlKey && e.key.toLowerCase() === 'f') {
        e.preventDefault();
        switchTab('search');
        if (searchInput) { searchInput.focus(); searchInput.select(); }
        return;
      }
      if (e.ctrlKey && e.key.toLowerCase() === 'z') { e.preventDefault(); Model.undo(); return; }
      if (e.ctrlKey && (e.key.toLowerCase() === 'y' || (e.shiftKey && e.key.toLowerCase() === 'z'))) {
        e.preventDefault(); Model.redo(); return;
      }
      if (typing) return;

      // ---- ブロックのコピー＆はりつけ
      //   コピーしたものは Model.clone で切り離して持つので、あとから元を消しても平気です。
      const sel = N.Drag.getSelected();
      const key = e.key.toLowerCase();
      if (e.ctrlKey && (key === 'c' || key === 'x')) {
        if (!sel || !sel.block) return;
        e.preventDefault();
        App.clipboard = Model.clone(sel.block);
        if (key === 'x' && Array.isArray(sel.stack)) {
          Model.act(() => { sel.stack.splice(sel.index, 1); });
          N.Drag.select(null);
        }
        App.toast(key === 'x' ? 'ブロックを切り取りました' : 'ブロックをコピーしました');
        return;
      }
      if (e.ctrlKey && key === 'v') {
        if (!App.clipboard) { App.toast('コピーしたブロックがありません', true); return; }
        if (!sel || !Array.isArray(sel.stack)) {
          App.toast('はりつける場所のブロックを、先にクリックしてください', true);
          return;
        }
        e.preventDefault();
        Model.act(() => { sel.stack.splice(sel.index + 1, 0, Model.clone(App.clipboard)); });
        App.toast('はりつけました');
        return;
      }
      if (e.ctrlKey && key === 'd') {
        if (!sel || !sel.block || !Array.isArray(sel.stack)) return;
        e.preventDefault();
        Model.act(() => { sel.stack.splice(sel.index + 1, 0, Model.clone(sel.block)); });
        App.toast('ブロックを複製しました');
        return;
      }

      if (e.key === 'Delete' || e.key === 'Backspace') {
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

    setInterval(() => { if (Model.dirty) N.Dialog.saveProject(true); }, 60000);
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

  // ---------------------------------------------------- 分けた画面ファイルへ渡すもの
  // shell.js / search.js / dialog.js / ssp.js は app.js のあとに読みこまれるので、
  // ここに載せたものを N.App 経由で使います。逆向き（app.js から向こう）は、
  // 読みこみ順の都合で N.Shell.xxx() のように、その場で引きます。
  App.api = api;
  App.sleep = sleep;
  App.escapeHtml = escapeHtml;
  App.updateTitle = updateTitle;
  App.switchTab = switchTab;
  App.refsFor = refsFor;
  App.runPreview = runPreview;
  App.renderRunVars = renderRunVars;
  App.balloonOf = balloonOf;
  App.scheduleRender = scheduleRender;
  App.renderAll = renderAll;

  document.addEventListener('DOMContentLoaded', boot);

})(window.NASHI);
