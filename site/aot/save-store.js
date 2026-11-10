/* Separate from legacy CheerpX/Wine databases. One atomic snapshot per game. */
globalThis.VascoletSaveStore=(()=>{
  const number=Number(globalThis.VASCOLET_GAME_NUMBER||2),home='vascolet'+number;
  let database,last='';
  const LIMIT=8*1024*1024;
  function encode(raw){let text='';for(let i=0;i<raw.length;i+=8192)text+=String.fromCharCode(...raw.subarray(i,i+8192));return btoa(text);}
  function validate(save){
    if(save?.format!=='vascolet-save-v1'||save.game!==number||save.engine!=='xo'||!Array.isArray(save.files)||save.files.length>512)throw Error('Respaldo incompatible con Vascolet '+number+' XO.');
    let total=0;const seen=new Set();
    for(const file of save.files){const p=typeof file.path==='string'?file.path.split('/'):[];
      if(p[0]!==home||p.length<2||p.some(part=>!part||part==='.'||part==='..'||/[\\\0]/.test(part))||seen.has(file.path)||typeof file.data!=='string'||file.data.length>Math.ceil(LIMIT/3)*4)throw Error('Ruta o archivo de guardado no válido.');
      seen.add(file.path);total+=atob(file.data).length;if(total>LIMIT)throw Error('El respaldo supera 8 MiB.');
    }return save;
  }
  function open(){if(!database)database=new Promise((resolve,reject)=>{const request=indexedDB.open(globalThis.VASCOLET_SAVE_DATABASE||'vascolet-aot-saves-v1',1);request.onupgradeneeded=()=>request.result.createObjectStore('snapshots');request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error);request.onblocked=()=>reject(Error('El almacenamiento está bloqueado por otra pestaña.'));});return database;}
  async function load(){const db=await open();return new Promise((resolve,reject)=>{const tx=db.transaction('snapshots','readonly'),request=tx.objectStore('snapshots').get(number);request.onsuccess=()=>{try{const save=request.result?validate(request.result):null;last=save?JSON.stringify(save):'';resolve(save);}catch(e){reject(e);}};request.onerror=()=>reject(request.error);});}
  async function put(value){const save=validate(value),text=JSON.stringify(save);if(text===last)return save;const db=await open();await new Promise((resolve,reject)=>{const tx=db.transaction('snapshots','readwrite');tx.objectStore('snapshots').put(save,number);tx.oncomplete=resolve;tx.onerror=()=>reject(tx.error);tx.onabort=()=>reject(tx.error||Error('No se pudo confirmar el guardado.'));});last=text;return save;}
  const pack=files=>validate({format:'vascolet-save-v1',game:number,engine:'xo',files:files.map(file=>({path:file.path,data:encode(file.raw)}))});
  return {load,put,pack,validate};
})();
