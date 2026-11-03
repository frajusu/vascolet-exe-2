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
setInterval(()=>{if(!embedReady)return;const now=performance.now(),snapshot=vascoletAOT.stats(),fps=(snapshot.stats.frames-embedFrames)*1000/(now-embedTime);embedFrames=snapshot.stats.frames;embedTime=now;embedSend('metrics',{text:`Motor: XO original recompilado · WebAssembly\nVersión: ${globalThis.VASCOLET_BUILD_ID||'sin versión'}\nPresentaciones / segundo: ${fps.toFixed(1)}\nMemoria Wasm: ${(snapshot.stats.wasmBytes/1048576).toFixed(1)} MiB\nAudio: ${snapshot.audio.state}\nGuardado automático: IndexedDB`});},1000);
(async()=>{try{vascoletAOT.mute(embedParams.get('sound')==='false');if(parent.vascoletBackup)await vascoletAOT.import({format:'vascolet-save-v1',game:2,engine:'xo',files:parent.vascoletBackup});start.click();}catch(error){embedSend('error',{text:error.message});}})();
