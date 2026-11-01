'use strict';
let game;
const $ = id => document.getElementById(id);
async function load() {
  try {
    const response = await fetch('game.json');
    if (!response.ok) throw new Error(`Configuración: HTTP ${response.status}`);
    game = await response.json();
    document.title = `Vascolet ${game.number} · EXE original`;
    $('number').textContent = game.number;
    $('filename').textContent = game.exe;
    $('repo').href = `https://github.com/frajusu/vascolet-exe-${game.number}`;
    $('download-size').textContent = `Primera carga: aproximadamente ${Math.ceil((game.zipBytes + game.wineBytes + 5000000) / 1000000)} MB.`;
    for (let n = 1; n <= 4; n++) {
      const link = document.createElement('a');
      link.href = `../vascolet-exe-${n}/`;
      link.textContent = n;
      link.setAttribute('aria-label', `Vascolet ${n}`);
      if (n === game.number) link.setAttribute('aria-current', 'page');
      $('games').append(link);
    }
    $('status').textContent = 'Listo para iniciar. Compatibilidad del juego en prueba.';
    $('start').disabled = false;
  } catch (error) {
    $('status').textContent = `No se pudo preparar la página. Recargá para reintentar. ${error.message}`;
  }
}
function launch(desktop = false) {
  if (!game) return;
  const mode = $('engine').value;
  const params = new URLSearchParams({root: 'boxedwine.zip', rootBase: '../', app: 'game.zip', appBase: '../../', p: game.exe, auto: 'true', sound: $('sound').value, storage: 'memory', resolution: '1024x768', 'wine-override': 'nomono', registry: 'gl', desktop: String(desktop)});
  $('welcome').hidden = true;
  $('emulator').hidden = false;
  $('emulator').src = `runtime/${mode}/boxedwine.html?${params}`;
  $('restart').disabled = false;
  $('status').textContent = `Prueba iniciada con ${mode === 'jit' ? 'JIT' : 'intérprete'}${desktop ? ' · Escritorio Wine' : ''}. Mirá la pantalla y la consola del emulador; el arranque puede tardar.`;
}
$('start').addEventListener('click', () => launch());
$('restart').addEventListener('click', () => launch());
$('desktop').addEventListener('click', () => launch(true));
$('fullscreen').addEventListener('click', async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await $('player').requestFullscreen();
  } catch { $('status').textContent = 'El navegador no permitió pantalla completa. Podés ampliar la ventana.'; }
});
load();
