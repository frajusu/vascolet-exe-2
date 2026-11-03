/* Native SDL 1.2 frame, input and audio transport. MIT license. */
const canvas=document.getElementById('display');
const context=canvas.getContext('2d',{alpha:false});
window.bridgeStats={frames:0,frameTimes:[],audioStarts:0,audioErrors:[],nativeLog:''};
let stream=new Uint8Array(131072),streamRead=0,streamEnd=0;
let rgba;
const colors=new Map();
let audio;
let meter;
let master,readySent=false,inputWidth=0,inputHeight=0,yuvCanvas,yuvContext,yuvImage;
const sounds=new Map(),channels=new Map();
const soundCache=new Map();
const SOUND_CACHE_LIMIT=32*1024*1024;
let soundBytes=0;
function trimSounds(){
  const active=new Set([...channels.values()].map(record=>record.buffer));
  for(const [url,entry] of soundCache){if(soundBytes<=SOUND_CACHE_LIMIT)break;if(!entry.buffer||active.has(entry.buffer))continue;soundBytes-=entry.bytes;soundCache.delete(url);}
  bridgeStats.audioDecodedBytes=soundBytes;
}
function loadSound(url){
  const cached=soundCache.get(url);if(cached){soundCache.delete(url);soundCache.set(url,cached);bridgeStats.audioCacheHits=(bridgeStats.audioCacheHits||0)+1;return cached.promise;}
  const entry={buffer:null,bytes:0,promise:null};soundCache.set(url,entry);
  entry.promise=fetch(url).then(response=>{if(!response.ok)throw Error(`${url}: ${response.status}`);return response.arrayBuffer();}).then(bytes=>audioContext().decodeAudioData(bytes)).then(buffer=>{entry.buffer=buffer;entry.bytes=buffer.length*buffer.numberOfChannels*4;soundBytes+=entry.bytes;bridgeStats.audioDecodedBytes=soundBytes;return buffer;}).catch(error=>{soundCache.delete(url);bridgeStats.audioErrors.push(error.message);return null;});
  return entry.promise;
}
const textures=new Map();
const nativeTextures=new Map();
function nativeTexture(record,mode,color){
  const replace=color&&mode==='replace',key=replace?'replace:'+color.slice(0,3).join(','):mode;
  if(record.variants.has(key))return record.variants.get(key);
  const image=new ImageData(record.w,record.h),raw=record.raw,alpha=record.alpha;
  for(let n=0;n<record.w*record.h;n++){
    const pixel=raw.getUint32(n*4,true),y=((pixel&255)-16)*298,u=(pixel>>>8&255)-128,v=(pixel>>>24)-128,at=n*4;
    image.data[at]=replace?color[0]*255:(y+409*v+128)>>8;
    image.data[at+1]=replace?color[1]*255:(y-100*u-208*v+128)>>8;
    image.data[at+2]=replace?color[2]*255:(y+516*u+128)>>8;
    const actual=replace?record.mode:mode;
    image.data[at+3]=actual===0&&alpha?Math.min(255,alpha.getUint16(n*2,true)*255/256):actual===2&&pixel===record.key?0:255;
  }
  const texture=document.createElement('canvas');texture.width=record.w;texture.height=record.h;texture.getContext('2d').putImageData(image,0,0);if(record.variants.size>=6)record.variants.delete(record.variants.keys().next().value);record.variants.set(key,texture);return texture;
}
function nativeBlend(blend){context.globalCompositeOperation=blend===0||blend===1?'lighter':'source-over';}
function frame(pixels=false){bridgeStats.frames++;bridgeStats.frameTimes.push(performance.now());if(bridgeStats.frameTimes.length>6000)bridgeStats.frameTimes.shift();if(!readySent&&(textures.size>0||pixels)){readySent=true;window.onFirstFrame?.();}}
function size(w,h){if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;context.imageSmoothingEnabled=false;}}
function audioContext(){if(!audio){audio=new AudioContext();meter=audio.createAnalyser();master=audio.createGain();master.gain.value=new URLSearchParams(location.search).get('sound')==='false'?0:1;meter.connect(master).connect(audio.destination);}return audio;}
window.audioSnapshot=()=>{const pcm=new Float32Array(1024);meter?.getFloatTimeDomainData(pcm);return {state:audio?.state,starts:bridgeStats.audioStarts,rms:Math.sqrt(pcm.reduce((sum,x)=>sum+x*x,0)/pcm.length),errors:bridgeStats.audioErrors};};
const generations=new Map();
function ended(channel){if(window.nativeInput)for(const byte of new TextEncoder().encode(`A:${channel<0?65535:channel}:0\n`))window.nativeInput(byte);}
function startSound(record){const a=audioContext(),node=a.createBufferSource();record.node=node;node.buffer=record.buffer;node.loop=record.loops<0;record.gain?.disconnect();record.gain=a.createGain();record.gain.gain.value=record.volume/128;node.connect(record.gain).connect(meter);record.started=a.currentTime;node.onended=()=>{node.disconnect();if(record.paused||record.node!==node||channels.get(record.channel)!==record)return;if(record.loops>0){record.loops--;record.offset=0;startSound(record);}else{record.gain.disconnect();channels.delete(record.channel);ended(record.channel);trimSounds();}};node.start(0,record.offset||0);bridgeStats.audioStarts++;}
function stopSound(channel){generations.set(channel,(generations.get(channel)||0)+1);const record=channels.get(channel);channels.delete(channel);if(record?.node)try{record.node.stop();record.node.disconnect();record.gain.disconnect();}catch{}}
function packet(kind,data) {
  const view=new DataView(data.buffer,data.byteOffset,data.byteLength);
  if(kind===1) {
    const w=view.getUint32(0,true),h=view.getUint32(4,true),pitch=view.getUint32(8,true),bpp=view.getUint32(12,true);
    if(w<1||h<1||w>4096||h>4096)return;
    if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;rgba=context.createImageData(w,h);}
    if(!rgba)rgba=context.createImageData(w,h);
    const out=new Uint32Array(rgba.data.buffer),bytes=data.subarray(32);
    if(bpp===2){let table=colors.get('565');if(!table){table=new Uint32Array(65536);for(let n=0;n<65536;n++)table[n]=0xff000000|((n>>11)*255/31)|(((n>>5&63)*255/63)<<8)|(((n&31)*255/31)<<16);colors.set('565',table);}const raw=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);for(let y=0;y<h;y++)for(let x=0;x<w;x++)out[y*w+x]=table[raw.getUint16(y*pitch+x*2,true)];}
    else {for(let y=0;y<h;y++)for(let x=0;x<w;x++){const at=y*pitch+x*bpp;out[y*w+x]=0xff000000|bytes[at+2]|bytes[at+1]<<8|bytes[at]<<16;}}
    context.putImageData(rgba,0,0);bridgeStats.frames++;bridgeStats.frameTimes.push(performance.now());
    if(bridgeStats.frameTimes.length>6000)bridgeStats.frameTimes.shift();
    if(bridgeStats.frames===1)window.onFirstFrame?.();
  }
  if(kind===2){size(view.getUint32(0,true),view.getUint32(4,true));frame();}
  if(kind===5){
    const id=view.getUint32(0,true),w=view.getUint32(4,true),h=view.getUint32(8,true),pitch=view.getUint32(12,true),bpp=view.getUint32(16,true),flags=view.getUint32(36,true),key=view.getUint32(40,true);
    const image=new ImageData(w,h),raw=new DataView(data.buffer,data.byteOffset+44,data.byteLength-44);
    const masks=[20,24,28,32].map(offset=>view.getUint32(offset,true));
    const shifts=masks.map(mask=>mask?31-Math.clz32(mask&-mask):0),max=masks.map((mask,n)=>mask>>>shifts[n]);
    const pixelAlpha=!!((flags&0x10000)&&max[3]),rgbMask=(masks[0]|masks[1]|masks[2])>>>0;
    for(let y=0;y<h;y++)for(let x=0;x<w;x++){const at=y*pitch+x*bpp,pixel=bpp===2?raw.getUint16(at,true):bpp===4?raw.getUint32(at,true):raw.getUint8(at)|raw.getUint8(at+1)<<8|raw.getUint8(at+2)<<16,base=(y*w+x)*4;for(let c=0;c<3;c++)image.data[base+c]=max[c]?((pixel&masks[c])>>>shifts[c])*255/max[c]:0;image.data[base+3]=pixelAlpha?((pixel&masks[3])>>>shifts[3])*255/max[3]:(flags&0x1000)&&(pixel&rgbMask)===(key&rgbMask)?0:255;}
    const texture=document.createElement('canvas');texture.width=w;texture.height=h;texture.getContext('2d').putImageData(image,0,0);textures.set(id,texture);
  }
  if(kind===6){const texture=textures.get(view.getUint32(0,true));if(texture){const w=view.getInt32(12,true),h=view.getInt32(16,true);context.globalAlpha=view.getInt32(28,true)/255;context.drawImage(texture,view.getInt32(4,true),view.getInt32(8,true),w,h,view.getInt32(20,true),view.getInt32(24,true),w,h);context.globalAlpha=1;}}
  if(kind===7){const values=Array.from({length:8},(_,n)=>view.getUint32(n*4,true));size(values[6],values[7]);const color=values[4];context.fillStyle=values[5]===16?`rgb(${(color>>11)*255/31},${(color>>5&63)*255/63},${(color&31)*255/31})`:`rgb(${color>>16&255},${color>>8&255},${color&255})`;context.fillRect(values[0],values[1],values[2],values[3]);}
  if(kind===8)textures.delete(view.getUint32(0,true));
  if(kind===18){inputWidth=view.getUint32(0,true);inputHeight=view.getUint32(4,true);}
  if(kind===9){
    const w=view.getUint32(0,true),h=view.getUint32(4,true),format=view.getUint32(8,true),planes=view.getUint32(12,true),pitch=view.getUint32(16,true),up=view.getUint32(20,true),vp=view.getUint32(24,true),dx=view.getInt32(28,true),dy=view.getInt32(32,true),dw=view.getUint32(36,true),dh=view.getUint32(40,true),raw=data.subarray(44);
    if(!yuvCanvas||yuvCanvas.width!==w||yuvCanvas.height!==h){yuvCanvas=document.createElement('canvas');yuvCanvas.width=w;yuvCanvas.height=h;yuvContext=yuvCanvas.getContext('2d',{alpha:false});yuvImage=yuvContext.createImageData(w,h);}
    const out=yuvImage.data,uvStart=pitch*h,planeSize=up*Math.ceil(h/2);
    for(let y=0;y<h;y++)for(let x=0;x<w;x++){
      let yy,u,v;
      if(planes===3){yy=raw[y*pitch+x];const at=(y>>1)*up+(x>>1);if(format===0x32315659){v=raw[uvStart+at];u=raw[uvStart+planeSize+at];}else{u=raw[uvStart+at];v=raw[uvStart+planeSize+at];}}
      else{const at=y*pitch+(x>>1)*4;if(format===0x59565955){u=raw[at];yy=raw[at+(x&1?3:1)];v=raw[at+2];}else{yy=raw[at+(x&1?2:0)];u=raw[at+(format===0x55595659?3:1)];v=raw[at+(format===0x55595659?1:3)];}}
      const c=(yy-16)*298,d=u-128,e=v-128,at=(y*w+x)*4;out[at]=(c+409*e+128)>>8;out[at+1]=(c-100*d-208*e+128)>>8;out[at+2]=(c+516*d+128)>>8;out[at+3]=255;
    }
    yuvContext.putImageData(yuvImage,0,0);size(inputWidth||dw||w,inputHeight||dh||h);context.drawImage(yuvCanvas,dx,dy,dw||canvas.width,dh||canvas.height);frame(true);
  }
  if(kind===10){const id=view.getUint32(0,true),path=new TextDecoder().decode(data.subarray(4)).replace(/\0.*$/s,'');const root=window.assetBase||new URL('files/',location.href);sounds.set(id,new URL('game/'+path.replace(/^\/game\//,'').replace(/\/{2,}/g,'/'),root).href);}
  if(kind===11){const id=view.getUint32(0,true),channel=view.getInt32(4,true),loops=view.getInt32(8,true),volume=view.getInt32(12,true);stopSound(channel);const generation=generations.get(channel),url=sounds.get(id);(url?loadSound(url):Promise.resolve(null)).then(buffer=>{if(generation!==generations.get(channel))return;if(!buffer){ended(channel);return;}const record={channel,buffer,loops,volume,offset:0,paused:false};channels.set(channel,record);startSound(record);trimSounds();});}
  if(kind===12){const channel=view.getInt32(0,true);if(channel===-1){for(const key of [...channels.keys()])if(key>=0)stopSound(key);}else stopSound(channel);}
  if(kind===13){for(const key of [...channels.keys()])stopSound(key);}
  if(kind===14){const channel=view.getInt32(0,true),pause=!!view.getInt32(4,true);for(const record of channels.values()){if(record.channel!==channel&&!(channel===-1&&record.channel>=0))continue;if(pause&&!record.paused){record.paused=true;record.offset=(record.offset+audio.currentTime-record.started)%record.buffer.duration;try{record.node.stop();}catch{}}else if(!pause&&record.paused){record.paused=false;startSound(record);}}}
  if(kind===15){const channel=view.getInt32(0,true),volume=view.getInt32(4,true);for(const record of channels.values())if(record.channel===channel||channel===-1&&record.channel>=0){record.volume=volume;record.gain.gain.value=volume/128;}}
  if(kind===21||kind===22)window.onSavePacket?.(kind,data);
  if(kind===30){size(view.getUint32(0,true),view.getUint32(4,true));context.globalCompositeOperation='source-over';context.globalAlpha=1;context.fillStyle=`rgb(${view.getFloat32(8,true)*255},${view.getFloat32(12,true)*255},${view.getFloat32(16,true)*255})`;context.fillRect(0,0,canvas.width,canvas.height);bridgeStats.renderer='browser';}
  if(kind===31){const id=view.getUint32(0,true),w=view.getUint32(4,true),h=view.getUint32(8,true),copy=data.slice(24),pixels=w*h;nativeTextures.set(id,{w,h,mode:view.getUint32(12,true),key:view.getUint32(16,true),raw:new DataView(copy.buffer,0,pixels*4),alpha:view.getUint32(20,true)?new DataView(copy.buffer,pixels*4,pixels*2):null,variants:new Map()});}
  if(kind===32){
    const id=view.getUint32(0,true),mode=view.getUint32(4,true),blend=view.getUint32(8,true),flip=view.getUint32(12,true),record=nativeTextures.get(id);if(!record)return;
    const values=Array.from({length:11},(_,n)=>view.getFloat32(16+n*4,true)),[sx,sy,w,h,dx,dy,alpha]=values;
    const texture=nativeTexture(record,blend===2?'replace':mode,blend===2?values.slice(7):null);
    nativeBlend(blend);context.globalAlpha=Math.max(0,Math.min(1,alpha));
    if(flip){context.save();context.translate(Math.trunc(dx)+Math.trunc(w),Math.trunc(dy));context.scale(-1,1);context.drawImage(texture,Math.trunc(sx),Math.trunc(sy),Math.trunc(w),Math.trunc(h),0,0,Math.trunc(w),Math.trunc(h));context.restore();}
    else context.drawImage(texture,Math.trunc(sx),Math.trunc(sy),Math.trunc(w),Math.trunc(h),Math.trunc(dx),Math.trunc(dy),Math.trunc(w),Math.trunc(h));
    context.globalAlpha=1;context.globalCompositeOperation='source-over';bridgeStats.nativeDraws=(bridgeStats.nativeDraws||0)+1;
  }
  if(kind===33){const v=Array.from({length:9},(_,n)=>view.getFloat32(4+n*4,true));nativeBlend(view.getUint32(0,true));context.strokeStyle=`rgba(${v[5]*255},${v[6]*255},${v[7]*255},${v[8]})`;context.lineWidth=v[4]||1;context.beginPath();context.moveTo(v[0],v[1]);context.lineTo(v[2],v[3]);context.stroke();context.globalCompositeOperation='source-over';}
  if(kind===34)frame(true);
  if(kind===35)nativeTextures.delete(view.getUint32(0,true));
  if(kind===36){size(view.getUint32(0,true),view.getUint32(4,true));bridgeStats.renderer='browser';}
}
window.consumeSDL=bytes=>{
  const pending=streamEnd-streamRead;
  if(streamEnd+bytes.length>stream.length){if(pending+bytes.length<=stream.length){stream.copyWithin(0,streamRead,streamEnd);}else{let capacity=stream.length;while(capacity<pending+bytes.length)capacity*=2;const next=new Uint8Array(capacity);next.set(stream.subarray(streamRead,streamEnd));stream=next;bridgeStats.transportAllocations=(bridgeStats.transportAllocations||0)+1;}streamRead=0;streamEnd=pending;}
  stream.set(bytes,streamEnd);streamEnd+=bytes.length;
  while(streamEnd-streamRead>=12){const v=new DataView(stream.buffer,streamRead,streamEnd-streamRead);if(v.getUint32(0,true)!==0x4c445356){bridgeStats.nativeLog=(bridgeStats.nativeLog+String.fromCharCode(stream[streamRead++])).slice(-8000);continue;}const kind=v.getUint32(4,true),size=v.getUint32(8,true);if(size>20*1024*1024){streamRead+=4;continue;}if(streamEnd-streamRead<12+size)return;packet(kind,stream.subarray(streamRead+12,streamRead+12+size));streamRead+=12+size;}
  if(streamRead===streamEnd)streamRead=streamEnd=0;
};
const state=new Uint8Array(332);
function inputPacket(bytes){if(!window.nativeInput)return;const line=bytes[0]===0xc1?`K:${bytes[1]|bytes[2]<<8}:${bytes[3]}\n`:`M:${bytes[1]|bytes[2]<<8}:${bytes[3]|bytes[4]<<8}:${bytes[5]}\n`;for(const byte of new TextEncoder().encode(line))window.nativeInput(byte);}
window.connectInput=async device=>{};
const keymap={ArrowUp:273,ArrowDown:274,ArrowRight:275,ArrowLeft:276,Escape:27,Enter:13,' ':32,Shift:304,Control:306,Backspace:8,Tab:9};
const physicalKeys=new Set(),virtualKeys=new Set();
function gameKey(key,down,source){const code=keymap[key]??(key.length===1?key.toLowerCase().charCodeAt(0):0);if(!code||code>=323)return false;down?source.add(code):source.delete(code);const active=physicalKeys.has(code)||virtualKeys.has(code);if(state[code]!==Number(active)){bridgeStats.sentKeys=(bridgeStats.sentKeys||0)+1;state[code]=Number(active);inputPacket([0xc1,code&255,code>>8,active?1:0]);}return true;}
window.setGameKey=(key,down)=>{if(down)audioContext().resume().catch(()=>{});return gameKey(key,down,virtualKeys);};
function key(event,down){if(gameKey(event.key,down,physicalKeys))event.preventDefault();audio?.resume();}
window.addEventListener('keydown',e=>key(e,true));window.addEventListener('keyup',e=>key(e,false));
function mouse(event,down){const rect=canvas.getBoundingClientRect(),scale=Math.min(rect.width/canvas.width,rect.height/canvas.height),left=rect.left+(rect.width-canvas.width*scale)/2,top=rect.top+(rect.height-canvas.height*scale)/2,iw=inputWidth||canvas.width,ih=inputHeight||canvas.height,x=Math.max(0,Math.min(iw-1,(event.clientX-left)/scale*iw/canvas.width)),y=Math.max(0,Math.min(ih-1,(event.clientY-top)/scale*ih/canvas.height)),v=new DataView(state.buffer);v.setUint16(324,x,true);v.setUint16(326,y,true);if(down!==undefined)state[328]=down?1:0;inputPacket([0xc2,x&255,x>>8,y&255,y>>8,state[328]]);if(down){audioContext().resume();canvas.focus();}}
let canvasPointer=null,canvasDownAt=0,canvasReleaseTimer;
canvas.style.touchAction='none';
canvas.addEventListener('pointerdown',event=>{if(event.button!==0||canvasPointer!==null)return;event.preventDefault();clearTimeout(canvasReleaseTimer);canvasPointer=event.pointerId;canvasDownAt=performance.now();canvas.setPointerCapture(event.pointerId);mouse(event,true);});
canvas.addEventListener('pointermove',event=>{if(event.pointerType==='mouse'||event.pointerId===canvasPointer)mouse(event);});
window.addEventListener('pointerup',event=>{if(event.pointerId!==canvasPointer)return;event.preventDefault();canvasPointer=null;const wait=event.pointerType==='touch'?Math.max(0,140-(performance.now()-canvasDownAt)):0;canvasReleaseTimer=setTimeout(()=>mouse(event,false),wait);});
function cancelCanvasPointer(event){if(event.pointerId!==canvasPointer)return;canvasPointer=null;clearTimeout(canvasReleaseTimer);mouse(event,false);}
canvas.addEventListener('pointercancel',cancelCanvasPointer);canvas.addEventListener('lostpointercapture',cancelCanvasPointer);
window.addEventListener('blur',()=>{canvasPointer=null;clearTimeout(canvasReleaseTimer);physicalKeys.clear();virtualKeys.clear();for(let code=0;code<323;code++)if(state[code])inputPacket([0xc1,code&255,code>>8,0]);state.fill(0);inputPacket([0xc2,0,0,0,0,0]);});
window.addEventListener('pagehide',()=>{for(const key of [...channels.keys()])stopSound(key);audio?.close().catch(()=>{});});
