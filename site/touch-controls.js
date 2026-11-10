/* Multi-pointer controls shared by the four launchers. No frame polling. */
'use strict';
(() => {
  const panel=document.getElementById('touch-controls'),pad=document.getElementById('touch-dpad'),actions=document.getElementById('touch-actions'),toggle=document.getElementById('touch-enabled'),hint=document.getElementById('touch-hint');
  const pointers=new Map(),pressed=new Set(),timers=new Set();
  let number=0,enabled=false;
  const profiles={
    1:[{id:'jump',label:'Saltar',style:'primary'}],
    2:[{id:'shoot',label:'Disparar'},{id:'longjump',label:'Salto largo'},{id:'jump',label:'Saltar',style:'primary'}],
    3:[{id:'turbo',label:'Turbo'},{id:'missile',label:'Misil'},{id:'jump',label:'Saltar',style:'primary wide'}],
    4:[{id:'shoot',label:'Disparar'},{id:'jump',label:'Saltar',style:'primary'}]
  };
  const padGlyphs={ArrowUp:'▲',ArrowDown:'▼',ArrowLeft:'◀',ArrowRight:'▶'};
  function send(key,down){
    try {const frame=document.getElementById('emulator').contentWindow;if(typeof frame?.setGameKey==='function')frame.setGameKey(key,down);}catch{}
  }
  function paint(){for(const button of panel.querySelectorAll('[data-key]'))button.setAttribute('aria-pressed',String(pressed.has(button.dataset.key)));}
  function reconcile(){
    const next=new Set();for(const pointer of pointers.values())for(const key of pointer.keys)next.add(key);
    for(const key of pressed)if(!next.has(key)){send(key,false);pressed.delete(key);}
    for(const key of next)if(!pressed.has(key)){send(key,true);pressed.add(key);}
    paint();
  }
  function releaseAll(){
    for(const timer of timers)clearTimeout(timer);timers.clear();pointers.clear();
    for(const key of pressed)send(key,false);pressed.clear();paint();
  }
  function available(){return enabled&&number&&typeof ready!=='undefined'&&ready&&document.getElementById('menu').hidden&&document.getElementById('boot').hidden;}
  function refresh(){const visible=!!available();if(!visible)releaseAll();panel.hidden=!visible;toggle.checked=enabled;hint.hidden=!enabled;}
  function direction(event){
    const rect=pad.getBoundingClientRect(),x=(event.clientX-rect.left)/rect.width,y=(event.clientY-rect.top)/rect.height,keys=new Set();
    if(x<-.25||x>1.25||y<-.25||y>1.25)return keys;
    if(x<.35)keys.add('ArrowLeft');else if(x>.65)keys.add('ArrowRight');
    if(y<.35)keys.add('ArrowUp');else if(y>.65)keys.add('ArrowDown');
    return keys;
  }
  function start(event,isPad){
    if(!available()||event.button!==0)return;
    const button=event.target.closest('button[data-key]');if(!isPad&&!button)return;
    event.preventDefault();const owner=isPad?pad:button;
    if(pointers.delete(event.pointerId))reconcile();
    pointers.set(event.pointerId,{keys:isPad?direction(event):new Set([button.dataset.key]),isPad,began:performance.now()});
    owner.setPointerCapture(event.pointerId);reconcile();
  }
  function finish(event,cancelled=false){
    const pointer=pointers.get(event.pointerId);if(!pointer)return;
    event.preventDefault();
    window.vascoletRefocus?.();
    pointer.finished=true;
    // Keep short action taps visible for at least one guest input poll.
    const remaining=cancelled||pointer.isPad?0:(number===3?160:80)-(performance.now()-pointer.began);
    if(remaining>0){const id=event.pointerId,timer=setTimeout(()=>{timers.delete(timer);if(pointers.get(id)===pointer){pointers.delete(id);reconcile();}},remaining);timers.add(timer);}
    else {pointers.delete(event.pointerId);reconcile();}
  }
  pad.addEventListener('pointerdown',event=>start(event,true));
  pad.addEventListener('pointermove',event=>{const pointer=pointers.get(event.pointerId);if(pointer?.isPad){event.preventDefault();pointer.keys=direction(event);reconcile();}});
  actions.addEventListener('pointerdown',event=>start(event,false));
  panel.querySelector('.touch-utility').addEventListener('pointerdown',event=>start(event,false));
  panel.addEventListener('pointerup',event=>finish(event));
  panel.addEventListener('pointercancel',event=>finish(event,true));
  panel.addEventListener('lostpointercapture',event=>{const pointer=pointers.get(event.pointerId);if(pointer&&!pointer.finished)finish(event,true);});
  panel.addEventListener('contextmenu',event=>event.preventDefault());
  panel.addEventListener('click',event=>{
    const button=event.target.closest('button[data-key]');if(!button)return;event.preventDefault();
    if(event.detail!==0||!available())return;
    const id=Symbol('accessible-key'),pointer={keys:new Set([button.dataset.key])};pointers.set(id,pointer);reconcile();
    const timer=setTimeout(()=>{timers.delete(timer);pointers.delete(id);reconcile();},160);timers.add(timer);
  });
  toggle.addEventListener('change',()=>{enabled=toggle.checked;try{localStorage.setItem('vascolet-touch-controls',JSON.stringify(enabled));}catch{}refresh();});
  window.addEventListener('blur',releaseAll);window.addEventListener('pagehide',releaseAll);window.addEventListener('orientationchange',releaseAll);
  document.addEventListener('visibilitychange',()=>{if(document.hidden)releaseAll();});
  function refreshBindings(){
    const controls=window.VascoletControls;
    if(!controls||!number)return;
    for(const button of panel.querySelectorAll('[data-key]')){
      const action=controls.forLogical(button.dataset.key);
      if(!action)continue;
      const code=controls.bound(action.id),cap=controls.label(code),changed=code!==controls.defaultCode(action.key);
      if(button.closest('#touch-dpad')){
        button.replaceChildren();
        const glyph=document.createElement('span');glyph.textContent=padGlyphs[button.dataset.key]||button.dataset.key;button.append(glyph);
        if(changed){const keycap=document.createElement('small');keycap.textContent=cap;button.append(keycap);}
        button.setAttribute('aria-label',changed?`${action.label} (${cap})`:action.label);
      }else if(button.closest('.touch-utility')){
        button.textContent=cap+(action.id==='confirm'?' ↵':'');
        button.setAttribute('aria-label',`${action.label}: ${cap}`);
      }else{
        const keycap=button.querySelector('small');
        if(keycap)keycap.textContent=cap;else{const fresh=document.createElement('small');fresh.textContent=cap;button.append(fresh);}
        button.setAttribute('aria-label',`${action.label} (${cap})`);
      }
    }
  }
  window.touchControls={refresh,releaseAll,refreshBindings,configure(gameNumber){
    releaseAll();number=gameNumber;actions.replaceChildren();
    window.VascoletControls?.configure(gameNumber);
    for(const item of profiles[number]||[]){
      const button=document.createElement('button');button.type='button';
      button.dataset.key=window.VascoletControls?window.VascoletControls.logical(item.id):'';
      button.className=item.style||'';button.setAttribute('aria-pressed','false');
      const caption=document.createElement('span');caption.textContent=item.label;
      button.append(caption,document.createElement('small'));
      actions.append(button);
    }
    refreshBindings();
    if(number===4)hint.textContent='Doble toque en ← o → para correr. Podés mantener una dirección y una acción a la vez. Tocá el juego para usar sus menús.';
    const coarse=matchMedia('(pointer: coarse)').matches;try{enabled=JSON.parse(localStorage.getItem('vascolet-touch-controls'))??coarse;}catch{enabled=coarse;}refresh();
  }};
  window.VascoletControls?.onChange(()=>window.touchControls.refreshBindings());
  if(typeof game!=='undefined'&&game)window.touchControls.configure(game.number);
})();
