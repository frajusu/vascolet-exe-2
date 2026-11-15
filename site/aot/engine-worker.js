/* Original synchronous AOT code runs off the UI thread. No Asyncify pass. */
importScripts('./protocol.js');
const P=VascoletWire;
let control,input,audio,sharedPixels,framePixels,numberView,mod,rpcID=0,frameID=0;
let gameNumber=2,gameHome='/home/vascolet2';
const decoder=new TextDecoder();
const stats={frames:0,images:0,imageBytesCopied:0,events:0,wasmBytes:0,lastTitle:''};
/* RGB565→RGBA packed LUT: one lookup + one 32-bit store per pixel instead of
 * four byte stores and twelve bit ops (was ~10 ms/frame at 1200x900 on phone). */
const rgb565LUT=(()=>{const t=new Uint32Array(65536);for(let v=0;v<65536;v++){const r=(v>>8&248)|(v>>13&7),g=(v>>3&252)|(v>>9&3),b=(v<<3&248)|(v>>2&7);t[v]=(255<<24)|(b<<16)|(g<<8)|r;}return t;})();
function stopped(){return Atomics.load(control,P.STOP)!==0;}
function waitFor(index,value,seconds=20){const until=performance.now()+seconds*1000;while(Atomics.load(control,index)!==value){if(stopped())throw Error('Prueba detenida');if(performance.now()>until)throw Error('El navegador no respondió dentro del límite');const old=Atomics.load(control,index);if(old!==value)Atomics.wait(control,index,old,250);}}
function waitForAtLeast(index,value,seconds=20){const until=performance.now()+seconds*1000;while(Atomics.load(control,index)<value){if(stopped())throw Error('Prueba detenida');if(performance.now()>until)throw Error('El navegador no respondió dentro del límite');Atomics.wait(control,index,Atomics.load(control,index),250);}}
function rpc(method,args=[],raw){
  const id=++rpcID;postMessage({type:'rpc',id,method,args,raw},raw?[raw.buffer]:[]);waitFor(P.RPC_DONE,id);
  if(Atomics.load(control,P.RPC_ERROR)){const n=Atomics.load(control,P.ERROR_LENGTH);throw Error(decoder.decode(sharedPixels.subarray(0,Math.min(n,P.ERROR_BYTES))));}
  return numberView.getFloat64(P.NUMBER_OFFSET,true);
}
function normalize(path){const parts=[];for(const p of path.split('/')){if(!p||p==='.')continue;if(p==='..')parts.pop();else parts.push(p);}if(parts[0]==='game')parts.shift();return parts.join('/');}
function saveFiles(){
  const files=[];let bytes=0;
    function walk(dir){for(const name of mod.FS.readdir(dir)){if(name==='.'||name==='..')continue;const path=dir+'/'+name,info=mod.FS.stat(path);if(mod.FS.isDir(info.mode))walk(path);else{if(files.length>=512||bytes+info.size>8*1024*1024)throw Error('El guardado supera el límite de exportación');const raw=mod.FS.readFile(path);bytes+=raw.length;files.push({path:path.slice('/home/'.length),raw});}}}
    walk(gameHome);return files;
}
function saveIfRequested(){
  const request=Atomics.load(control,P.SAVE_REQUEST);if(request===Atomics.load(control,P.SAVE_ACK))return;
  try{const files=saveFiles();postMessage({type:'save',request,files},files.map(f=>f.raw.buffer));}
  catch(error){postMessage({type:'save-error',request,message:String(error)});}
  Atomics.store(control,P.SAVE_ACK,request);
}
function installSaveHooks(){
  const home=path=>typeof path==='string'&&path.startsWith(gameHome+'/');
  /* A storage failure (quota, private mode, blocked DB) must never crash the
   * guest mid-fclose: warn and keep playing. */
  const persist=()=>{try{rpc('persist',[saveFiles()]);}catch(error){postMessage({type:'log',line:'Aviso de guardado: '+error});}};
  const close=mod.FS.close.bind(mod.FS);mod.FS.close=stream=>{const changed=home(stream.path)&&(stream.flags&3)!==0;const result=close(stream);if(changed)persist();return result;};
  const rename=mod.FS.rename.bind(mod.FS);mod.FS.rename=(from,to)=>{const result=rename(from,to);if(home(from)||home(to))persist();return result;};
  const unlink=mod.FS.unlink.bind(mod.FS);mod.FS.unlink=path=>{const result=unlink(path);if(home(path))persist();return result;};
}
function eventQueue(){
  const held=new Set(),pending=[];let buttons=0,peek=null,readLocal=0; /* worker-local mirror of INPUT_READ: the worker is its only writer */
  function recover(){if(!Atomics.load(control,P.INPUT_OVERFLOW))return;if(!Atomics.exchange(control,P.INPUT_OVERFLOW,0))return;const write=Atomics.load(control,P.INPUT_WRITE);Atomics.store(control,P.INPUT_READ,write);readLocal=write;for(const code of held){const e=new Uint8Array(20);e[0]=3;new DataView(e.buffer).setInt32(8,code,true);pending.push(e);}for(let button=1;button<=5;button++)if(buttons&(1<<(button-1))){const e=new Uint8Array(20);e[0]=6;e[2]=button;pending.push(e);}}
  function next(){if(peek)return peek.event;if(!pending.length&&Atomics.load(control,P.INPUT_WRITE)===readLocal)return undefined;recover();if(pending.length){peek={event:pending[0],local:true};return peek.event;}const read=Atomics.load(control,P.INPUT_READ),write=Atomics.load(control,P.INPUT_WRITE);if(read===write)return undefined;const offset=(read%P.QUEUE)*20;peek={event:input.slice(offset,offset+20),read,local:false};return peek.event;}
  return {get 0(){return next();},shift(){const event=next();if(!event)return undefined;if(peek.local)pending.shift();else{const nr=peek.read+1;Atomics.store(control,P.INPUT_READ,nr);readLocal=nr;}peek=null;const code=new DataView(event.buffer).getInt32(8,true);if(event[0]===2)held.add(code);else if(event[0]===3)held.delete(code);else if(event[0]===5)buttons|=1<<(event[2]-1);else if(event[0]===6)buttons&=~(1<<(event[2]-1));stats.events++;return event;}};
}
function finishedQueue(){return {get length(){return Atomics.load(control,P.AUDIO_WRITE)-Atomics.load(control,P.AUDIO_READ);},set length(value){if(value!==0)throw Error('Invalid audio queue reset');Atomics.store(control,P.AUDIO_READ,Atomics.load(control,P.AUDIO_WRITE));},shift(){const read=Atomics.load(control,P.AUDIO_READ);if(read===Atomics.load(control,P.AUDIO_WRITE))return undefined;const value=audio[read%P.QUEUE];Atomics.store(control,P.AUDIO_READ,read+1);return value;}};}
function renderer(){
  const canvas=new OffscreenCanvas(512,384),context=canvas.getContext('2d',{alpha:false}),source=new OffscreenCanvas(1,1),ctx=source.getContext('2d',{alpha:false});let image;
  /* postMessage structured-clones synchronously, so one reusable frame message
   * (with the live stats object) skips a fresh object+copy per frame. */
  const frameMsg={type:'frame',stats};
  return {resize(w,h){canvas.width=w;canvas.height=h;postMessage({type:'resize',w,h});},present(heap,ptr,w,h,pitch,kind,dw,dh){
    if(stopped())throw Error('Prueba detenida');
    const t0=performance.now();
    saveIfRequested();
    if(kind===4){/* wasm pre-converted rgb565->RGBA: zero-copy view, no JS pixel loop */
      const bytes=w*h*4;
      if(framePixels&&dw===w&&dh===h&&canvas.width===w&&canvas.height===h&&bytes*2<=framePixels.length){
        /* Identity present: skip the OffscreenCanvas round trip entirely
         * (putImageData + drawImage + getImageData readback + copy cost ~4 full-frame
         * passes on the phone). One memcpy wasm->SAB; the main thread draws straight
         * from shared memory.
         * Double-buffered: slot (id&1) is only overwritten after main acked frame
         * id-1 (previous user of this slot was id-2, so ack id-1 suffices). Guest
         * work of frame id overlaps main's putImageData of id-1, but the 4 MiB
         * memcpy never runs concurrently with it: concurrent full-frame copies
         * saturate phone memory bandwidth (cost us fps). */
        const id=++frameID,stride=framePixels.length>>1,off=(id&1)*stride;
        const wait0=performance.now();
        if(id>1)waitForAtLeast(P.FRAME_ACK,id-1,20);
        stats.ackMs=(stats.ackMs||0)+(performance.now()-wait0);
        framePixels.set(heap.subarray(ptr,ptr+bytes),off);
        stats.frames++;stats.wasmBytes=heap.buffer.byteLength;stats.workerHeapBytes=performance.memory?performance.memory.usedJSHeapSize:-1;
        frameMsg.id=id;frameMsg.off=off;frameMsg.slot=id&1;frameMsg.w=w;frameMsg.h=h;postMessage(frameMsg);
        stats.presentMs=(stats.presentMs||0)+(performance.now()-t0);
        mod._renew_entry_budget();
        return;
      }
      if(source.width!==w||source.height!==h){source.width=w;source.height=h;}
      ctx.putImageData(new ImageData(new Uint8ClampedArray(heap.buffer,ptr,bytes),w),0,0);
    }else{
    if(!image||source.width!==w||source.height!==h){source.width=w;source.height=h;image=ctx.createImageData(w,h);}const out=image.data;
    if(kind===1){for(let y=0;y<h;y++){let row=ptr+y*pitch,at=y*w*4;for(let x=0;x<w;x+=2){const u=heap[row+1]-128,v=heap[row+3]-128,c0=(heap[row]-16)*298,c1=(heap[row+2]-16)*298;out[at]=(c0+409*v+128)>>8;out[at+1]=(c0-100*u-208*v+128)>>8;out[at+2]=(c0+516*u+128)>>8;out[at+3]=255;out[at+4]=(c1+409*v+128)>>8;out[at+5]=(c1-100*u-208*v+128)>>8;out[at+6]=(c1+516*u+128)>>8;out[at+7]=255;row+=4;at+=8;}}}
    else if(kind===2){const out32=new Uint32Array(out.buffer,out.byteOffset,w*h);if(((heap.byteOffset+ptr)&1)===0&&(pitch&1)===0){const src=new Uint16Array(heap.buffer),base=(heap.byteOffset+ptr)>>1,stride=pitch>>1;for(let y=0;y<h;y++){let i=base+y*stride,at=y*w;for(let x=0;x<w;x++)out32[at++]=rgb565LUT[src[i++]];}}else{for(let y=0;y<h;y++){let row=ptr+y*pitch,at=y*w;for(let x=0;x<w;x++){out32[at++]=rgb565LUT[heap[row]|heap[row+1]<<8];row+=2;}}}}
    else if(kind===3){for(let y=0;y<h;y++){let row=ptr+y*pitch,at=y*w*4;for(let x=0;x<w;x++){out[at]=heap[row];out[at+1]=heap[row+1];out[at+2]=heap[row+2];out[at+3]=255;row+=4;at+=4;}}}
    else throw Error('Formato de pantalla no soportado');
    ctx.putImageData(image,0,0);
    }
    context.drawImage(source,0,0,dw,dh);stats.frames++;stats.wasmBytes=heap.buffer.byteLength;stats.workerHeapBytes=performance.memory?performance.memory.usedJSHeapSize:-1;
    const id=++frameID;
    if(framePixels){
      /* Raw RGBA via shared memory: a per-frame ImageBitmap leaked one GPU texture per frame in Opera's GPU process. */
      const width=canvas.width,height=canvas.height,bytes=width*height*4;
      if(bytes>framePixels.length)throw Error('El frame supera el límite de memoria compartida');
      framePixels.set(context.getImageData(0,0,width,height).data);
      postMessage({type:'frame',id,w:width,h:height,stats:{...stats}});
    }else{
      const bitmap=canvas.transferToImageBitmap();postMessage({type:'frame',id,bitmap,stats:{...stats}},[bitmap]);
    }
    waitFor(P.FRAME_ACK,id,20);mod._renew_entry_budget();
  }};
}
function host(){const video=renderer();let imageName='',image=null;
  return {events:eventQueue(),finished:finishedQueue(),get mouse(){const xy=Atomics.load(control,P.MOUSE_XY);return {x:xy&65535,y:xy>>>16,buttons:Atomics.load(control,P.MOUSE_BUTTONS)};},
    init:flags=>rpc('init',[flags]),resize:video.resize,caption(text){stats.lastTitle=text;postMessage({type:'caption',text});},cursor:mode=>rpc('cursor',[mode]),
    image(path){path=normalize(path);if(path!==imageName){const raw=mod.FS.readFile('/game/'+path);rpc('image',[path],raw);const w=Atomics.load(control,P.IMAGE_W),h=Atomics.load(control,P.IMAGE_H),length=Atomics.load(control,P.IMAGE_LENGTH);if(w<1||h<1||length!==w*h*4||length>P.PIXEL_BYTES)throw Error('Imagen fuera del límite');imageName=path;image={w,h,pixels:sharedPixels.subarray(0,length)};stats.images++;stats.imageBytesCopied+=length;}return image;},
    present:video.present,shouldStop:stopped,
    openAudio:(...args)=>rpc('openAudio',args),allocateChannels:n=>rpc('allocateChannels',[n]),soundDuration:path=>rpc('soundDuration',[normalize(path)]),play:(...args)=>rpc('play',args),halt:n=>rpc('halt',[n]),freeSound:n=>rpc('freeSound',[n]),pause:(...args)=>rpc('pause',args),volume:(...args)=>rpc('volume',args),playing:(...args)=>rpc('playing',args),playMusic:(...args)=>rpc('playMusic',args),haltMusic:()=>rpc('haltMusic',[]),pauseMusic:v=>rpc('pauseMusic',[v]),musicVolume:v=>rpc('musicVolume',[v]),musicPlaying:()=>rpc('musicPlaying',[]),musicFinished:()=>rpc('musicFinished',[])
  };
}
onmessage=async({data})=>{
  if(data.type!=='start')return;control=new Int32Array(data.control);input=new Uint8Array(data.input);audio=new Int32Array(data.audio);sharedPixels=new Uint8Array(data.pixels);framePixels=data.frame?new Uint8Array(data.frame):null;numberView=new DataView(data.control);
  gameNumber=Number(data.game||2);if(![1,2,3,4].includes(gameNumber))throw Error('Juego no compatible');gameHome='/home/vascolet'+gameNumber;
  try{
    const base=data.base;stats.runtimeBase=base;importScripts(base+'worker-runtime.js');postMessage({type:'progress',text:'Cargando el módulo y los recursos originales…'});
    mod=await createVascoletAOTWorker({locateFile:file=>base+file,aotHost:host(),print:line=>postMessage({type:'log',line}),printErr:line=>postMessage({type:'log',line})});
    for(const name of ['data','instance','tmp'])mod.FS.mkdirTree(gameHome+'/'+name);mod.FS.chdir('/game');
    if(data.save){let total=0;for(const file of data.save.files){const parts=file.path.split('/');if(parts[0]!=='vascolet'+gameNumber||parts.length<2||parts.some(p=>!p||p==='.'||p==='..'||p.includes('\\')||p.includes('\0')))throw Error('Ruta de guardado no válida');const text=atob(file.data);total+=text.length;if(total>8*1024*1024)throw Error('Guardado demasiado grande');const raw=Uint8Array.from(text,c=>c.charCodeAt(0));const path='/home/'+file.path;mod.FS.mkdirTree(path.slice(0,path.lastIndexOf('/')));mod.FS.writeFile(path,raw);}}
    function walk(dir){for(const name of mod.FS.readdir(dir)){if(name==='.'||name==='..')continue;const path=dir+'/'+name;if(mod.FS.isDir(mod.FS.stat(path).mode))walk(path);else if(/\.(ogg|wav|mp3)$/i.test(path)){const raw=mod.FS.readFile(path);postMessage({type:'sound-file',path:normalize(path),raw},[raw.buffer]);}}}walk('/game');
    installSaveHooks();mod._set_entry_budget(500000000);postMessage({type:'progress',text:'Arrancando el juego original. La carga puede tardar…'});
    const status=mod._run_startup();postMessage({type:'done',status,stats});
  }catch(error){postMessage({type:'failure',message:String(error),stack:error.stack,stats});}
};
