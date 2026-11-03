# Vascolet 2: original XO executable recompiled to WebAssembly

The default engine is now the original Linux i386 game statically translated
to WebAssembly, running synchronously in a dedicated worker. It does not use
CheerpX, Wine, Boxedwine or full-module Asyncify. Original assets are preserved.
The previous engines remain explicitly selectable for compatibility and backups.

## Saves

- Atomic IndexedDB snapshots in `vascolet-aot-saves-v1`, game key `2`.
- Files under `/home/vascolet2`, including the original `save.sav`, are restored
  before the original game's startup. Writable-file close, rename and unlink
  trigger confirmed persistence; a five-second snapshot and visibility flush
  provide a secondary safety net.
- This persists files actually saved by the game, not an emulator save-state.
  Use the original game's checkpoints/save behavior.
- Export/import uses `vascolet-save-v1`, game `2`, engine `xo`. The legacy XO
  database is not deleted or overwritten; export from that engine to migrate.
- Saves are browser/origin specific; export before clearing site data.
- Limits: 512 files, 8 MiB decoded, paths confined to this game's home.

## Deployment

`site/aot/` contains the worker, browser SDL/audio host and prebuilt Wasm/data.
The existing service worker supplies COOP/COEP isolation on GitHub Pages; the
first visit may reload once. HTTPS, service workers, SharedArrayBuffer and
OffscreenCanvas are required. No silent fallback to another engine is used
when the explicitly selected AOT engine fails.

The approved dark launcher, collapsed menu, game navigation and multitouch
controls are preserved. S (long jump) is included alongside X/Z and Enter/Esc.

## Verification and limits

Original first-level gameplay, movement and real audio have run in Chromium.
The storage integration probe writes through the worker FS close hook, confirms
the IndexedDB transaction, destroys the worker, reloads the page, and restores
the same bytes in a fresh worker. Allocator/formatting/host regression tests pass.
Full checkpoint progression, all levels, mobile performance and all save migration
cases are not certified. A short desktop gameplay sample was ~36 FPS, not 60 FPS.

Wasm memory maximum is 512 MiB. Compiler processes were kernel-limited to an
aggregate 1536 MiB, 25% CPU and five minutes per stage; the latest link peaked
near 98 MiB. The browser's total process memory is not covered by that compiler cap.

Translator basis: Djsmk123/pop-two-thrones-browser (MIT), pinned upstream
`b021b20688b2fb84eaf323e0594a9139f4763d2c`, adapted for ELF/i386. The C++
compatibility library is from the existing bundle, not claimed to be the original
XO-shipped library. Full x87 80-bit precision, guest longjmp and unwinding remain
unsupported paths, with explicit diagnostics rather than success stubs.
