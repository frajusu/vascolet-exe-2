# AOT host compatibility sources

Snapshot of the C host and ABI headers used for the published worker runtime.
This includes the locale, multibyte and file-descriptor save fixes, allocator
and embedded host regression tests. Native and guest pointers/FILE handles are
kept separate; unsupported paths report a diagnostic.

These files require the generated `recomp.h`, `image.h`, guest CPU/SIMD headers
and translated objects from the ELF recompilation lab. They are not standalone
game sources or a complete reproducible build distribution. See `docs/AOT.md`
for provenance and remaining compatibility limitations; the original game and
its assets retain their own rights.
