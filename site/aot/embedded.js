/* Adapter for the existing published launcher and multitouch controls. */
const embedParams=new URLSearchParams(location.search);
const embedSend=(type,data={})=>parent.postMessage({channel:'vascolet-runtime',run:embedParams.get('run'),type,...data},location.origin);
let embedReady=false,embedFrames=0,embedTime=performance.now();
window.setGameKey=(key,down)=>{vascoletAOT.key(key,down);if(down)void backend.resumeAudio().catch(log);};
addEventListener('vascolet-status',event=>{if(!embedReady)embedSend('progress',{text:event.detail});});
addEventListener('vascolet-log',event=>embedSend('log',{text:event.detail}));
addEventListener('vascolet-ready',()=>{embedReady=true;embedSend('ready');});
addEventListener('vascolet-saved',()=>embedSend('saved'));
addEventListener('vascolet-save-error',event=>embedSend('save-error',{text:event.detail}));
addEventListener('vascolet-failure',event=>embedSend('error',{text:event.detail}));
addEventListener('message',async event=>{
  if(event.origin!==location.origin||event.source!==parent||event.data?.channel!=='vascolet-command')return;
  const {id,action}=event.data;
  try{let data;if(action==='sync')await vascoletAOT.sync();else if(action==='export')data=await vascoletAOT.export();else throw Error('Acción desconocida.');embedSend('reply',{id,data});}
  catch(error){embedSend('reply',{id,error:error.message});}
});
/* memoria total de pestaña: muestreada por el padre (launcher.js) */
setInterval(()=>{if(!embedReady)return;const now=performance.now(),snapshot=vascoletAOT.stats(),fps=(snapshot.stats.frames-embedFrames)*1000/(now-embedTime);embedFrames=snapshot.stats.frames;embedTime=now;const host=snapshot.host||{},mem=host.mem,heap=mem?`\nJS heap: ${(mem.used/1048576).toFixed(1)} MiB · total ${(mem.total/1048576).toFixed(1)} · límite ${(mem.limit/1048576).toFixed(0)}`:'',workerHeap=typeof snapshot.stats.workerHeapBytes==='number'&&snapshot.stats.workerHeapBytes>=0?`\nJS heap worker: ${(snapshot.stats.workerHeapBytes/1048576).toFixed(1)} MiB`:'',bitmaps=`\nFrames: ${host.frames||0} presentados · ImageBitmap por cuadro: 0 (RGBA directo a putImageData) · imágenes RPC ${host.imageRpc||0}`,saves=`\nGuardado: ${host.saves||0} sincronizaciones · ${host.saveWaiters||0} en espera · ${(host.rawSoundBytes||0)/1048576|0} MiB sonidos crudos`;embedSend('metrics',{text:`Motor: XO original recompilado · WebAssembly\nVersión: ${globalThis.VASCOLET_BUILD_ID||'sin versión'}\nPresentaciones / segundo: ${fps.toFixed(1)}\nMemoria Wasm: ${(snapshot.stats.wasmBytes/1048576).toFixed(1)} MiB${heap}${workerHeap}\nAudio: ${snapshot.audio.state} · ${(snapshot.audio.decodedBytes/1048576).toFixed(1)} MiB decodificados · ${snapshot.audio.channels} canales\nImágenes: ${snapshot.stats.images||0} cargas (acumulado) · ${((snapshot.stats.imageBytesCopied||0)/1048576).toFixed(1)} MiB copiados${bitmaps}${saves}\nGuardado automático: IndexedDB`});},1000);
(async()=>{try{vascoletAOT.mute(embedParams.get('sound')==='false');if(parent.vascoletBackup)await vascoletAOT.import({format:'vascolet-save-v1',game:GAME_NUMBER,engine:'xo',files:parent.vascoletBackup});start.click();}catch(error){embedSend('error',{text:error.message});}})();
