/* Standalone AOT diagnostic browser backend. No CheerpX/Wine dependency. */
window.createAOTBrowserHost = (canvas, {frameLimit=3}={}) => {
  const context=canvas.getContext('2d',{alpha:false});
  if(!context)throw Error('Canvas 2D unavailable');
  const source=document.createElement('canvas'),sourceContext=source.getContext('2d',{alpha:false});
  const events=[],images=new Map(),sounds=new Map(),channels=new Map(),finished=[],mouse={x:0,y:0,buttons:0},held=new Set();
  const stats={frames:0,events:0,width:0,height:0,images:0,decodedBytes:0,lastTitle:'',initialized:0};
  let image,shown=true,audio,meter,channelLimit=8,muted=false;
  let music=null,musicVol=128,musicDone=false;
  const volumes=new Map();
  function assetName(path){const parts=[];for(const part of path.split('/')){if(!part||part==='.')continue;if(part==='..')parts.pop();else parts.push(part);}if(parts[0]==='game')parts.shift();return parts.join('/');}
  function audioContext(){if(!audio){const Audio=window.AudioContext||window.webkitAudioContext;if(!Audio)throw Error('Web Audio unavailable');audio=new Audio();meter=audio.createAnalyser();meter.fftSize=512;if(!muted)meter.connect(audio.destination);}return audio;}
  const bufferBytes=buffer=>buffer.length*buffer.numberOfChannels*4;
  let rgb565Lut=null;
  function rgb565(){if(!rgb565Lut){rgb565Lut=new Uint32Array(65536);for(let v=0;v<65536;v++)rgb565Lut[v]=255<<24|(((v<<3&248)|(v>>2&7))<<16)|(((v>>3&252)|(v>>9&3))<<8)|((v>>8&248)|(v>>13&7));}return rgb565Lut;}
  function trimDecodedSounds(budget=32*1024*1024){const active=new Set([...channels.values()].map(record=>record.buffer));if(music)active.add(music.buffer);let idle=0;for(const buffer of sounds.values())if(!active.has(buffer))idle+=bufferBytes(buffer);for(const [path,buffer] of sounds){if(idle<=budget)break;if(active.has(buffer))continue;sounds.delete(path);idle-=bufferBytes(buffer);stats.audioDecodedBytes-=bufferBytes(buffer);}stats.sounds=sounds.size;return stats.audioDecodedBytes||0;}
  function finish(channel,record){if(channels.get(channel)!==record)return;channels.delete(channel);record.node?.disconnect();record.gain?.disconnect();finished.push(channel);trimDecodedSounds();}
  function startChannel(channel,record){
    const a=audioContext(),node=a.createBufferSource(),gain=a.createGain();record.node=node;record.gain=gain;node.buffer=record.buffer;node.loop=record.loops<0;gain.gain.value=(volumes.get(channel)??128)*record.chunkVolume/16384;node.connect(gain).connect(meter);record.started=a.currentTime;
    node.onended=()=>{node.disconnect();gain.disconnect();if(record.node!==node||record.paused||channels.get(channel)!==record)return;if(record.loops>0&&(!record.deadline||a.currentTime<record.deadline)){record.loops--;record.offset=0;startChannel(channel,record);}else finish(channel,record);};
    node.start(0,record.offset||0);if(record.deadline)node.stop(Math.max(a.currentTime,record.deadline));stats.audioStarts=(stats.audioStarts||0)+1;
  }
  function startMusic(record){
    const a=audioContext(),node=a.createBufferSource();record.node=node;node.buffer=record.buffer;node.loop=record.loops<0;node.connect(record.gain).connect(meter);record.started=a.currentTime;
    node.onended=()=>{if(music!==record||record.paused)return;node.disconnect();if(record.loops>0){record.loops--;record.offset=0;startMusic(record);return;}record.gain.disconnect();music=null;musicDone=true;};
    node.start(0,record.offset||0);
  }
  function stopMusicNode(record){if(record.node){record.node.onended=null;try{record.node.stop();}catch{}record.node.disconnect();}}
  const keys={ArrowUp:273,ArrowDown:274,ArrowRight:275,ArrowLeft:276,Enter:13,Escape:27,Space:32,ShiftLeft:304,ShiftRight:303,ControlLeft:306,ControlRight:305};
  const enqueue=event=>{if(events.length===256)events.shift();events.push(event);stats.events++;};
  function key(code,down){if(!Number.isInteger(code)||code<0||code>=323)return;if(held.has(code)===down)return;if(down)held.add(code);else held.delete(code);const e=new Uint8Array(20),v=new DataView(e.buffer);e[0]=down?2:3;e[2]=down?1:0;v.setInt32(8,code,true);v.setUint16(16,code<128?code:0,true);enqueue(e);}
  function coordinates(e){const r=canvas.getBoundingClientRect();return {x:Math.max(0,Math.min(canvas.width-1,Math.floor((e.clientX-r.left)*canvas.width/r.width))),y:Math.max(0,Math.min(canvas.height-1,Math.floor((e.clientY-r.top)*canvas.height/r.height)))};}
  function pointer(e,button){const p=coordinates(e),bytes=new Uint8Array(20),v=new DataView(bytes.buffer);if(button){bytes[0]=e.type==='pointerdown'?5:6;bytes[2]=e.button===2?3:e.button===1?2:1;bytes[3]=e.type==='pointerdown'?1:0;const mask=1<<(bytes[2]-1);mouse.buttons=bytes[3]?mouse.buttons|mask:mouse.buttons&~mask;}else{bytes[0]=4;bytes[2]=mouse.buttons;v.setInt16(8,p.x-mouse.x,true);v.setInt16(10,p.y-mouse.y,true);}v.setUint16(4,p.x,true);v.setUint16(6,p.y,true);Object.assign(mouse,p);enqueue(bytes);}
  canvas.tabIndex=0;
  const onKey=e=>{
    const target=e.target;
    if(target&&target!==canvas&&target.closest&&target.closest('button,input,textarea,select,a,[contenteditable]'))return;
    let physical=e.code,expected=e.key;
    const mapped=globalThis.VascoletControls?VascoletControls.translate(physical):null;
    if(mapped){if(mapped.block){e.preventDefault();return;}physical=mapped.code;expected=mapped.key;}
    const code=keys[physical]??(expected.length===1?expected.toLowerCase().charCodeAt(0):undefined);
    if(code!==undefined){e.preventDefault();key(code,e.type==='keydown');}
  };
  const release=()=>{for(const code of [...held])key(code,false);for(let button=1;button<=5;button++)if(mouse.buttons&(1<<(button-1))){const e=new Uint8Array(20),v=new DataView(e.buffer);e[0]=6;e[2]=button;v.setUint16(4,mouse.x,true);v.setUint16(6,mouse.y,true);enqueue(e);}mouse.buttons=0;};
  window.addEventListener('keydown',onKey);window.addEventListener('keyup',onKey);
  canvas.addEventListener('blur',release);
  window.addEventListener('blur',release);
  canvas.addEventListener('pointerdown',e=>{canvas.focus();canvas.setPointerCapture(e.pointerId);pointer(e,true);audio?.resume();});
  canvas.addEventListener('pointerup',e=>pointer(e,true));
  canvas.addEventListener('pointermove',e=>pointer(e,false));
  canvas.addEventListener('pointercancel',release);
  canvas.addEventListener('contextmenu',e=>e.preventDefault());
  return {
    events,images,mouse,stats,key,finished,
    resumeAudio(){return audioContext().resume();},
    setMuted(value){muted=!!value;if(meter){meter.disconnect();if(!muted)meter.connect(audio.destination);}},
    trimDecodedSounds,
    image(path){return images.get(assetName(path));},
    init(flags){if(flags&0x10)audioContext();stats.initialized|=flags;return true;},
    openAudio(frequency,format,count,chunk){if(frequency<8000||frequency>192000||format!==0x8010||count<1||count>2||chunk<1)return false;audioContext();meter.channelCount=count;meter.channelCountMode='explicit';stats.audioSpec={frequency,format,channels:count,chunk};return true;},
    allocateChannels(count){if(count<0)return channelLimit;if(count>128)throw Error('Diagnostic channel limit exceeded');for(const [channel,record] of channels)if(channel>=count){record.node.onended=null;record.node.stop();finish(channel,record);}channelLimit=count;return count;},
    soundDuration(path){return sounds.get(assetName(path))?.duration||0;},
    play(channel,path,loops,ticks,chunkVolume,chunkID){path=assetName(path);const buffer=sounds.get(path);if(!buffer)return -1;if(channel<0){channel=0;while(channel<channelLimit&&channels.has(channel))channel++;}if(channel>=channelLimit)return -1;const old=channels.get(channel);if(old){old.node.onended=null;old.node.stop();finish(channel,old);}sounds.delete(path);sounds.set(path,buffer);const record={path,buffer,loops,chunkVolume,chunkID,offset:0,paused:false,deadline:ticks>=0?audioContext().currentTime+ticks/1000:0};channels.set(channel,record);startChannel(channel,record);return channel;},
    halt(channel){for(const [id,record] of channels)if(channel<0||channel===id){record.node.onended=null;record.node.stop();finish(id,record);}return 0;},
    freeSound(chunkID){for(const [id,record] of channels)if(record.chunkID===chunkID){record.node.onended=null;record.node.stop();finish(id,record);}},
    pause(channel,paused){for(const [id,record] of channels)if((channel<0||channel===id)&&record.paused!==paused){if(paused){record.offset=((record.offset||0)+audioContext().currentTime-record.started)%record.buffer.duration;record.paused=true;record.node.onended=null;record.node.stop();record.node.disconnect();record.gain.disconnect();}else{record.paused=false;startChannel(id,record);}}},
    volume(channel,value){const ids=channel<0?Array.from({length:channelLimit},(_,i)=>i):[channel];if(channel>=channelLimit)return -1;const previous=Math.floor(ids.reduce((sum,id)=>sum+(volumes.get(id)??128),0)/(ids.length||1));if(value>=0)for(const id of ids){volumes.set(id,Math.min(value,128));const record=channels.get(id);if(record)record.gain.gain.value=Math.min(value,128)*record.chunkVolume/16384;}return previous;},
    playing(channel,pausedOnly=false){return [...channels].filter(([id,r])=>(channel<0||id===channel)&&(!pausedOnly||r.paused)).length;},
    audioSnapshot(){let rms=0;if(meter){const values=new Float32Array(meter.fftSize);meter.getFloatTimeDomainData(values);rms=Math.sqrt(values.reduce((sum,v)=>sum+v*v,0)/values.length);}return {state:audio?.state||'uninitialized',channels:channels.size,starts:stats.audioStarts||0,decodedBytes:stats.audioDecodedBytes||0,rms};},
    async preloadAudio(files,FS){const a=audioContext();for(const file of files){const path=assetName(file);if(!/\.(ogg|wav|mp3)$/i.test(path)||sounds.has(path))continue;const raw=FS.readFile('/game/'+path),buffer=await a.decodeAudioData(raw.buffer.slice(raw.byteOffset,raw.byteOffset+raw.byteLength));if(sounds.has(path))continue;sounds.set(path,buffer);stats.sounds=sounds.size;stats.audioDecodedBytes=(stats.audioDecodedBytes||0)+bufferBytes(buffer);}},
    resize(w,h){if(canvas.width!==w)canvas.width=w;if(canvas.height!==h)canvas.height=h;stats.width=w;stats.height=h;},
    caption(text){stats.lastTitle=text;canvas.setAttribute('aria-label',text);},
    cursor(mode){const previous=shown?1:0;if(mode>=0){shown=!!mode;canvas.style.cursor=shown?'auto':'none';}return previous;},
    async preloadImages(files,FS){
      for(const file of files){const path=file.replace(/^\/game\//,'');if(!/\.(png|jpe?g|bmp)$/i.test(path))continue;
        const blob=new Blob([FS.readFile('/game/'+path)]),bitmap=await createImageBitmap(blob),decoder=document.createElement('canvas');decoder.width=bitmap.width;decoder.height=bitmap.height;const c=decoder.getContext('2d');c.drawImage(bitmap,0,0);const pixels=c.getImageData(0,0,bitmap.width,bitmap.height).data;images.set(path,{w:bitmap.width,h:bitmap.height,pixels});stats.images++;stats.decodedBytes+=pixels.byteLength;bitmap.close();
      }
    },
    present(heap,ptr,w,h,pitch,kind,dw,dh){
      if(!image||source.width!==w||source.height!==h){source.width=w;source.height=h;image=sourceContext.createImageData(w,h);}
      const out=image.data;
      if(kind===2){const lut=rgb565(),out32=new Uint32Array(out.buffer,out.byteOffset,w*h);for(let y=0;y<h;y++){let from=ptr+y*pitch,to=y*w;for(let x=0;x<w;x++)out32[to++]=lut[heap[from]|heap[from+1]<<8],from+=2;}}
      else for(let y=0;y<h;y++)for(let x=0;x<w;x++){
        const to=(y*w+x)*4;
        if(kind===1){const from=ptr+y*pitch+(x>>1)*4,yy=heap[from+(x&1?2:0)],u=heap[from+1]-128,v=heap[from+3]-128,c=(yy-16)*298;out[to]=(c+409*v+128)>>8;out[to+1]=(c-100*u-208*v+128)>>8;out[to+2]=(c+516*u+128)>>8;}
        else if(kind===3){const from=ptr+y*pitch+x*4;out[to]=heap[from];out[to+1]=heap[from+1];out[to+2]=heap[from+2];}
        else throw Error('Unsupported SDL presentation kind');
        out[to+3]=255;
      }
      sourceContext.putImageData(image,0,0);context.drawImage(source,0,0,dw,dh);stats.frames++;
    },
    playMusic(path,loops,volume){path=assetName(path);const buffer=sounds.get(path);if(!buffer)return -1;if(music)this.haltMusic();musicDone=false;musicVol=Math.max(0,Math.min(Number(volume)||0,128));const gain=audioContext().createGain();gain.gain.value=musicVol/128;music={buffer,loops:Number(loops)||0,gain,offset:0,paused:false,node:null,started:0};startMusic(music);return 0;},
    haltMusic(){if(music){stopMusicNode(music);music.gain.disconnect();music=null;}return 0;},
    pauseMusic(paused){if(!music||music.paused===!!paused)return 0;const a=audioContext();if(paused){music.offset=((music.offset||0)+a.currentTime-music.started)%music.buffer.duration;music.paused=true;stopMusicNode(music);music.gain.disconnect();}else{music.paused=false;startMusic(music);}return 0;},
    musicVolume(value){const previous=musicVol;if(value>=0){musicVol=Math.max(0,Math.min(value,128));if(music)music.gain.gain.value=musicVol/128;}return previous;},
    musicPlaying(){return music&&!music.paused?1:0;},
    musicFinished(){if(musicDone){musicDone=false;return 1;}return 0;},
    requestStop:false,
    shouldStop(){return this.requestStop||stats.frames>=frameLimit;}
  };
};
