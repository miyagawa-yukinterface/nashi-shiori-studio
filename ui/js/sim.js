/* なしスタジオ - ブラウザ側インタプリタ（栞と同じ意味でブロックを実行してプレビューする）
 * shiori/src/interp.cpp と同じ規則で動かしています。
 */
'use strict';

(function (N) {

  const MAX_STEPS = 40000;
  const MAX_VALUE = 32000;   // 変数 1 つぶんの長さの上限（バイト）
  const MAX_LOOP = 5000;

  // 長すぎる文字は切る（UTF-8 の途中では切らない）。
  // shiori/src/interp.cpp の CapText と同じ規則にすること（一致テストが見ています）。
  function capText(s) {
    if (s.length <= MAX_VALUE / 4) return s;      // どう転んでも上限以下
    const bytes = new TextEncoder().encode(s);
    if (bytes.length <= MAX_VALUE) return s;
    let n = MAX_VALUE;
    while (n > 0 && (bytes[n] & 0xC0) === 0x80) n--;
    return new TextDecoder().decode(bytes.subarray(0, n));
  }

  function escapeText(text) {
    let out = '';
    const s = String(text == null ? '' : text);
    for (const ch of s) {
      if (ch === '\\') out += '\\\\';
      else if (ch === '\r') continue;
      else if (ch === '\n') out += '\\n';
      else out += ch;
    }
    return out;
  }

  const isNumeric = (v) => v !== '' && v !== null && v !== undefined && !isNaN(Number(v));
  const toNum = (v) => (isNumeric(v) ? Number(v) : 0);
  // 栞は「はい/いいえ」を数の 1 / 0 として持っている（program.h の Value::Bool）ので、
  // 文字にするときも 1 / 0 にそろえる。true / false と出すとプレビューだけ表示が変わる。
  const toStr = (v) => (
    v == null ? ''
      : typeof v === 'boolean' ? (v ? '1' : '0')
        : typeof v === 'number' ? fmtNum(v) : String(v));
  const toBool = (v) => {
    if (typeof v === 'boolean') return v;
    if (typeof v === 'number') return v !== 0;
    const s = String(v == null ? '' : v);
    return s !== '' && s !== '0' && s.toLowerCase() !== 'false';
  };

  function fmtNum(v) {
    if (!isFinite(v)) return '0';
    if (Number.isInteger(v)) return String(v);
    return String(Math.round(v * 1e10) / 1e10);
  }

  function compare(a, b) {
    if (isNumeric(a) && isNumeric(b)) {
      const x = Number(a), y = Number(b);
      return x < y ? -1 : x > y ? 1 : 0;
    }
    const x = toStr(a), y = toStr(b);
    return x < y ? -1 : x > y ? 1 : 0;
  }

  function sysValue(key, ctx) {
    const now = new Date();
    switch (key) {
      case 'hour': return now.getHours();
      case 'minute': return now.getMinutes();
      case 'second': return now.getSeconds();
      case 'year': return now.getFullYear();
      case 'month': return now.getMonth() + 1;
      case 'day': return now.getDate();
      case 'weekday': return now.getDay();
      // 話のとっかかりに使う「いまごろ」。shiori/src/interp.cpp と同じ区切りにすること。
      case 'daypart': {
        const h = now.getHours();
        if (h < 5) return '夜';
        if (h < 11) return '朝';
        if (h < 17) return '昼';
        if (h < 22) return '夕方';
        return '夜';
      }
      case 'season': {
        const m = now.getMonth() + 1;
        if (m >= 3 && m <= 5) return '春';
        if (m >= 6 && m <= 8) return '夏';
        if (m >= 9 && m <= 11) return '秋';
        return '冬';
      }
      case 'weekdayname': return ['日', '月', '火', '水', '木', '金', '土'][now.getDay()] || '日';
      case 'uptime': return ctx.sys.uptime;
      case 'uptimemin': return Math.floor(ctx.sys.uptime / 60);
      case 'boots': return ctx.sys.boots;
      case 'talks': return ctx.sys.talks;
      case 'ghostname': return ctx.sys.ghostName;
      case 'shellname': return ctx.sys.shellName;
      case 'lasttalk': return ctx.sys.lastTalk || '';
      // ゴースト間通信。OnCommunicate の Reference0 / Reference1。
      case 'commfrom': return ctx.refs[0] == null ? '' : ctx.refs[0];
      case 'commtext': return ctx.refs[1] == null ? '' : ctx.refs[1];
      default: return 0;
    }
  }

  function evalExpr(node, ctx) {
    if (ctx.steps++ > MAX_STEPS) return '';
    if (node == null) return '';
    if (typeof node === 'number' || typeof node === 'string' || typeof node === 'boolean') return node;
    if (Array.isArray(node)) return '';
    const t = node.type;
    switch (t) {
      case 'num': return toNum(node.value);
      case 'text': return toStr(node.value);
      case 'bool': return toBool(node.value);
      case 'var': return ctx.vars[node.name] == null ? 0 : ctx.vars[node.name];
      case 'ref': return ctx.refs[node.index] == null ? '' : ctx.refs[node.index];
      case 'sys': return sysValue(node.key, ctx);
      case 'random': {
        let lo = Math.floor(toNum(evalExpr(node.min, ctx)));
        let hi = Math.floor(toNum(evalExpr(node.max, ctx)));
        if (hi < lo) { const t2 = lo; lo = hi; hi = t2; }
        return lo + Math.floor(Math.random() * (hi - lo + 1));
      }
      case 'arith': {
        const a = toNum(evalExpr(node.a, ctx)), b = toNum(evalExpr(node.b, ctx));
        switch (node.op) {
          case '+': return a + b;
          case '-': return a - b;
          case '*': return a * b;
          case '/': return b === 0 ? 0 : a / b;
          case '%': {
            if (b === 0) return 0;
            let r = a % b;
            if (r !== 0 && (r < 0) !== (b < 0)) r += b;
            return r;
          }
          case 'min': return Math.min(a, b);
          case 'max': return Math.max(a, b);
          default: return 0;
        }
      }
      case 'round': {
        const a = toNum(evalExpr(node.a, ctx));
        if (node.op === 'floor') return Math.floor(a);
        if (node.op === 'ceil') return Math.ceil(a);
        if (node.op === 'abs') return Math.abs(a);
        return Math.floor(a + 0.5);
      }
      case 'compare': {
        const c = compare(evalExpr(node.a, ctx), evalExpr(node.b, ctx));
        switch (node.op) {
          case '=': case '==': return c === 0;
          case '!=': case '<>': return c !== 0;
          case '<': return c < 0;
          case '>': return c > 0;
          case '<=': return c <= 0;
          case '>=': return c >= 0;
          default: return false;
        }
      }
      case 'logic': {
        const a = toBool(evalExpr(node.a, ctx));
        if (node.op === 'and') return a ? toBool(evalExpr(node.b, ctx)) : false;
        return a ? true : toBool(evalExpr(node.b, ctx));
      }
      case 'not': return !toBool(evalExpr(node.a, ctx));
      // つなげ続けてメモリを食いつぶさないように、上限までで切る
      case 'join': return capText(toStr(evalExpr(node.a, ctx)) + toStr(evalExpr(node.b, ctx)));
      case 'contains': {
        const a = toStr(evalExpr(node.a, ctx)), b = toStr(evalExpr(node.b, ctx));
        return b === '' ? true : a.indexOf(b) >= 0;
      }
      case 'length': return Array.from(toStr(evalExpr(node.a, ctx))).length;
      case 'chance': return Math.random() * 100 < toNum(evalExpr(node.a, ctx));
      // 外部モジュール(SAORI)は本物の DLL が要るので、プレビューでは呼べない。
      // 空を返して、SSP に入れてから確かめてもらう。
      case 'saori': return '';
      default: return '';
    }
  }

  // [ … ] の中に入れる文字を安全にする。
  // ] が混じると、その先が命令として読まれてしまいます。ここへ入るのは
  // ほかのゴーストから来た言葉かもしれないので、必ず通してから出します。
  // shiori/src/interp.cpp の TagArg と同じ規則にすること（一致テストが見ています）。
  function safeTag(text, dropComma) {
    const re = dropComma ? /[,[\]\r\n]/g : /[[\]\r\n]/g;
    return String(text == null ? '' : text).replace(re, '');
  }

  function emitScope(ctx, who) {
    who = Math.floor(toNum(who));
    if (ctx.scope === who) return;
    ctx.out += who === 0 ? '\\0' : who === 1 ? '\\1' : '\\p[' + who + ']';
    ctx.scope = who;
  }

  function runBlocks(stack, ctx) {
    if (!Array.isArray(stack)) return;
    if (ctx.depth++ > 48) { ctx.depth--; return; }
    for (const b of stack) {
      if (ctx.stopped || ctx.steps > MAX_STEPS) break;
      runBlock(b, ctx);
    }
    ctx.depth--;
  }

  function runBlock(b, ctx) {
    if (!b || typeof b !== 'object' || b.disabled) return;
    if (ctx.steps++ > MAX_STEPS) { ctx.stopped = true; return; }
    const ev = (v) => evalExpr(v, ctx);

    switch (b.type) {
      case 'say': {
        emitScope(ctx, ev(b.who));
        if (b.surface != null && b.surface !== '') ctx.out += '\\s[' + Math.floor(toNum(ev(b.surface))) + ']';
        ctx.out += escapeText(toStr(ev(b.text)));
        if (b.nl == null || toBool(b.nl)) ctx.out += '\\n';
        return;
      }
      case 'surface':
        emitScope(ctx, ev(b.who));
        ctx.out += '\\s[' + Math.floor(toNum(ev(b.id))) + ']';
        return;
      // 3 人目以降のキャラに切りかえる（\p[n]）。0 と 1 は \0 \1 になる。
      case 'chara': {
        let id = Math.floor(toNum(ev(b.id)));
        if (!(id >= 0)) id = 0;
        emitScope(ctx, id);
        return;
      }
      case 'newline': {
        let n = Math.floor(toNum(ev(b.count)));
        if (!(n >= 1)) n = 1;
        if (n > 32) n = 32;
        ctx.out += '\\n'.repeat(n);
        return;
      }
      case 'wait': {
        let ms = Math.floor(toNum(ev(b.ms)));
        ms = Math.max(0, Math.min(60000, ms));
        ctx.out += '\\_w[' + ms + ']';
        return;
      }
      case 'click_wait': ctx.out += '\\x'; return;
      case 'clear': ctx.out += '\\c'; return;
      case 'raw': ctx.out += toStr(ev(b.text)); return;
      // SERIKO のアニメーションを再生する（\i[n]）。
      // プレビューでは動かないが、さくらスクリプトには出す。
      case 'anim': {
        let id = Math.floor(toNum(ev(b.id)));
        if (!(id >= 0)) id = 0;
        ctx.out += '\\i[' + id + ']';
        return;
      }
      case 'balloon': ctx.out += '\\b[' + Math.floor(toNum(ev(b.id))) + ']'; return;
      case 'sound': {
        const f = safeTag(toStr(ev(b.file)), false);
        if (f) ctx.out += '\\_v[' + f + ']';
        return;
      }
      // 他のゴーストに話しかける。プレビューでは普通のセリフとして出す
      // （相手に届くところは、SSP に入れてから確かめてください）。
      case 'communicate': {
        emitScope(ctx, ev(b.who));
        ctx.out += escapeText(toStr(ev(b.text)));
        const to = toStr(ev(b.to)).trim();
        if (to) ctx.commTo = to;
        return;
      }
      case 'link': {
        const url = safeTag(toStr(ev(b.url)), false);
        const label = toStr(ev(b.label)) || url;
        if (url) ctx.out += '\\_a[' + url + ']' + safeTag(escapeText(label), false) + '\\_a';
        return;
      }
      case 'choice': {
        const label = safeTag(escapeText(toStr(ev(b.label))), false);
        if (!label) return;
        ctx.out += '\\q[' + label + ',' + safeTag(toStr(ev(b.target)), true) + ']';
        return;
      }
      // \![…] でお願いする系。プレビューでは何も起きないが、出力は本番と同じにする。
      // カンマ・角かっこは命令を壊すので、栞と同じ規則で落とす。
      case 'ask': case 'change_ghost': case 'open_browser': case 'raise': {
        const one = (v) => safeTag(toStr(v), true).trim();
        if (b.type === 'ask') {
          const into = one(b.into);
          if (into) ctx.out += '\\![open,inputbox,nashi:' + into + ',-1,' + one(ev(b.initial)) + ']';
          return;
        }
        if (b.type === 'change_ghost') {
          const name = one(ev(b.name));
          if (name) { ctx.out += '\\![change,ghost,' + name + ']'; ctx.stopped = true; }
          return;
        }
        if (b.type === 'open_browser') {
          const url = one(ev(b.url));
          if (url) ctx.out += '\\![open,browser,' + url + ']';
          return;
        }
        const evName = one(ev(b.event));
        if (!evName) return;
        const a = one(ev(b.a));
        ctx.out += '\\![raise,' + evName + (a ? ',' + a : '') + ']';
        return;
      }
      // 「N 秒後によぶ」。プレビューでは待てないので、何も起きない。
      case 'later': return;
      // 答えを待たない SAORI。プレビューでは本物の DLL を読めないので、
      // 何も起きない（変数にも入れない。答えは「あとで届く」ものなので）。
      case 'saori_call': return;
      // ネットワーク更新。プレビューでは何も起きないが、出力は本番と同じにする。
      case 'update': ctx.out += '\\![updatebymyself]'; return;
      case 'end': ctx.out += '\\e'; ctx.stopped = true; return;
      case 'stop': ctx.stopped = true; return;
      case 'close': ctx.out += '\\-'; ctx.stopped = true; return;
      case 'set': {
        if (!b.name) return;
        const v = ev(b.value);
        ctx.vars[b.name] = typeof v === 'string' ? capText(v) : v;
        return;
      }
      case 'change':
        if (b.name) ctx.vars[b.name] = toNum(ctx.vars[b.name]) + toNum(ev(b.value));
        return;
      case 'talk_interval':
        ctx.vars['@talkInterval'] = Math.max(0, Math.floor(toNum(ev(b.sec))));
        return;
      case 'if':
        if (toBool(ev(b.cond))) runBlocks(b.then, ctx);
        return;
      case 'if_else':
        if (toBool(ev(b.cond))) runBlocks(b.then, ctx);
        else runBlocks(b.else, ctx);
        return;
      case 'repeat': {
        let n = Math.floor(toNum(ev(b.count)));
        if (n > MAX_LOOP) n = MAX_LOOP;
        for (let i = 0; i < n && !ctx.stopped; i++) {
          if (ctx.steps > MAX_STEPS) break;
          runBlocks(b.body, ctx);
        }
        return;
      }
      case 'while': {
        let guard = 0;
        while (!ctx.stopped && guard++ < MAX_LOOP) {
          if (ctx.steps > MAX_STEPS) break;
          if (!toBool(ev(b.cond))) break;
          runBlocks(b.body, ctx);
        }
        return;
      }
      case 'random_one': {
        const branches = Array.isArray(b.branches) ? b.branches : [];
        if (!branches.length) return;
        runBlocks(branches[Math.floor(Math.random() * branches.length)], ctx);
        return;
      }
      // 同じ「まとまり」のランダムトークから 1 つえらぶ（栞と同じ規則）
      case 'call_group': {
        const group = toStr(ev(b.group)).trim();
        if (!group) return;
        let list = ctx.project.scripts.filter(
          (s) => s.kind === 'talk' && !s.disabled && (s.group || '') === group
        );
        if (!list.length) return;
        if (list.length > 1 && ctx.sys.lastTalk) {
          list = list.filter((s) => s.id !== ctx.sys.lastTalk);
        }
        let total = 0;
        for (const s of list) { const w = Number(s.weight); if (w > 0) total += w; }
        let pick = null;
        if (total <= 0) {
          pick = list[Math.floor(Math.random() * list.length)];
        } else {
          let r = Math.random() * total;
          for (const s of list) {
            const w = Number(s.weight);
            if (!(w > 0)) continue;
            r -= w;
            if (r <= 0) { pick = s; break; }
          }
          if (!pick) pick = list[list.length - 1];
        }
        if (!pick || ctx.callStack.includes(pick) || ctx.callStack.length > 16) return;
        ctx.callStack.push(pick);
        runBlocks(pick.blocks, ctx);
        ctx.callStack.pop();
        return;
      }
      case 'call': {
        const name = b.name || '';
        const fn = ctx.project.scripts.find(
          (s) => (s.kind === 'function' || s.kind === 'talk') && s.name === name
        ) || ctx.project.scripts.find((s) => s.id === name);
        if (!fn || ctx.callStack.includes(fn) || ctx.callStack.length > 16) return;
        ctx.callStack.push(fn);
        runBlocks(fn.blocks, ctx);
        ctx.callStack.pop();
        return;
      }
      default:
        return;
    }
  }

  /** script を実行して、さくらスクリプト文字列を返す */
  function runScript(project, script, options) {
    const opt = options || {};
    const ctx = {
      project,
      vars: opt.vars || {},
      refs: opt.refs || [],
      sys: Object.assign({
        uptime: 0, boots: 1, talks: 0, lastTalk: '',
        ghostName: (project.meta && project.meta.name) || '',
        shellName: 'master',
      }, opt.sys || {}),
      out: '',
      commTo: '',
      scope: -1,
      steps: 0,
      depth: 0,
      stopped: false,
      callStack: [],
    };
    for (const v of project.variables || []) {
      if (ctx.vars[v.name] === undefined) ctx.vars[v.name] = v.value;
    }
    runBlocks(script.blocks, ctx);
    let out = ctx.out;
    const isClose = script.kind === 'event' && (script.event === 'OnClose' || script.event === 'OnCloseAll');
    if (out) {
      if (isClose) {
        if (out.indexOf('\\-') < 0) out += '\\-';
      } else if (out.indexOf('\\e') < 0 && out.indexOf('\\-') < 0) {
        out += '\\e';
      }
    }
    return { script: out, vars: ctx.vars, commTo: ctx.commTo };
  }

  // ------------------------------------------------------- SakuraScript 解析
  /** プレビュー再生のために、さくらスクリプトを操作の列に分解する */
  function parseSakura(s) {
    const ops = [];
    let i = 0;
    let text = '';
    const flush = () => { if (text) { ops.push({ op: 'text', text }); text = ''; } };
    const readBracket = () => {
      if (s[i] !== '[') return null;
      const end = s.indexOf(']', i);
      if (end < 0) return null;
      const body = s.slice(i + 1, end);
      i = end + 1;
      return body;
    };

    while (i < s.length) {
      const c = s[i];
      if (c !== '\\') { text += c; i++; continue; }
      i++;
      const t = s[i];
      if (t === undefined) break;
      if (t === '\\') { text += '\\'; i++; continue; }
      i++;
      switch (t) {
        case '0': case '1': flush(); ops.push({ op: 'scope', who: Number(t) }); break;
        case 'h': flush(); ops.push({ op: 'scope', who: 0 }); break;
        case 'u': flush(); ops.push({ op: 'scope', who: 1 }); break;
        case 'p': { const a = readBracket(); flush(); ops.push({ op: 'scope', who: Number(a) || 0 }); break; }
        case 's': { const a = readBracket(); flush(); ops.push({ op: 'surface', id: Number(a) || 0 }); break; }
        case 'n': {
          if (s[i] === '[') readBracket();
          flush(); ops.push({ op: 'newline' });
          break;
        }
        case 'c': flush(); ops.push({ op: 'clear' }); break;
        case 'e': flush(); ops.push({ op: 'end' }); break;
        case '-': flush(); ops.push({ op: 'close' }); break;
        case 'x': flush(); ops.push({ op: 'clickwait' }); break;
        case 'b': { readBracket(); break; }
        case 'q': {
          const a = readBracket() || '';
          const comma = a.lastIndexOf(',');
          flush();
          ops.push({
            op: 'choice',
            label: comma >= 0 ? a.slice(0, comma) : a,
            id: comma >= 0 ? a.slice(comma + 1) : '',
          });
          break;
        }
        case 'w': {
          const n = Number(s[i]);
          if (!isNaN(n)) { i++; flush(); ops.push({ op: 'wait', ms: n * 50 }); }
          break;
        }
        case '_': {
          const kind = s[i]; i++;
          const a = readBracket();
          if (kind === 'w') { flush(); ops.push({ op: 'wait', ms: Number(a) || 0 }); }
          else if (kind === 'a') { flush(); ops.push({ op: 'link', url: a }); }
          else if (kind === 'v') { flush(); ops.push({ op: 'sound', file: a }); }
          break;
        }
        case '!': { readBracket(); break; }
        default: {
          if (s[i] === '[') readBracket();
          break;
        }
      }
    }
    flush();
    return ops;
  }

  N.Sim = { runScript, parseSakura, escapeText, evalExpr, toStr, toNum, toBool };

})(window.NASHI);
