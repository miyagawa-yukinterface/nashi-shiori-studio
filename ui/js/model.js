/* なしスタジオ - プロジェクトのデータモデルと Undo */
'use strict';

(function (N) {

  const clone = (v) => JSON.parse(JSON.stringify(v));

  // こまの重ねかた（SERIKO の描画メソッド）。書き出しでもこの名前だけ通します。
  const ANIM_METHODS = ['overlay', 'overlayfast', 'base', 'replace',
    'interpolate', 'asis', 'reduce', 'move'];

  let seq = 0;
  function uid(prefix) {
    seq++;
    return (prefix || 'id') + '_' + Date.now().toString(36).slice(-4) + seq.toString(36);
  }

  const Model = {
    project: null,
    undoStack: [],
    redoStack: [],
    listeners: [],
    dirty: false,

    uid,
    clone,

    // ------------------------------------------------------------ lifecycle
    newProject() {
      return {
        format: 'nashi/1',
        meta: {
          name: 'なしゴースト',
          sakuraName: 'さくら',
          keroName: 'うにゅう',
          craftman: '',
          craftmanUrl: '',
          homeUrl: '',
          version: '1.0.0',
          description: '',
        },
        settings: {
          randomTalkInterval: 180,
          randomTalkEnabled: true,
          noRepeatCount: 0,
          defaultSurfaceSakura: 0,
          defaultSurfaceKero: 10,
        },
        shell: {
          sakuraColor: '#f08cae', sakuraCloth: '#6e82c8',
          keroColor: '#8fd18a', keroCloth: '#e8b45c',
          images: [],       // { id, path, name, w, h } 用意した立ち絵
        },
        variables: [],
        scripts: [],
      };
    },

    normalize(p) {
      const base = Model.newProject();
      const out = Object.assign(base, p || {});
      out.meta = Object.assign(base.meta, (p && p.meta) || {});
      out.settings = Object.assign(base.settings, (p && p.settings) || {});
      out.shell = Object.assign(base.shell, (p && p.shell) || {});
      out.shell.images = (Array.isArray(out.shell.images) ? out.shell.images : [])
        .filter((x) => x && x.path && Number.isFinite(Number(x.id)))
        .map((x) => ({
          id: Number(x.id), path: String(x.path),
          name: x.name ? String(x.name) : String(x.path).split(/[\/]/).pop(),
          w: Number(x.w) || 0, h: Number(x.h) || 0,
        }))
        .sort((a, b) => a.id - b.id);
      out.variables = Array.isArray(out.variables) ? out.variables : [];
      out.variables = out.variables
        .filter((v) => v && v.name)
        .map((v) => ({ name: String(v.name), value: v.value == null ? 0 : v.value }));
      // うごき（SERIKO のアニメーション）。書き出すと surfaces.txt に入ります。
      out.animations = (Array.isArray(out.animations) ? out.animations : [])
        .map((a, i) => ({
          id: Number.isFinite(Number(a && a.id)) ? Number(a.id) : i,
          base: Number(a && a.base) || 0,
          interval: (a && a.interval) ? String(a.interval) : 'never',
          every: Number(a && a.every) || 4,
          patterns: (Array.isArray(a && a.patterns) ? a.patterns : [])
            .map((p) => ({
              surface: Number(p && p.surface) || 0,
              wait: Number.isFinite(Number(p && p.wait)) ? Number(p.wait) : 200,
              method: ANIM_METHODS.indexOf(p && p.method) >= 0 ? p.method : 'base',
              x: Number(p && p.x) || 0,
              y: Number(p && p.y) || 0,
            })),
          // このうごきの間だけ有効な当たり判定
          collisions: (Array.isArray(a && a.collisions) ? a.collisions : [])
            .filter((c) => c && String(c.name || '').trim())
            .map((c) => ({
              name: String(c.name).trim(),
              x1: Number(c.x1) || 0, y1: Number(c.y1) || 0,
              x2: Number(c.x2) || 0, y2: Number(c.y2) || 0,
            })),
        }));

      out.scripts = Array.isArray(out.scripts) ? out.scripts : [];
      let y = 40;
      out.scripts = out.scripts.map((s) => {
        const script = Object.assign({ kind: 'event', blocks: [] }, s);
        script.id = script.id || uid('s');
        script.blocks = Array.isArray(script.blocks) ? script.blocks : [];
        if (typeof script.x !== 'number') script.x = 60;
        if (typeof script.y !== 'number') { script.y = y; y += 180; }
        if (script.kind === 'talk' && script.weight == null) script.weight = 1;
        if ((script.kind === 'talk' || script.kind === 'function') && !script.name) {
          script.name = script.kind === 'talk' ? 'トーク' : 'なまえのないトーク';
        }
        if (script.kind === 'event') {
          if (!script.event) script.event = 'OnBoot';
          // マウス系イベントのしぼり込み（当たった場所・相手）
          if (typeof script.area !== 'string') script.area = '';
          if (typeof script.who !== 'number') script.who = -1;
          // ゴースト間通信のしぼり込み（だれが・なんと言ったら）
          if (typeof script.from !== 'string') script.from = '';
          if (typeof script.contains !== 'string') script.contains = '';
          // くりかえしの間隔（1 なら毎秒）
          script.everySec = Math.max(1, Math.floor(Number(script.everySec) || 1));
          // 読み込んだゴーストの filter を、編集できる形にほどく
          if (s.filter && typeof s.filter === 'object') {
            if (typeof s.filter.area === 'string') script.area = s.filter.area;
            if (typeof s.filter.who === 'number') script.who = s.filter.who;
            if (typeof s.filter.from === 'string') script.from = s.filter.from;
            if (typeof s.filter.contains === 'string') script.contains = s.filter.contains;
            delete script.filter;
          }
        }
        return script;
      });
      return out;
    },

    init(project) {
      Model.project = Model.normalize(project);
      Model.undoStack = [];
      Model.redoStack = [];
      Model.dirty = false;
      Model.emit();
    },

    // --------------------------------------------------------------- events
    onChange(fn) { Model.listeners.push(fn); },
    emit() { Model.listeners.forEach((fn) => fn(Model.project)); },

    /** すべての変更はこれを通す（Undo 用のスナップショットを取る） */
    act(fn) {
      const snap = clone(Model.project);
      let result;
      try {
        result = fn();
      } catch (e) {
        console.error(e);
        Model.project = snap;
        Model.emit();
        throw e;
      }
      if (result === false) return false;   // 変更なし
      Model.undoStack.push(snap);
      if (Model.undoStack.length > 80) Model.undoStack.shift();
      Model.redoStack = [];
      Model.dirty = true;
      Model.emit();
      return result;
    },

    /** 入力欄の編集など、すでに書き換えたあとで Undo 履歴だけ積みたいとき */
    pushUndo(snapshot) {
      if (!snapshot) return;
      if (JSON.stringify(snapshot) === JSON.stringify(Model.project)) return;
      Model.undoStack.push(snapshot);
      if (Model.undoStack.length > 80) Model.undoStack.shift();
      Model.redoStack = [];
      Model.dirty = true;
      Model.emit();
    },

    undo() {
      if (!Model.undoStack.length) return false;
      Model.redoStack.push(clone(Model.project));
      Model.project = Model.undoStack.pop();
      Model.dirty = true;
      Model.emit();
      return true;
    },

    redo() {
      if (!Model.redoStack.length) return false;
      Model.undoStack.push(clone(Model.project));
      Model.project = Model.redoStack.pop();
      Model.dirty = true;
      Model.emit();
      return true;
    },

    // -------------------------------------------------------------- scripts
    addScript(kind, opts) {
      const s = Object.assign({
        id: uid('s'),
        kind,
        x: 60, y: 60,
        blocks: [],
      }, opts || {});
      if (kind === 'event') {
        if (!s.event) s.event = 'OnBoot';
        if (typeof s.area !== 'string') s.area = '';
        if (typeof s.who !== 'number') s.who = -1;
        if (typeof s.from !== 'string') s.from = '';
        if (typeof s.contains !== 'string') s.contains = '';
        if (typeof s.everySec !== 'number') s.everySec = 1;
      }
      if (kind === 'talk') { s.name = s.name || 'トーク'; if (s.weight == null) s.weight = 1; }
      if (kind === 'function') s.name = s.name || 'あたらしいトーク';
      Model.project.scripts.push(s);
      return s;
    },

    removeScript(script) {
      const i = Model.project.scripts.indexOf(script);
      if (i >= 0) Model.project.scripts.splice(i, 1);
    },

    scriptTitle(s) {
      if (!s) return '';
      if (s.kind === 'event') {
        const found = N.EVENTS.find((e) => e[1] === s.event);
        let label = found ? found[0] : (s.event || 'イベント');
        if (s.event === 'OnSecondChange') {
          const n = Math.max(1, Number(s.everySec) || 1);
          return n > 1 ? `${n}秒ごとにくりかえす` : label;
        }
        if (N.MOUSE_EVENTS[s.event]) {
          // 仮シェルの名前に無いもの（うごきで足した当たり判定）は、そのまま出す
          const area = (N.AREAS.find((a) => a[1] === (s.area || '')) || [])[0] || s.area;
          const who = (N.WHO_ANY.find((w) => w[1] === (s.who == null ? -1 : s.who)) || [])[0];
          if (s.area || (s.who != null && s.who >= 0)) label = who + 'の' + area + 'が' + label;
        }
        if (s.event === 'OnCommunicate' && (s.from || s.contains)) {
          const who = s.from ? s.from : 'だれか';
          label = s.contains ? `${who}に「${s.contains}」と${label}` : `${who}に${label}`;
        }
        return label;
      }
      if (s.kind === 'talk') return 'ランダムトーク「' + (s.name || '') + '」';
      if (s.kind === 'function') return '「' + (s.name || '') + '」';
      return 'ブロックのかたまり';
    },

    functionNames() {
      return Model.project.scripts
        .filter((s) => s.kind === 'function' || s.kind === 'talk')
        .map((s) => s.name)
        .filter((n, i, a) => n && a.indexOf(n) === i);
    },

    // ------------------------------------------------------------ variables
    addVariable(name, value) {
      name = String(name || '').trim();
      if (!name) return null;
      if (Model.project.variables.some((v) => v.name === name)) return null;
      const v = { name, value: value == null ? 0 : value };
      Model.project.variables.push(v);
      return v;
    },

    renameVariable(oldName, newName) {
      newName = String(newName || '').trim();
      if (!newName || oldName === newName) return false;
      if (Model.project.variables.some((v) => v.name === newName)) return false;
      const v = Model.project.variables.find((x) => x.name === oldName);
      if (!v) return false;
      v.name = newName;
      Model.walkBlocks((b) => {
        if ((b.type === 'var' || b.type === 'set' || b.type === 'change') && b.name === oldName) {
          b.name = newName;
        }
      });
      return true;
    },

    removeVariable(name) {
      const i = Model.project.variables.findIndex((v) => v.name === name);
      if (i >= 0) Model.project.variables.splice(i, 1);
    },

    variableNames() { return Model.project.variables.map((v) => v.name); },

    // --------------------------------------------------------------- blocks
    /** すべてのブロックを訪問する。visit(block, stack, index) */
    walkBlocks(visit) {
      const walkStack = (stack) => {
        if (!Array.isArray(stack)) return;
        stack.forEach((b, i) => {
          if (!b || typeof b !== 'object') return;
          visit(b, stack, i);
          walkInner(b);
        });
      };
      const walkInner = (b) => {
        const d = N.getDef(b);
        if (d) {
          for (const s of d.subs || []) walkStack(b[s.key]);
          if (d.dynamic && Array.isArray(b[d.dynamic])) b[d.dynamic].forEach(walkStack);
          for (const name in (d.args || {})) {
            const v = b[name];
            if (v && typeof v === 'object' && v.type) { visit(v, null, -1); walkInner(v); }
          }
        }
      };
      Model.project.scripts.forEach((s) => walkStack(s.blocks));
    },

    countBlocks() {
      let n = 0;
      Model.walkBlocks(() => { n++; });
      return n;
    },
  };

  N.Model = Model;

})(window.NASHI);
