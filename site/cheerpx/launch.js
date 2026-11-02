/* CheerpX Linux + original XO ELF, with browser-native SDL devices. MIT. */
const params=new URLSearchParams(location.search);
const send=(type,data={})=>parent.postMessage({channel:'vascolet-runtime',run:params.get('run'),type,...data},location.origin);
const started=performance.now();
let cx,seed,home,gameStore,config,ready=false,exportPending;
let lastFrames=0,lastMetric=performance.now();
const timeout=(promise,ms,message)=>Promise.race([promise,new Promise((_,reject)=>setTimeout(()=>reject(new Error(message)),ms))]);
function progress(text){send('progress',{text});}
async function copyFiles(files,base){
  let next=0,loaded=0;const manifest=[];
  await Promise.all(Array.from({length:6},async()=>{for(;;){const id=next++;if(id>=files.length)break;const path=files[id];const response=await fetch(new URL(path,base));if(!response.ok)throw new Error(`${path}: HTTP ${response.status}`);await seed.writeFile('/f'+id,new Uint8Array(await response.arrayBuffer()));manifest.push('f'+id+'|'+path);loaded++;progress(`Cargando XO… ${loaded} / ${files.length}`);}}));
  await seed.writeFile('/manifest',manifest.join('\n')+'\n');
}
async function restore(files){
  if(files.length>5000)throw new Error('La copia contiene demasiados archivos.');
  let total=0;const staged=files.map((file,id)=>{
    if(typeof file.path!=='string'||typeof file.data!=='string'||!file.path.startsWith(`vascolet${config.number}/`)||file.path.includes('\\')||/[|\r\n]/.test(file.path)||file.path.split('/').some(p=>!p||p==='.'||p==='..'))throw new Error('La copia no corresponde al guardado XO de este juego.');
    const bytes=Uint8Array.from(atob(file.data),c=>c.charCodeAt(0));total+=bytes.length;if(total>32*1024*1024)throw new Error('La copia supera 32 MB.');
    return {name:'restore-'+id,path:'home/'+file.path,bytes};
  });
  for(const file of staged)await seed.writeFile('/'+file.name,file.bytes);
  await seed.writeFile('/restore-manifest',staged.map(file=>file.name+'|'+file.path).join('\n')+'\n');
  const result=await cx.run('/bin/native-init',['--import'],{cwd:'/',uid:0,gid:0});
  if(result.status!==0)throw new Error(`No se pudo importar la copia (${result.status}).`);
}
window.onSavePacket=(kind,bytes)=>{
  if(!exportPending)return;
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  if(kind===21){const length=view.getUint32(0,true),path=new TextDecoder().decode(bytes.subarray(4,4+length)),data=bytes.subarray(4+length);let text='';for(let at=0;at<data.length;at+=32768)text+=String.fromCharCode(...data.subarray(at,at+32768));exportPending.files.push({path,data:btoa(text)});}
  if(kind===22){const pending=exportPending;exportPending=null;const status=view.getInt32(0,true);status?pending.reject(new Error('No se pudo exportar la partida.')):pending.resolve(pending.files);}
};
window.addEventListener('message',async event=>{
  if(event.origin!==location.origin||event.source!==parent||event.data?.channel!=='vascolet-command')return;
  const {id,action}=event.data;
  try{
    if(!cx||!ready)throw new Error('El juego todavía está iniciando.');
    let data;
    if(action==='sync'){send('saved');}
    else if(action==='export'){
      if(exportPending)throw new Error('Ya hay una exportación en curso.');
      const result=new Promise((resolve,reject)=>{exportPending={files:[],resolve,reject};});
      window.nativeInput('E'.charCodeAt(0));window.nativeInput(10);
      data=await timeout(result,15000,'La exportación tardó demasiado.');
    }else throw new Error('Acción desconocida.');
    send('reply',{id,data});
  }catch(error){send('reply',{id,error:error.message});}
});
window.addEventListener('error',event=>send('error',{text:event.message}));
window.addEventListener('unhandledrejection',event=>send('error',{text:String(event.reason?.message||event.reason)}));
window.addEventListener('pagehide',()=>{try{cx?.delete();}catch{}});
setInterval(()=>{
  const now=performance.now(),fps=(bridgeStats.frames-lastFrames)*1000/(now-lastMetric),audio=audioSnapshot();
  send('metrics',{text:`Motor: XO · CheerpX\nPresentaciones / segundo: ${fps.toFixed(1)}\nAudio: ${audio.state||'esperando un clic'}\nTiempo activo: ${Math.round((now-started)/1000)} s`});lastFrames=bridgeStats.frames;lastMetric=now;
},1000);
try{
  progress('Preparando XO…');
  const response=await fetch('config.json');if(!response.ok)throw new Error('No se pudo cargar la configuración XO.');config=await response.json();
  const base=window.assetBase=new URL(config.fsRoot+'/',location.href);
  const root=await CheerpX.WebDevice.create(base.href.replace(/\/$/,''));
  const bin=await CheerpX.WebDevice.create(new URL('bin',base).href),app=await CheerpX.WebDevice.create(new URL('app',base).href);
  home=await CheerpX.IDBDevice.create(`vascolet-exe-${config.number}-xo-home-v1`);
  gameStore=await CheerpX.IDBDevice.create(`vascolet-exe-${config.number}-xo-game-v1`);
  const libraries=await CheerpX.IDBDevice.create(`vascolet-exe-${config.number}-xo-libs-${config.version}`);
  seed=await CheerpX.DataDevice.create();
  const marker=await gameStore.readFileAsBlob('/.bundle-version');
  const warm=marker&&(await marker.text()).trim()===config.version;
  await copyFiles(warm?[]:config.files,base);
  cx=window.cx=await CheerpX.Linux.create({mounts:[{type:'dir',path:'/',dev:root},{type:'devs',path:'/dev'},{type:'proc',path:'/proc'},{type:'dir',path:'/bin',dev:bin},{type:'dir',path:'/app',dev:app},{type:'dir',path:'/seed',dev:seed},{type:'dir',path:'/lib',dev:libraries},{type:'dir',path:'/home',dev:home},{type:'dir',path:'/game',dev:gameStore}]});
  window.nativeInput=cx.setCustomConsole((bytes,terminal)=>{window.nativeVT=terminal;consumeSDL(bytes);},80,24);
  if(config.trace){window.nativeActivity={};for(const name of ['cpuActivity','diskActivity'])cx.registerCallback(name,value=>{window.nativeActivity[name]=value;});}
  const env=['LD_LIBRARY_PATH=/lib','HOME=/home',`SUGAR_ACTIVITY_ROOT=/home/vascolet${config.number}`,'SUGAR_BUNDLE_PATH=/game','SDL_VIDEODRIVER=dummy','SDL_AUDIODRIVER=dummy'];
  if(config.trace)env.push('BROWSER_SDL_TRACE=1');
  if(params.get('renderer')==='software')env.push('BROWSER_SOFTWARE_RENDERER=1');
  const setup=await cx.run('/bin/native-init',['--prepare',config.version],{env,cwd:'/',uid:0,gid:0});
  if(setup.status!==0)throw new Error(`No se pudieron preparar los archivos XO (${setup.status}).`);
  if(parent.vascoletBackup)await restore(parent.vascoletBackup);
  progress('Iniciando XO…');
  window.onFirstFrame=()=>{ready=true;send('ready');send('log',{text:`XO original iniciado en ${((performance.now()-started)/1000).toFixed(1)} s; entorno ${warm?'conservado':'nuevo'}.`});document.getElementById('display').focus();};
  const result=await cx.run(config.entry||'/app/'+config.program,[],{env,cwd:'/game',uid:0,gid:0});
  send('error',{text:result.status?`El juego XO se detuvo (${result.status}).`:'El juego se cerró. Podés reiniciarlo desde el menú.'});
}catch(error){send('error',{text:`XO · CheerpX: ${error.message}`});}
