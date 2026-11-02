/* Integration layer for the pinned GPL Boxedwine shell. No guest EXE changes. */
'use strict';
(() => {
  const params = new URLSearchParams(location.search);
  const send = (type, data={}) => parent.postMessage({channel:'vascolet-runtime',run:params.get('run'),type,...data},location.origin);
  const number = Number((params.get('storageKey') || '').match(/\d+$/)?.[0]);
  const mode = location.pathname.includes('/mt/') ? 'mt' : 'jit';
  getJitCacheZipFilename = () => `${mode}-game-jit-modules.zip`;
  // No speculative 404 or unsafe replay cache: this build uses live JIT only.
  fetchServerJitCache = callback => callback();
  const bootTime = performance.now();
  let rendering = false, ready = false, draws = 0, syncing = false, frames = 0, lastFrames = 0;
  window.vascoletRuntimeStats = {frames:0, fps:0, started:performance.now()};
  let logQueue = [];
  document.getElementById('showConsole').checked = false;
  // Fetch the large independent archives concurrently, instead of serially.
  const downloads = new Map();
  const transfer = new Map();
  function archive(url, label) {
    const absolute = new URL(url,location.href).href;
    if (!downloads.has(absolute)) downloads.set(absolute,(async () => {
      const response = await fetch(absolute);
      if (!response.ok) throw new Error(`${label}: HTTP ${response.status}`);
      const total = Number(response.headers.get('Content-Length')) || 0;
      const reader = response.body.getReader(); const chunks=[]; let loaded=0, last=0;
      transfer.set(label,{loaded,total});
      for (;;) {
        const {done,value} = await reader.read(); if(done)break;
        chunks.push(value); loaded+=value.length; transfer.set(label,{loaded,total});
        if(performance.now()-last>150) { last=performance.now(); progress(); }
      }
      const result = new Uint8Array(loaded); let offset=0; for(const part of chunks){result.set(part,offset);offset+=part.length;}
      transfer.set(label,{loaded,total:loaded}); progress(); return result;
    })());
    return downloads.get(absolute);
  }
  function progress() {
    let loaded=0,total=0; for(const item of transfer.values()){loaded+=item.loaded;total+=item.total;}
    send('progress',{text:loaded<total?`Descargando… ${Math.round(loaded/1000000)} / ${Math.round(total/1000000)} MB`:'Iniciando juego…',loaded,total});
  }
  for(const [url,label] of [[`${params.get('rootBase')}${params.get('root')}`,'Wine'],[`${params.get('appBase')}${params.get('app')}`,'Juego']]) archive(url,label).catch(error=>send('error',{text:error.message}));
  loadFile = (base, filename, callback) => archive(base+filename,filename).then(callback).catch(error=>send('error',{text:error.message}));
  const originalParams = getEmulatorParams;
  getEmulatorParams = () => {
    const args=originalParams(); const index=args.indexOf('/bin/wine');
    // No cmd / reg processes. The pinned Wine prefix already uses WineD3D WebGL.
    args.splice(index,0,'-vsync','0','-env','WINEDEBUG=-all');
    const overrides=args.findIndex(arg=>String(arg).startsWith('WINEDLLOVERRIDES='));
    if(overrides>=0)args[overrides]+=';winemenubuilder.exe=d';
    return args;
  };
  const originalInit = initBrowserFilesystem;
  initBrowserFilesystem = callback => originalInit(() => {
    try {
      const backup=parent.vascoletBackup;
      if(backup) restore(backup);
      callback();
    } catch(error){send('error',{text:`No se pudo restaurar la partida: ${error.message}`});}
  });
  Module.print = (...args) => {
    const text=args.join(' '); logQueue.push(`[${((performance.now()-bootTime)/1000).toFixed(1)}s] ${text}`);
    // With WINEDEBUG disabled, the emulator's own WineD3D marker remains visible.
    if(text.includes('WebGL WineD3D build timestamp') || text.includes('WebGL-compatible profile')) rendering=true;
    if(text.includes('Showing Window')) send('progress',{text:'Iniciando juego…'});
  };
  Module.printErr = (...args) => logQueue.push(args.join(' '));
  Module.setStatus = text => { if(text && !ready) send('progress',{text:'Preparando emulador…'}); };
  Module.onAbort = reason => send('error',{text:`El emulador se detuvo: ${reason}. Probá JIT de un hilo desde el menú.`});
  const present = Module.boxedwinePresentFrame;
  Module.boxedwinePresentFrame = function(...args) { if(args[0])frames++; return present.apply(this,args); };
  const drawImage = CanvasRenderingContext2D.prototype.drawImage;
  CanvasRenderingContext2D.prototype.drawImage = function(...args) {
    if(mode==='jit' && this.canvas.id==='boxedwine-webgl-canvas-0')frames++;
    return drawImage.apply(this,args);
  };
  const nativeError=window.onerror;
  window.onerror = (message,...args) => {send('error',{text:String(message)}); if(nativeError)nativeError(message,...args);};
  window.addEventListener('unhandledrejection', event=>send('error',{text:String(event.reason?.message||event.reason)}));
  // Count actual graphics work, not requestAnimationFrame callbacks (which can
  // report 60 even while a game is stalled). This is a diagnostic draw count.
  for(const proto of [window.WebGLRenderingContext?.prototype,window.WebGL2RenderingContext?.prototype]) {
    if(!proto)continue;
    for(const name of ['drawElements','drawArrays']) {
      const original=proto[name]; if(!original)continue;
      proto[name]=function(...args){draws++; return original.apply(this,args);};
    }
  }
  let priorDraws=0, renderSeenAt=0;
  setInterval(() => {
    for(const text of logQueue.splice(0)) send('log',{text});
    if(rendering && (frames>0 || mode==='jit' && draws>0) && !renderSeenAt)renderSeenAt=performance.now();
    if(!ready && renderSeenAt && performance.now()-renderSeenAt>2000){ready=true;document.body.classList.add('game-ready');send('ready');}
    window.vascoletRuntimeStats.frames=frames;
    window.vascoletRuntimeStats.fps=frames-lastFrames;
    send('metrics',{text:`Motor: ${mode}\nAislamiento: ${crossOriginIsolated?'sí':'no'}\n${mode==='mt'?`Presentaciones / segundo: ${frames-lastFrames}`:`Dibujados GL / segundo: ${draws-priorDraws} (no son FPS)`}\nTiempo activo: ${Math.round((performance.now()-bootTime)/1000)} s`});
    lastFrames=frames;
    priorDraws=draws;
  },1000);
  // autoPersist handles individual writes. Explicit flushes cover navigation,
  // backups and idle periods, without scanning the FS on every rendered frame.
  function sync(){
    if(syncing)return syncing;
    if(typeof FS==='undefined')return Promise.reject(new Error('El juego todavía está iniciando.'));
    syncing=new Promise((resolve,reject)=>FS.syncfs(false,error=>{
      if(error){send('save-error',{text:String(error)});reject(error);}else{send('saved');resolve();}
    })).finally(()=>{syncing=false;});
    return syncing;
  }
  setInterval(()=>{if(ready)sync().catch(()=>{});},30000);
  function snapshot(){
    const files=[]; let size=0;
    function walk(path){
      for(const name of FS.readdir(path)){if(name==='.'||name==='..')continue;const full=`${path}/${name}`,stat=FS.lstat(full);
        if(FS.isLink(stat.mode))continue;
        if(FS.isDir(stat.mode)){walk(full);continue;}
        if(!FS.isFile(stat.mode))continue;
        const relative=full.slice(ROOT.length+1); if(/^(tmp|dev|proc|mnt)\//.test(relative))continue;
        size+=stat.size;if(size>32*1024*1024)throw new Error('La copia de archivos supera 32 MB.');
        const bytes=FS.readFile(full);let text='';for(let at=0;at<bytes.length;at+=32768)text+=String.fromCharCode(...bytes.subarray(at,at+32768));
        files.push({path:relative,data:btoa(text)});
      }
    }
    walk(ROOT);return files;
  }
  function restore(files){
    if(files.length>5000)throw new Error('Demasiados archivos.');
    let size=0;
    const validated=files.map(file=>{
      if(typeof file.path!=='string'||typeof file.data!=='string'||file.path.startsWith('/')||file.path.includes('\\')||file.path.split('/').some(part=>!part||part==='.'||part==='..'))throw new Error('Ruta de guardado inválida.');
      const bytes=Uint8Array.from(atob(file.data),c=>c.charCodeAt(0));size+=bytes.length;if(size>32*1024*1024)throw new Error('Copia demasiado grande.');
      return {path:file.path,bytes};
    });
    for(const file of validated){const full=`${ROOT}/${file.path}`;const parts=file.path.split('/');let at=ROOT;for(const part of parts.slice(0,-1)){at+=`/${part}`;try{if(FS.isLink(FS.lstat(at).mode))throw new Error('Enlace no permitido.');}catch(error){if(error.message==='Enlace no permitido.')throw error;FS.mkdirTree(at);}}try{if(FS.isLink(FS.lstat(full).mode))throw new Error('Enlace no permitido.');}catch(error){if(error.message==='Enlace no permitido.')throw error;}FS.writeFile(full,file.bytes);}
    send('saved');
  }
  window.addEventListener('message', async event=>{
    if(event.origin!==location.origin||event.source!==parent||event.data?.channel!=='vascolet-command')return;
    const {id,action}=event.data;
    try{
      let data;
      if(action==='sync')await sync();
      else if(action==='export'){await sync();data=snapshot();}
      else throw new Error('Acción desconocida.');
      send('reply',{id,data});
    }catch(error){send('reply',{id,error:error.message});}
  });
})();
