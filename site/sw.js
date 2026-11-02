'use strict';
const CACHE = 'vascolet-exe-runtime-v2-1';
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
  if (event.request.method !== 'GET' || url.origin !== self.location.origin || event.request.headers.has('Range')) return;
  event.respondWith((async () => {
    const immutable = /\/(?:game\.zip|runtime\/boxedwine\.zip|runtime\/(?:jit|mt)\/(?:boxedwine\.(?:wasm|js)|fsfs\.js))$/.test(url.pathname);
    // Wine is identical across the four repositories: download and cache it once.
    const key = url.pathname.endsWith('/runtime/boxedwine.zip') ? new URL('/__vascolet_wine_ae2ed179.zip',url.origin).href : event.request;
    let cache;
    if (immutable) { cache = await caches.open(CACHE); const hit = await cache.match(key); if (hit) return isolated(hit); }
    const response = await fetch(event.request);
    if (cache && response.ok) event.waitUntil(cache.put(key,response.clone()).catch(() => {}));
    return isolated(response);
  })());
});
