"""Rebuild the three adapter binaries with portable Zig 0.13.0."""
import os
import pathlib
import shutil
import subprocess
import zipfile

HERE=pathlib.Path(__file__).resolve().parent
ROOT=HERE.parent.parent
zig=os.environ.get('ZIG') or shutil.which('zig')
if not zig:raise SystemExit('Set ZIG to a Zig 0.13.0 executable, or put zig on PATH.')
output=ROOT/'.cache'/'cheerpx-rebuild'
output.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(ROOT/'site'/'cheerpx'/'bundle.zip') as archive:
    (output/'libNAT-1.2.so.0').write_bytes(archive.read('lib/libNAT-1.2.so.0'))
common=[zig,'cc','-target','x86-linux-gnu.2.17','-shared','-fPIC','-O2']
subprocess.run(common+[str(HERE/'sdl_bridge.c'),'-ldl','-Wl,-soname,libSDL-1.2.so.0','libNAT-1.2.so.0','-o','libSDL-1.2.so.0'],cwd=output,check=True)
subprocess.run(common+[str(HERE/'mixer_anchor.c'),'-Wl,-soname,libSDL_mixer-1.2.so.0','libSDL-1.2.so.0','-o','libSDL_mixer-1.2.so.0'],cwd=output,check=True)
subprocess.run([zig,'cc','-target','x86-linux-musl','-static','-Os',str(HERE/'native_init.c'),'-o','native-init'],cwd=output,check=True)
print('Rebuilt adapter files:',output)
