'use strict';
const CACHE = 'vascolet-exe-runtime-v4-xo1';
self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
function isolated(response) {
  if (response.type === 'opaque' || response.status === 0) return response;
  const headers = new Headers(response.headers);
  headers.set('Cross-Origin-Opener-Policy','same-origin');
  headers.set('Cross-Origin-Embedder-Policy','require-corp');
  headers.set('Cross-Origin-Resource-Policy','same-origin');
  return new Response(response.body,{status:response.status,statusText:response.statusText,headers});
}
self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  if (event.request.method !== 'GET' || url.origin !== self.location.origin) return;
  const native=url.pathname.includes('/cheerpx/fs-');
  const requestedRange=event.request.headers.get('Range');
  if(requestedRange&&!native)return;
  event.respondWith((async () => {
    if(native&&requestedRange){
      const match=/^bytes=(\d+)-(\d*)$/.exec(requestedRange);
      if(!match)return isolated(await fetch(event.request));
      const cache=await caches.open(CACHE);let full=await cache.match(url.href);
      if(!full){const headers=new Headers(event.request.headers);headers.delete('Range');full=await fetch(new Request(url.href,{headers,credentials:'same-origin'}));if(full.ok)event.waitUntil(cache.put(url.href,full.clone()).catch(()=>{}));}
      if(!full.ok)return isolated(full);
      const bytes=await full.arrayBuffer(),start=Number(match[1]),end=Math.min(match[2]?Number(match[2]):bytes.byteLength-1,bytes.byteLength-1);
      const headers=new Headers(full.headers);headers.delete('Content-Encoding');headers.set('Accept-Ranges','bytes');
      if(start>end||start>=bytes.byteLength){headers.set('Content-Range',`bytes */${bytes.byteLength}`);return isolated(new Response(null,{status:416,headers}));}
      headers.set('Content-Range',`bytes ${start}-${end}/${bytes.byteLength}`);headers.set('Content-Length',String(end-start+1));
      return isolated(new Response(bytes.slice(start,end+1),{status:206,headers}));
    }
    const immutable = native || /\/(?:game\.zip|runtime\/boxedwine\.zip|runtime\/(?:jit|mt)\/(?:boxedwine\.(?:wasm|js)|fsfs\.js))$/.test(url.pathname);
    // Wine is identical across the four repositories: download and cache it once.
    const key = url.pathname.endsWith('/runtime/boxedwine.zip') ? new URL('/__vascolet_wine_ae2ed179.zip',url.origin).href : event.request;
    let cache;
    if (immutable) { cache = await caches.open(CACHE); const hit = url.pathname.endsWith('/runtime/boxedwine.zip') ? await caches.match(key) : await cache.match(key); if (hit) return isolated(hit); }
    const response = await fetch(event.request);
    if (cache && response.ok) event.waitUntil(cache.put(key,response.clone()).catch(() => {}));
    return isolated(response);
  })());
});
