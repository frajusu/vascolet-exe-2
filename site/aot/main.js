/* A single worker; bounded queues/images, audio decoded only when played. */
const P=VascoletWire,canvas=document.getElementById('game'),context=canvas.getContext('2d',{alpha:false});
const GAME_NUMBER=Number(globalThis.VASCOLET_GAME_NUMBER||2);
const status=document.getElementById('status'),start=document.getElementById('start'),stop=document.getElementById('stop'),logBox=document.getElementById('log');
const backend=createAOTBrowserHost(canvas),rawSounds=new Map(),durations=new Map(),pendingAudio=new Map();
let worker=null,wire=null,serial=0,importedSave=null,watchdog=null,lastStats={},lastReport=null,lines=[],saveCount=0,frameSeen=0,bitmapClosed=0,imageRpcClosed=0,frameImages=[null,null];
let drawMs=0,lastFrameAt=0,hitchCount=0,hitchMaxMs=0;
const saveWaiters=new Map();
function requestSave(){if(!worker)return Promise.reject(Error('El juego no está iniciado.'));const request=Atomics.add(wire.c,P.SAVE_REQUEST,1)+1;return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{saveWaiters.delete(request);reject(Error('El guardado tardó demasiado.'));},15000);saveWaiters.set(request,{resolve,reject,timer});});}
function settleSave(request,save,error){for(const [id,item] of saveWaiters)if(id<=request){clearTimeout(item.timer);saveWaiters.delete(id);error?item.reject(error):item.resolve(save);}}
const say=text=>{if(status.textContent===text)return;status.textContent=text;dispatchEvent(new CustomEvent('vascolet-status',{detail:text}));};
function log(text){lines.push(String(text));if(lines.length>120)lines.shift();logBox.textContent=lines.join('\n');dispatchEvent(new CustomEvent('vascolet-log',{detail:String(text)}));}
function normalize(path){const parts=[];for(const p of path.split('/')){if(!p||p==='.')continue;if(p==='..')parts.pop();else parts.push(p);}if(parts[0]==='game')parts.shift();return parts.join('/');}
function createWire(){const control=new SharedArrayBuffer(128),input=new SharedArrayBuffer(P.QUEUE*20),audio=new SharedArrayBuffer(P.QUEUE*4),pixels=new SharedArrayBuffer(P.PIXEL_BYTES),frame=new SharedArrayBuffer(P.PIXEL_BYTES);return {control,input,audio,pixels,frame,c:new Int32Array(control),events:new Uint8Array(input),ended:new Int32Array(audio),bytes:new Uint8Array(pixels),framePixels:new Uint8Array(frame),number:new DataView(control)};}
backend.events.push=function(event){
  if(!worker||!wire)return 0;const w=wire,c=w.c,read=Atomics.load(c,P.INPUT_READ),write=Atomics.load(c,P.INPUT_WRITE);
  const view=new DataView(event.buffer,event.byteOffset,event.byteLength);if([4,5,6].includes(event[0])){Atomics.store(c,P.MOUSE_XY,view.getUint16(4,true)|(view.getUint16(6,true)<<16));let buttons=Atomics.load(c,P.MOUSE_BUTTONS);if(event[0]===4)buttons=event[2];else{const mask=1<<(event[2]-1);buttons=event[3]?buttons|mask:buttons&~mask;}Atomics.store(c,P.MOUSE_BUTTONS,buttons);}
  if(write-read>=P.QUEUE){Atomics.store(c,P.INPUT_OVERFLOW,1);return 0;}w.events.set(event,(write%P.QUEUE)*20);Atomics.store(c,P.INPUT_WRITE,write+1);return write-read+1;
};
function publishFinished(){if(!worker||!wire){backend.finished.length=0;return;}const c=wire.c;while(backend.finished.length){const read=Atomics.load(c,P.AUDIO_READ),write=Atomics.load(c,P.AUDIO_WRITE);if(write-read>=P.QUEUE){finish('La cola de audio alcanzó su límite.');return;}wire.ended[write%P.QUEUE]=backend.finished.shift();Atomics.store(c,P.AUDIO_WRITE,write+1);}}
setInterval(publishFinished,20);
function oggDuration(raw){
  try{const view=new DataView(raw.buffer,raw.byteOffset,raw.byteLength),streams=new Map();let p=0;
    while(p+27<=raw.length){if(raw[p]!==79||raw[p+1]!==103||raw[p+2]!==103||raw[p+3]!==83)return 0;const n=raw[p+26],body=p+27+n;if(body>raw.length)return 0;let size=0;for(let i=0;i<n;i++)size+=raw[p+27+i];if(body+size>raw.length)return 0;const id=view.getUint32(p+14,true);let stream=streams.get(id);if(!stream){stream={rate:0,samples:0};streams.set(id,stream);}if(size>=16&&raw[body]===1&&String.fromCharCode(...raw.subarray(body+1,body+7))==='vorbis')stream.rate=view.getUint32(body+12,true);const granule=view.getBigUint64(p+6,true);if(granule!==0xffffffffffffffffn&&granule<=BigInt(Number.MAX_SAFE_INTEGER))stream.samples=Math.max(stream.samples,Number(granule));p=body+size;
    }let seconds=0;for(const s of streams.values()){if(!s.rate)return 0;seconds+=s.samples/s.rate;}return seconds;
  }catch{return 0;}
}
async function ensureAudio(path){path=normalize(path);if(backend.soundDuration(path)>0)return;if(pendingAudio.has(path))return pendingAudio.get(path);const raw=rawSounds.get(path);if(!raw)throw Error('Falta el sonido original: '+path);const task=backend.preloadAudio(['/game/'+path],{readFile:()=>raw});pendingAudio.set(path,task);try{await task;}finally{pendingAudio.delete(path);}}
async function handleRPC(message,w,token){
  let result=0,error='';try{
    const args=message.args;
    if(message.method==='persist'){
      saveCount++;
      const save=VascoletSaveStore.pack(args[0]);await VascoletSaveStore.put(save);dispatchEvent(new CustomEvent('vascolet-saved',{detail:{files:save.files.length}}));result=1;
    }else if(message.method==='image'){
      const bitmap=await createImageBitmap(new Blob([message.raw]));try{const bytes=bitmap.width*bitmap.height*4;if(bytes>P.PIXEL_BYTES)throw Error('La imagen supera el límite de 16 MiB');const image=new OffscreenCanvas(bitmap.width,bitmap.height),ctx=image.getContext('2d');ctx.drawImage(bitmap,0,0);w.bytes.set(ctx.getImageData(0,0,bitmap.width,bitmap.height).data);Atomics.store(w.c,P.IMAGE_W,bitmap.width);Atomics.store(w.c,P.IMAGE_H,bitmap.height);Atomics.store(w.c,P.IMAGE_LENGTH,bytes);result=1;}finally{bitmap.close();imageRpcClosed++;}
    }else if(message.method==='soundDuration'){
      const name=normalize(args[0]);result=durations.get(name)||0;if(!result){try{await ensureAudio(name);}catch{}result=backend.soundDuration(name);}
    }else if(message.method==='play'){
      await ensureAudio(args[1]);if(token!==serial||!worker)throw Error('Prueba detenida');result=backend.play(...args);backend.trimDecodedSounds?.(32*1024*1024);
    }else if(message.method==='playMusic'){
      try{await ensureAudio(args[0]);}catch{}
      if(token!==serial||!worker)throw Error('Prueba detenida');result=backend.playMusic(...args);backend.trimDecodedSounds?.(32*1024*1024);
    }else if(['init','cursor','openAudio','allocateChannels','halt','freeSound','pause','volume','playing','haltMusic','pauseMusic','musicVolume','musicPlaying','musicFinished'].includes(message.method)){
      if(token!==serial||!worker)throw Error('Prueba detenida');result=backend[message.method](...args);if(result===undefined)result=0;
    }else throw Error('Operación del puente no soportada: '+message.method);
    publishFinished();
  }catch(e){error=String(e);}
  if(error){const encoded=new TextEncoder().encode(error).subarray(0,P.ERROR_BYTES);w.bytes.set(encoded);Atomics.store(w.c,P.ERROR_LENGTH,encoded.length);Atomics.store(w.c,P.RPC_ERROR,1);}else Atomics.store(w.c,P.RPC_ERROR,0);
  w.number.setFloat64(P.NUMBER_OFFSET,Number(result)||0,true);Atomics.store(w.c,P.RPC_DONE,message.id);Atomics.notify(w.c,P.RPC_DONE);
}
function download(name,data){const url=URL.createObjectURL(new Blob([data],{type:'application/json'})),a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
function saveDiagnostic(text){try{sessionStorage.setItem('vascolet-last-stop-'+GAME_NUMBER,JSON.stringify({text,time:new Date().toISOString(),stats:lastStats,report:lastReport,logs:lines}));}catch{}}
function finish(text,failed=false){saveDiagnostic(text);serial++;if(wire){Atomics.store(wire.c,P.STOP,1);Atomics.notify(wire.c,P.RPC_DONE);Atomics.notify(wire.c,P.FRAME_ACK);}worker?.terminate();worker=null;clearTimeout(watchdog);backend.halt(-1);backend.finished.length=0;backend.trimDecodedSounds?.(32*1024*1024);for(const item of saveWaiters.values()){clearTimeout(item.timer);item.reject(Error(text));}saveWaiters.clear();start.disabled=false;stop.disabled=true;document.getElementById('launch').hidden=failed;document.getElementById('failure').hidden=!failed;document.getElementById('failure-text').textContent=text;say(text);if(failed)dispatchEvent(new CustomEvent('vascolet-failure',{detail:text}));}
start.onclick=async()=>{
  if(worker)return;if(!crossOriginIsolated||typeof SharedArrayBuffer==='undefined'){say('Abre la prueba desde el servidor local indicado, no como archivo.');return;}
  start.disabled=true;lines=[];lastReport=null;rawSounds.clear();durations.clear();backend.halt(-1);backend.finished.length=0;wire=createWire();frameImages=[null,null];const token=++serial;lastStats={};
  try{
    const saved=importedSave||await VascoletSaveStore.load();if(importedSave)await VascoletSaveStore.put(importedSave);importedSave=null;void backend.resumeAudio().catch(log);worker=new Worker('./engine-worker.js'+(globalThis.VASCOLET_BUILD_ID?'?v='+encodeURIComponent(VASCOLET_BUILD_ID):''));stop.disabled=false;document.getElementById('launch').hidden=true;document.getElementById('failure').hidden=true;canvas.focus();say('Preparando los recursos originales…');
    worker.onmessage=({data})=>{
      if(token!==serial)return;
      if(data.type==='rpc'){void handleRPC(data,wire,token);return;}
      if(data.type==='sound-file'){rawSounds.set(data.path,data.raw);const duration=oggDuration(data.raw);if(duration)durations.set(data.path,duration);return;}
      if(data.type==='resize'){backend.resize(data.w,data.h);return;}
      if(data.type==='caption'){canvas.setAttribute('aria-label',data.text);return;}
      if(data.type==='frame'){
        frameSeen++;
        const draw0=performance.now();
        if(lastFrameAt){const gap=draw0-lastFrameAt;if(gap>150)hitchCount++;if(gap>hitchMaxMs)hitchMaxMs=gap;}
        lastFrameAt=draw0;
        try{
          if(data.bitmap){ /* legacy worker: per-frame ImageBitmap */
            if(canvas.width!==data.bitmap.width||canvas.height!==data.bitmap.height)backend.resize(data.bitmap.width,data.bitmap.height);
            context.drawImage(data.bitmap,0,0);
          }else{
            if(canvas.width!==data.w||canvas.height!==data.h)backend.resize(data.w,data.h);
            const bytes=data.w*data.h*4,off=data.off||0,slot=data.slot||0;
            if(off+bytes>wire.framePixels.length)throw Error('El frame supera el límite de memoria compartida');
            /* View the shared frame buffer directly (one ImageData per buffer slot):
             * skip the per-frame 4 MiB copy. */
            let fi=frameImages[slot];
            if(!fi||fi.w!==data.w||fi.h!==data.h){
              let shared=true,img;
              try{img=new ImageData(new Uint8ClampedArray(wire.frame.buffer,off,bytes),data.w,data.h);}
              catch{shared=false;img=context.createImageData(data.w,data.h);}
              fi=frameImages[slot]={img,shared,w:data.w,h:data.h};
            }
            if(!fi.shared)fi.img.data.set(wire.framePixels.subarray(off,off+bytes));
            context.putImageData(fi.img,0,0);
          }
          drawMs+=performance.now()-draw0;data.stats.drawMs=drawMs;data.stats.hitchCount=hitchCount;data.stats.hitchMaxMs=hitchMaxMs;
          lastStats=data.stats;if(data.id===1)dispatchEvent(new CustomEvent('vascolet-ready'));say('Juego en pantalla · clic para enfocar · Enter para continuar');
        }
        finally{if(data.bitmap){data.bitmap.close();bitmapClosed++;}Atomics.store(wire.c,P.FRAME_ACK,data.id);Atomics.notify(wire.c,P.FRAME_ACK);}return;
      }
      if(data.type==='progress'){say(data.text);return;}
      if(data.type==='log'){log(data.line);/*if(data.line.indexOf('CKPT')!==-1){try{alert(data.line);}catch{}}*/try{const item=JSON.parse(data.line);if(item.game)lastReport=item;}catch{}return;}
      if(data.type==='save'){
        saveCount++;
        void (async()=>{try{const save=VascoletSaveStore.pack(data.files);await VascoletSaveStore.put(save);settleSave(data.request,save);dispatchEvent(new CustomEvent('vascolet-saved',{detail:{files:save.files.length}}));}catch(error){settleSave(data.request,null,error);dispatchEvent(new CustomEvent('vascolet-save-error',{detail:String(error)}));}})();return;
      }
      if(data.type==='save-error'){settleSave(data.request,null,Error(data.message));say(data.message);return;}
      if(data.type==='failure'){log(data.stack||data.message);finish('La prueba se detuvo: '+data.message,true);return;}
      if(data.type==='done'){finish(lastReport?'La prueba se detuvo: '+lastReport.reason:'El juego terminó.',data.status!==0);}
    };
    worker.onerror=event=>{log(event.message);finish('Error del worker: '+event.message,true);};
    const base=new URL('../0_vascolets_web/recompiler-lab/game-'+GAME_NUMBER+'/runtime-stdlib/',location.href).href;
    worker.postMessage({type:'start',game:GAME_NUMBER,base:globalThis.VASCOLET_EMBEDDED?new URL(globalThis.VASCOLET_RUNTIME_BASE||'./runtime/',location.href).href:base,control:wire.control,input:wire.input,audio:wire.audio,pixels:wire.pixels,frame:wire.frame,save:saved});
    if(!globalThis.VASCOLET_EMBEDDED)watchdog=setTimeout(()=>finish('Prueba detenida tras 20 minutos por seguridad.'),20*60*1000);
  }catch(error){log(error.stack||error);finish(String(error),true);}
};
stop.onclick=async()=>{try{if(worker)await requestSave();finish('Prueba detenida.');}catch(error){say(String(error));}};
document.getElementById('retry').onclick=()=>start.click();
document.getElementById('menu').onclick=()=>{const panel=document.getElementById('panel');panel.hidden=!panel.hidden;document.getElementById('menu').setAttribute('aria-expanded',String(!panel.hidden));};
document.getElementById('export').onclick=async()=>{try{const save=worker?await requestSave():await VascoletSaveStore.load();if(!save)throw Error('No hay guardado para exportar.');download('vascolet'+GAME_NUMBER+'-respaldo.json',JSON.stringify(save));say('Respaldo exportado.');}catch(error){say(String(error));}};
document.getElementById('import').onchange=async event=>{try{if(worker)throw Error('Detén la prueba antes de importar');const file=event.target.files[0];if(!file)return;if(file.size>12*1024*1024)throw Error('Respaldo demasiado grande');const save=JSON.parse(await file.text());if(save.format!=='vascolet-save-v1'||save.game!==GAME_NUMBER||save.engine!=='xo'||!Array.isArray(save.files)||save.files.length>512)throw Error('Formato de respaldo no compatible');importedSave=save;say('Respaldo preparado para el próximo inicio.');}catch(error){say(String(error));}};
document.getElementById('feedback').onclick=()=>download('vascolet'+GAME_NUMBER+'-prueba-diagnostico.json',JSON.stringify({version:'worker-lab-1',game:GAME_NUMBER,stats:lastStats,audio:backend.audioSnapshot(),report:lastReport,logs:lines,browser:navigator.userAgent},null,2));
try{const previous=JSON.parse(sessionStorage.getItem('vascolet-last-stop-'+GAME_NUMBER));if(previous)say('Última prueba: '+previous.text);}catch{}
window.vascoletAOT={sync:requestSave,export:async()=>{const save=worker?await requestSave():await VascoletSaveStore.load();return save?.files||[];},import:async save=>{VascoletSaveStore.validate(save);await VascoletSaveStore.put(save);importedSave=save;},stats:()=>({stats:lastStats,audio:backend.audioSnapshot(),report:lastReport,host:{rawSounds:rawSounds.size,rawSoundBytes:[...rawSounds.values()].reduce((sum,raw)=>sum+raw.byteLength,0),pendingAudio:pendingAudio.size,saveWaiters:saveWaiters.size,lines:lines.length,saves:saveCount,frames:frameSeen,bitmapsClosed:bitmapClosed,imageRpc:imageRpcClosed,mem:performance.memory?{used:performance.memory.usedJSHeapSize,total:performance.memory.totalJSHeapSize,limit:performance.memory.jsHeapSizeLimit}:null}}),key:(key,down)=>{const codes={ArrowUp:273,ArrowDown:274,ArrowRight:275,ArrowLeft:276,Enter:13,Escape:27,' ':32};const code=codes[key]??(key.length===1?key.toLowerCase().charCodeAt(0):undefined);if(code!==undefined)backend.key(code,down);},mute:muted=>backend.setMuted(muted)};
setInterval(()=>{if(worker&&document.visibilityState==='visible')requestSave().catch(error=>dispatchEvent(new CustomEvent('vascolet-save-error',{detail:String(error)})));},5000);
document.addEventListener('visibilitychange',()=>{if(document.hidden&&worker)requestSave().catch(log);});
addEventListener('pagehide',()=>{if(worker)finish('Prueba cerrada.');});
