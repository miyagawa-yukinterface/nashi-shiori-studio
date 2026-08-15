/* なしスタジオ - プロジェクトのチェック（あぶないところ探し）
 *
 * 見つけたものは { level, message, hint, script, block } の配列で返します。
 * level は 'error'（動かない・たぶん間違い）と 'warn'（気になる）の 2 段階です。
 */
'use strict';

(function (N) {

  const Lint = {};

  function issue(list, level, message, hint, script, block) {
    list.push({ level, message, hint: hint || '', script: script || null, block: block || null });
  }

  /** スクリプトの中のブロックを、入れ子もふくめて順に見る */
  function walk(script, visit) {
    const walkStack = (stack) => {
      if (!Array.isArray(stack)) return;
      for (const b of stack) {
        if (!b || typeof b !== 'object') continue;
        visit(b);
        walkInner(b);
      }
    };
    const walkInner = (b) => {
      const d = N.getDef(b);
      if (!d) return;
      for (const s of d.subs || []) walkStack(b[s.key]);
      if (d.dynamic && Array.isArray(b[d.dynamic])) b[d.dynamic].forEach(walkStack);
      for (const name in (d.args || {})) {
        const v = b[name];
        if (v && typeof v === 'object' && v.type) { visit(v); walkInner(v); }
      }
    };
    walkStack(script.blocks);
  }

  /** あるブロックの内側（くりかえしの中など）だけを見る */
  function walkStackOnly(block, visit) {
    const d = N.getDef(block);
    if (!d) return;
    const stacks = [];
    for (const s of d.subs || []) stacks.push(block[s.key]);
    if (d.dynamic && Array.isArray(block[d.dynamic])) stacks.push(...block[d.dynamic]);
    for (const stack of stacks) {
      if (!Array.isArray(stack)) continue;
      for (const b of stack) {
        if (!b || typeof b !== 'object') continue;
        visit(b);
        walkStackOnly(b, visit);
      }
    }
  }

  /** メッセージ用のかたまり名。すでに「」が付いているものは足さない */
  function nameOf(script) {
    const t = N.Model.scriptTitle(script);
    return t.charAt(0) === '「' ? t : '「' + t + '」';
  }

  function isEmptyText(v) {
    if (v == null) return true;
    if (typeof v === 'object') return false;      // 変数などが入っている
    return String(v).trim() === '';
  }

  Lint.run = function (project) {
    const out = [];
    if (!project) return out;

    const varNames = (project.variables || []).map((v) => v.name);
    const callable = (project.scripts || [])
      .filter((s) => s.kind === 'function' || s.kind === 'talk')
      .map((s) => s.name);
    const talkCount = (project.scripts || []).filter((s) => s.kind === 'talk').length;

    // ---- 名前のかぶり
    const seen = {};
    for (const s of project.scripts || []) {
      if (s.kind !== 'function' && s.kind !== 'talk') continue;
      const name = s.name || '';
      if (seen[name]) {
        issue(out, 'error', `「${name}」という名前が 2 つあります`,
          'よぶときにどちらか一方しか使われません。名前を変えてください。', s);
      }
      seen[name] = true;
    }

    // ---- スクリプトごと
    for (const s of project.scripts || []) {
      const title = nameOf(s);
      const blocks = Array.isArray(s.blocks) ? s.blocks : [];

      if (s.kind !== 'event' && s.kind !== 'talk' && s.kind !== 'function') {
        issue(out, 'warn', 'どこにもつながっていないブロックがあります',
          '帽子ブロック（◯◯のとき など）につなげないと動きません。', s);
      } else if (!blocks.length) {
        issue(out, 'warn', `${title}の中身がからっぽです`,
          'ブロックをつなげるか、いらなければ削除してください。', s);
      }

      if (s.kind === 'event' && !String(s.event || '').trim()) {
        issue(out, 'error', 'イベント名が空のかたまりがあります',
          'イベントをえらぶか、名前を直接入力してください。', s);
      }
      if ((s.kind === 'talk' || s.kind === 'function') && !String(s.name || '').trim()) {
        issue(out, 'error', '名前のないトークがあります',
          '名前をつけないと、よび出せません。', s);
      }
      if (s.kind === 'talk' && Number(s.weight) < 0) {
        issue(out, 'warn', `${title}のえらばれやすさがマイナスです`,
          '0 として扱われるので、このトークは出ません。', s);
      }

      // ---- 定常ループ（ずっとくりかえす）の重さ
      if (s.kind === 'event' && s.event === 'OnSecondChange') {
        const every = Math.max(1, Number(s.everySec) || 1);
        let count = 0;
        let hasLoop = false;
        walk(s, (b) => {
          count++;
          if (b.type === 'repeat' || b.type === 'while') hasLoop = true;
        });
        // 帽子のすぐ下にセリフがある＝条件なしで毎回しゃべる
        const speaksAlways = blocks.some(
          (b) => b && (b.type === 'say' || b.type === 'raw' || b.type === 'call' || b.type === 'choice')
        );
        if (every <= 1 && speaksAlways) {
          issue(out, 'warn', `${title}は毎秒しゃべることになります`,
            '帽子ブロックの「◯秒ごと」を大きくするか、「もし」で条件をつけてください。', s);
        } else if (every <= 1 && (hasLoop || count >= 10)) {
          issue(out, 'warn', `${title}は毎秒これだけ動くので、重くなりがちです`,
            '「◯秒ごと」を 5 秒くらいにすると、体感は変わらずに軽くなります。', s);
        }
      }

      walk(s, (b) => {
        // くりかえしの中で細かく待つと、長いスクリプトになって重くなる
        if (b.type === 'repeat' || b.type === 'while') {
          let tiny = null;
          walkStackOnly(b, (inner) => {
            if (inner.type === 'wait' && typeof inner.ms === 'number' && inner.ms > 0 && inner.ms <= 20) {
              tiny = inner;
            }
          });
          if (tiny) {
            issue(out, 'warn', `${title}の「くりかえし」の中で、${tiny.ms} ミリ秒だけ待っています`,
              '待ちの数だけスクリプトが長くなります。回数を減らすか、待ちを長くしてください。', s, tiny);
          }
        }
        if (!N.getDef(b)) {
          issue(out, 'error', `${title}に、知らないブロック（${b.type}）が入っています`,
            '新しい版のなしスタジオで作られたファイルかもしれません。', s, b);
          return;
        }
        switch (b.type) {
          case 'say':
            if (isEmptyText(b.text)) {
              issue(out, 'warn', `${title}に、なにも言わないセリフがあります`,
                '文章を入れるか、ブロックを削除してください。', s, b);
            }
            break;
          case 'raw':
            if (isEmptyText(b.text)) {
              issue(out, 'warn', `${title}に、からっぽのさくらスクリプトがあります`, '', s, b);
            }
            break;
          case 'communicate':
            if (isEmptyText(b.to)) {
              issue(out, 'error', `${title}に、話しかける相手が空のブロックがあります`,
                '相手のゴースト名（本体の名前）を入れてください。'
                + '「話しかけてきた相手」ブロックを入れると、言われた相手に返せます。', s, b);
            }
            if (isEmptyText(b.text)) {
              issue(out, 'warn', `${title}に、なにも言わずに話しかけるブロックがあります`,
                '文章がないと、相手には届きません。', s, b);
            }
            break;
          case 'saori_call':
            if (isEmptyText(b.file)) {
              issue(out, 'error', `${title}に、よぶファイルが空の SAORI ブロックがあります`,
                'ゴーストのフォルダに置いた DLL の名前を入れてください。', s, b);
            }
            if (isEmptyText(b.into)) {
              issue(out, 'warn', `${title}に、答えの入れ先が空の SAORI ブロックがあります`,
                '答えを入れる変数を決めておかないと、届いた答えを使えません。'
                + '（「SAORI の答えがとどいたとき」でも受け取れます）', s, b);
            } else if (varNames.indexOf(b.into) < 0) {
              issue(out, 'error', `変数「${b.into}」がありません（${title}）`,
                '変数タブで作るか、別の変数にえらび直してください。', s, b);
            }
            break;
          case 'update':
            // 「更新のありか」が無いと、SSP は更新しようがない
            if (isEmptyText((project.meta || {}).homeUrl)) {
              issue(out, 'warn', `${title}に、ネットワーク更新のブロックがあります`,
                'ゴーストの設定の「更新のありか」が空です。'
                + '入れておかないと、SSP は更新できません。', s, b);
            }
            break;
          case 'var': case 'set': case 'change':
            if (isEmptyText(b.name)) {
              issue(out, 'error', `${title}に、変数をえらんでいないブロックがあります`,
                '変数タブで変数を作ってから、えらんでください。', s, b);
            } else if (varNames.indexOf(b.name) < 0) {
              issue(out, 'error', `変数「${b.name}」がありません（${title}）`,
                '変数タブで作るか、別の変数にえらび直してください。', s, b);
            }
            break;
          case 'call':
            if (isEmptyText(b.name)) {
              issue(out, 'error', `${title}の「よぶ」ブロックで、行き先がえらばれていません`, '', s, b);
            } else if (callable.indexOf(b.name) < 0) {
              issue(out, 'error', `「${b.name}」というトークがありません（${title}）`,
                '名前を変えたときは、よび出し側も直してください。', s, b);
            }
            break;
          case 'choice':
            if (isEmptyText(b.target)) {
              issue(out, 'error', `${title}の選択肢に、行き先がありません`,
                'えらんでも何も起きません。行き先のトークをえらんでください。', s, b);
            } else if (callable.indexOf(b.target) < 0) {
              issue(out, 'error', `選択肢の行き先「${b.target}」がありません（${title}）`, '', s, b);
            }
            if (isEmptyText(b.label)) {
              issue(out, 'warn', `${title}に、文字のない選択肢があります`, '', s, b);
            }
            break;
          case 'link':
            if (isEmptyText(b.url) || String(b.url).trim() === 'https://') {
              issue(out, 'warn', `${title}のリンクに、URL が入っていません`, '', s, b);
            }
            break;
          case 'wait':
            if (Number(b.ms) > 60000) {
              issue(out, 'warn', `${title}に、1 分より長く待つブロックがあります`,
                '長すぎるとゴーストが固まったように見えます。', s, b);
            }
            break;
          default:
            break;
        }
      });
    }

    // ---- 設定まわり
    const st = project.settings || {};
    if (st.randomTalkEnabled !== false) {
      if (!talkCount) {
        issue(out, 'warn', '自動でしゃべる設定なのに、ランダムトークが 1 つもありません',
          'イベントの「ランダムトーク」をキャンバスに置いてください。');
      } else if (Number(st.randomTalkInterval) <= 0) {
        issue(out, 'warn', 'しゃべる間隔が 0 秒なので、自動ではしゃべりません',
          'ゴーストタブで秒数を入れてください。');
      }
    }

    // ---- 使われていない変数
    const used = {};
    for (const s of project.scripts || []) {
      walk(s, (b) => {
        if ((b.type === 'var' || b.type === 'set' || b.type === 'change') && b.name) used[b.name] = true;
        if (b.type === 'saori_call' && b.into) used[b.into] = true;
      });
    }
    for (const name of varNames) {
      if (!used[name]) {
        issue(out, 'warn', `変数「${name}」はどこでも使われていません`,
          'いらなければ変数タブで削除できます。');
      }
    }

    // エラーを先に、あとは見つけた順
    out.sort((a, b) => (a.level === b.level ? 0 : (a.level === 'error' ? -1 : 1)));
    return out;
  };

  Lint.countErrors = function (list) {
    return list.filter((x) => x.level === 'error').length;
  };

  N.Lint = Lint;

})(window.NASHI);
