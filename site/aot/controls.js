'use strict';
/* Capa de controles reasignables compartida por la página y el iframe AOT.
   El juego siempre recibe las teclas originales: acá solo se traduce. */
(() => {
  const STORE = 'vascolet-controls-v1';
  const COMMON = [
    {id:'up', label:'Mover arriba', key:'ArrowUp'},
    {id:'down', label:'Mover abajo', key:'ArrowDown'},
    {id:'left', label:'Mover izquierda', key:'ArrowLeft'},
    {id:'right', label:'Mover derecha', key:'ArrowRight'},
    {id:'confirm', label:'Confirmar', key:'Enter'},
    {id:'back', label:'Menú o volver', key:'Escape'}
  ];
  const GAME_ACTIONS = {
    1:[{id:'jump', label:'Saltar', key:' '}],
    2:[{id:'shoot', label:'Disparar', key:'x'},{id:'longjump', label:'Salto largo', key:'s'},{id:'jump', label:'Saltar', key:'z'}],
    3:[{id:'turbo', label:'Turbo', key:'z'},{id:'missile', label:'Misil', key:'x'},{id:'jump', label:'Saltar', key:' '}],
    4:[{id:'shoot', label:'Disparar', key:'x'},{id:'jump', label:'Saltar', key:'z'}]
  };
  let game = Number(globalThis.VASCOLET_GAME_NUMBER) || 0;
  let store = {};
  const listeners = [];
  function defaultCode(key) {
    if (key === ' ') return 'Space';
    if (key.length === 1) {
      const lower = key.toLowerCase();
      if (lower >= 'a' && lower <= 'z') return 'Key' + key.toUpperCase();
      if (key >= '0' && key <= '9') return 'Digit' + key;
    }
    return key;
  }
  function label(code) {
    if (!code) return '';
    if (code === 'Space') return 'Espacio';
    if (code === 'Enter') return 'Enter';
    if (code === 'Escape') return 'Esc';
    if (code.indexOf('Arrow') === 0) return 'Flecha ' + ({Up:'↑', Down:'↓', Left:'←', Right:'→'}[code.slice(5)] || '');
    if (/^Key[A-Z]$/.test(code)) return code.slice(3);
    if (/^Digit[0-9]$/.test(code)) return code.slice(5);
    if (/^Numpad/.test(code)) return 'Num ' + code.slice(6);
    if (/^Shift/.test(code)) return 'Shift';
    if (/^Control/.test(code)) return 'Ctrl';
    if (/^Alt/.test(code)) return 'Alt';
    if (/^Meta/.test(code)) return 'Meta';
    return code;
  }
  function read() { try { store = JSON.parse(localStorage.getItem(STORE)) || {}; } catch { store = {}; } }
  function notify() { for (const callback of listeners.slice()) { try { callback(); } catch {} } }
  function write() { try { localStorage.setItem(STORE, JSON.stringify(store)); } catch {} notify(); }
  function all() { return (GAME_ACTIONS[game] || []).concat(COMMON); }
  function deviation(id) { const entry = store[game]; return entry && typeof entry[id] === 'string' ? entry[id] : ''; }
  function action(id) { return all().find(item => item.id === id) || null; }
  function bound(id) { const item = action(id); return item ? (deviation(id) || defaultCode(item.key)) : ''; }
  function logical(id) { const item = action(id); return item ? item.key : ''; }
  function forLogical(key) { return all().find(item => item.key === key) || null; }
  function actions() {
    return all().map(item => {
      const code = defaultCode(item.key), current = bound(item.id);
      return {id:item.id, label:item.label, key:item.key, code, bound:current, changed:current !== code};
    });
  }
  function set(id, code) {
    const target = action(id);
    if (!target || !code) return;
    if (!store[game]) store[game] = {};
    const previous = bound(id);
    for (const other of all()) {
      if (other.id === id || bound(other.id) !== code) continue;
      if (previous === defaultCode(other.key)) delete store[game][other.id];
      else store[game][other.id] = previous;
    }
    if (code === defaultCode(target.key)) delete store[game][id];
    else store[game][id] = code;
    write();
  }
  function reset() { if (store[game]) { delete store[game]; write(); } }
  /* Devuelve la tecla lógica que espera el juego, o {block} si la tecla física
     quedó libre por una reasignación, o null para dejarla pasar sin tocar. */
  function translate(code) {
    if (!code) return null;
    const items = all();
    for (const item of items) if (bound(item.id) === code) return {key:item.key, code:defaultCode(item.key)};
    for (const item of items) if (defaultCode(item.key) === code) return {block:true};
    return null;
  }
  function configure(number) { game = Number(number) || 0; read(); notify(); }
  function reload() { read(); notify(); }
  function onChange(callback) { listeners.push(callback); }
  read();
  addEventListener('storage', event => { if (event.key === null || event.key === STORE) reload(); });
  globalThis.VascoletControls = {configure, reload, onChange, actions, action, logical, forLogical, bound, label, defaultCode, set, reset, translate};
})();
