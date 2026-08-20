/* なしスタジオ - 動いているゴーストとの連携（SSTP）
 *
 * SSP を見つけて、いま組んでいるトークをその場のゴーストに喋らせたり、
 * 書き出したゴーストを SSP に入れたりします。
 *
 * app.js より**あとに**読みこまれるので、app.js の中身は N.App 経由で使います。
 * 逆に app.js からここを呼ぶときは、読みこみ順の都合で N.Ssp.xxx() の形になります。
 */
'use strict';

(function (N) {

  const Model = N.Model;
  const Render = N.Render;
  const App = N.App;
  const $ = N.$;
  const api = App.api;
  const escapeHtml = App.escapeHtml;
  const refsFor = App.refsFor;
  const runPreview = App.runPreview;
  const renderRunVars = App.renderRunVars;

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
    let res;
    try {
      res = await runPreview(script);
    } catch (e) { App.toast(e.message, true); return; }
    App.sessionVars = res.vars || {};
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

  N.Ssp = { refreshSsp, sendScriptToSsp, sendEventToSsp, sendCommToSsp, installToSsp };

})(window.NASHI);
