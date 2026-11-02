'use strict';
const $ = id => document.getElementById(id);
let game, settings, currentMode, currentRun, started, ready = false, watchdog;
const logs = [];
const pending = new Map();
let serial = 0;
window.vascoletBackup = null;
function note(text) { logs.push(String(text)); if (logs.length > 600) logs.shift(); }
function preference(key, fallback) { try { return JSON.parse(localStorage.getItem(key)) ?? fallback; } catch { return fallback; } }
function remember() { settings = {engine: $('engine').value, sound: $('sound').checked}; try { localStorage.setItem(`vascolet-exe-${game.number}-settings`, JSON.stringify(settings)); } catch {} }
function menu(open) { $('menu').hidden = !open; $('menu-toggle').setAttribute('aria-expanded', String(open)); if (open) $('close-menu').focus(); else { $('emulator').focus(); } }
function failure(text) { clearTimeout(watchdog); $('boot').hidden = false; $('boot-text').textContent = text; $('progress').hidden = true; $('retry').hidden = false; note(text); }
function request(action, data) {
  return new Promise((resolve, reject) => {
    const id = ++serial;
    const timer = setTimeout(() => { pending.delete(id); reject(new Error('El emulador no respondió. Esperá a que termine de iniciar.')); }, 20000);
    pending.set(id, {resolve, reject, timer});
    $('emulator').contentWindow.postMessage({channel:'vascolet-command', id, action, data}, location.origin);
  });
}
async function flush() {
  if (!ready) return;
  try { await request('sync'); } catch (error) { $('save-status').textContent = error.message; throw error; }
}
async function isolated() {
  if (!('serviceWorker' in navigator)) return false;
  try {
    await navigator.serviceWorker.register('sw.js');
    await navigator.serviceWorker.ready;
    if (!navigator.serviceWorker.controller) {
      await Promise.race([new Promise(resolve => navigator.serviceWorker.addEventListener('controllerchange', resolve, {once:true})), new Promise(resolve => setTimeout(resolve, 3000))]);
    }
    // The original navigation needs a single reload to receive the isolation headers.
    const reloadKey = `vascolet-isolation-reload-${game.number}`;
    if (!crossOriginIsolated && navigator.serviceWorker.controller && !sessionStorage.getItem(reloadKey)) {
      sessionStorage.setItem(reloadKey, '1'); location.reload(); return null;
    }
    return crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined';
  } catch (error) { note(`Aislamiento: ${error.message}`); return false; }
}
function launch() {
  ready = false; started = performance.now(); clearTimeout(watchdog);
  for (const item of pending.values()) { clearTimeout(item.timer); item.reject(new Error('Se reinició el emulador.')); } pending.clear();
  const canThread = crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined';
  currentMode = settings.engine === 'jit' || !canThread ? 'jit' : 'mt';
  const params = new URLSearchParams({root:'boxedwine.zip',rootBase:'../',app:'game.zip',appBase:'../../',p:game.exe,auto:'true',sound:String(settings.sound),storage:'indexeddb',rootfs:'indexeddb',storageKey:`vascolet-exe-${game.number}`,resolution:'800x600','wine-override':'nomono'});
  currentRun = `${Date.now()}-${++serial}`; params.set('run',currentRun);
  if(new URLSearchParams(location.search).get('record')==='1')params.set('jit-record','true');
  $('boot').hidden = false; $('boot-text').textContent = 'Cargando…'; $('progress').hidden = false; $('progress').removeAttribute('value'); $('retry').hidden = true;
  $('runtime-status').textContent = `${currentMode === 'mt' ? 'JIT multihilo' : 'JIT de un hilo'} · iniciando`;
  $('emulator').hidden = false;
  $('emulator').src = `runtime/${currentMode}/boxedwine.html?${params}`;
  watchdog = setTimeout(() => failure('El arranque está tardando. Abrí el menú para ver el diagnóstico o cambiar de motor.'), 150000);
}
async function restart() { try { await flush(); remember(); menu(false); launch(); } catch {} }
function download(blob, name) { const url = URL.createObjectURL(blob); const a = document.createElement('a'); a.href = url; a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(url),1000); }
window.addEventListener('message', event => {
  if (event.origin !== location.origin || event.source !== $('emulator').contentWindow || event.data?.channel !== 'vascolet-runtime' || event.data.run !== currentRun) return;
  const message = event.data;
  if (message.type === 'reply') { const item = pending.get(message.id); if (item) { clearTimeout(item.timer); pending.delete(message.id); message.error ? item.reject(new Error(message.error)) : item.resolve(message.data); } }
  if (message.type === 'log') note(message.text);
  if (message.type === 'progress' && !ready) { $('boot-text').textContent = message.text; if (message.total > 0) { $('progress').max = message.total; $('progress').value = message.loaded; } else $('progress').removeAttribute('value'); }
  if (message.type === 'ready') { ready = true; clearTimeout(watchdog); $('boot').hidden = true; window.vascoletBackup = null; const seconds = ((performance.now()-started)/1000).toFixed(1); $('runtime-status').textContent = `${currentMode === 'mt' ? 'JIT multihilo' : 'JIT de un hilo'} · arranque ${seconds} s`; $('emulator').focus(); }
  if (message.type === 'saved') { $('save-status').textContent = `Archivos sincronizados · ${new Date().toLocaleTimeString()}`; }
  if (message.type === 'save-error') $('save-status').textContent = `No se pudo guardar: ${message.text}. Exportá una copia antes de cerrar.`;
  if (message.type === 'error') {
    if(!ready && currentMode==='mt' && settings.engine==='auto') { note(`Multihilo no disponible: ${message.text}. Cambio a JIT de un hilo.`); settings.engine='jit'; launch(); }
    else failure(message.text);
  }
  if (message.type === 'metrics') $('metrics').textContent = message.text;
});
$('menu-toggle').onclick = () => menu($('menu').hidden);
$('close-menu').onclick = () => menu(false);
document.addEventListener('keydown', event => { if (event.key === 'Escape' && !$('menu').hidden) menu(false); });
$('restart').onclick = restart;
$('retry').onclick = restart;
$('fullscreen').onclick = async () => { try { if (document.fullscreenElement) await document.exitFullscreen(); else await document.documentElement.requestFullscreen(); menu(false); } catch (error) { note(error.message); } };
$('export-save').onclick = async () => { try { const data = await request('export'); download(new Blob([JSON.stringify({format:'vascolet-save-v1',game:game.number,files:data})],{type:'application/json'}),`vascolet-${game.number}-partida.json`); } catch(error) { $('save-status').textContent = error.message; } };
$('import-save').onclick = () => $('save-file').click();
$('save-file').onchange = async event => {
  const file = event.target.files[0]; if (!file) return;
  try {
    if (file.size > 45000000) throw new Error('La copia supera el tamaño permitido.');
    const backup = JSON.parse(await file.text());
    if (backup.format !== 'vascolet-save-v1' || backup.game !== game.number || !Array.isArray(backup.files)) throw new Error('La copia no corresponde a este Vascolet.');
    await flush(); window.vascoletBackup = backup.files; menu(false); launch();
  } catch(error) { $('save-status').textContent = error.message; }
  event.target.value = '';
};
$('diagnostics').addEventListener('toggle', () => { if ($('diagnostics').open) $('log').textContent = logs.join('\n'); });
$('download-log').onclick = () => download(new Blob([logs.join('\n')],{type:'text/plain'}),`vascolet-${game.number}.log`);
document.addEventListener('visibilitychange', () => { if (document.hidden && ready) flush().catch(() => {}); });
async function boot() {
  try {
    const response = await fetch('game.json'); if (!response.ok) throw new Error(`Configuración: HTTP ${response.status}`);
    game = await response.json(); document.title = `Vascolet ${game.number}`; $('title').textContent = document.title; $('repo').href = `https://github.com/frajusu/vascolet-exe-${game.number}`;
    settings = preference(`vascolet-exe-${game.number}-settings`, {engine:'auto',sound:true});
    if (!['auto','mt','jit'].includes(settings.engine)) settings.engine = 'auto';
    $('engine').value = settings.engine; $('sound').checked = settings.sound !== false;
    for (let number=1;number<=4;number++) {
      const a = document.createElement('a'); a.href = `../vascolet-exe-${number}/`; a.textContent = number; a.setAttribute('aria-label',`Vascolet ${number}`); if (number===game.number) a.setAttribute('aria-current','page');
      a.onclick = async event => { if (event.ctrlKey || event.metaKey || event.shiftKey || event.button!==0) return; event.preventDefault(); try { await flush(); location.href = a.href; } catch {} };
      $('games').append(a);
    }
    const result = await isolated(); if (result === null) return;
    navigator.storage?.persist?.().catch(() => {});
    launch();
  } catch(error) { failure(`No se pudo iniciar. ${error.message}`); }
}
boot();
