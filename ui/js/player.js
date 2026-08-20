/* なしスタジオ - さくらスクリプトの再生（「ためす」タブの動きと吹き出し）
 *
 * ここはブロックを実行しません。**実行するのは栞そのもの**です。
 * 画面は POST /api/preview（studio/src/preview.cpp → shiori/src/interp.cpp）に頼み、
 * 返ってきたさくらスクリプトを、ここで「操作の列」にほどいて再生します。
 *
 * 以前はブロックを動かす規則を JavaScript でもう一度書いていましたが、
 * 片方だけ直すと静かにズレるので、やめました（docs/maintenance.md）。
 */
'use strict';

(function (N) {

  function fmtNum(v) {
    if (!isFinite(v)) return '0';
    if (Number.isInteger(v)) return String(v);
    return String(Math.round(v * 1e10) / 1e10);
  }

  // 栞は「はい/いいえ」を数の 1 / 0 として持っている（program.h の Value::Bool）ので、
  // 画面に出すときも 1 / 0 にそろえる。
  const toStr = (v) => (
    v == null ? ''
      : typeof v === 'boolean' ? (v ? '1' : '0')
        : typeof v === 'number' ? fmtNum(v) : String(v));

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

  N.Player = { parseSakura, toStr };

})(window.NASHI);
