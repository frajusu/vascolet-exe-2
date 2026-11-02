"""Build a static site with checksum-pinned Boxedwine and Wine downloads."""
import hashlib
import json
import pathlib
import shutil
import urllib.request
import zipfile
from native.create_openal_shim import create_shim

ROOT = pathlib.Path(__file__).resolve().parent


def download(item):
    cache = ROOT / '.cache'
    cache.mkdir(exist_ok=True)
    path = cache / item['name']
    if not path.exists() or hashlib.file_digest(path.open('rb'), 'sha256').hexdigest() != item['sha256']:
        print('Downloading', item['url'], flush=True)
        with urllib.request.urlopen(item['url'], timeout=120) as response, path.open('wb') as output:
            shutil.copyfileobj(response, output)
    with path.open('rb') as source:
        actual = hashlib.file_digest(source, 'sha256').hexdigest()
    if actual != item['sha256']:
        raise RuntimeError(f"Checksum mismatch for {path.name}: {actual}")
    return path


def main():
    lock = json.loads((ROOT / 'runtime.lock.json').read_text())
    build = download(lock['build'])
    wine = download(lock['wine'])
    dist = ROOT / 'dist'
    if dist.exists():
        shutil.rmtree(dist)
    shutil.copytree(ROOT / 'site', dist)
    parts = sorted(dist.glob('game.zip.[0-9][0-9][0-9]'))
    if parts:
        with (dist / 'game.zip').open('wb') as output:
            for part in parts:
                with part.open('rb') as source:
                    shutil.copyfileobj(source, output)
                part.unlink()
    with zipfile.ZipFile(dist / 'game.zip') as original:
        audio = original.read('OpenAL32.dll')
        shim = create_shim(audio)
        with zipfile.ZipFile(dist / 'shim-game.zip', 'w', zipfile.ZIP_DEFLATED) as patched:
            for info in original.infolist():
                data = original.read(info.filename)
                name = 'soft_oal.dll' if info.filename.lower() == 'openal32.dll' else info.filename
                patched.writestr(name, data)
            patched.writestr('OpenAL32.dll', shim)
    (dist / 'shim-game.zip').replace(dist / 'game.zip')
    config_path = dist / 'game.json'
    config = json.loads(config_path.read_text(encoding='utf-8'))
    config['zipBytes'] = (dist / 'game.zip').stat().st_size
    config['audioBridge'] = 'default-device-forwarder + winmm'
    config_path.write_text(json.dumps(config,indent=2)+'\n',encoding='utf-8')
    bundle = dist / 'cheerpx' / 'bundle.zip'
    if bundle.exists():
        with bundle.open('rb') as source:
            version = hashlib.file_digest(source,'sha256').hexdigest()[:16]
        filesystem = bundle.parent / ('fs-'+version)
        filesystem.mkdir()
        with zipfile.ZipFile(bundle) as archive:
            for info in archive.infolist():
                target = filesystem / info.filename
                if '..' in pathlib.PurePosixPath(info.filename).parts or info.filename.startswith('/'):
                    raise RuntimeError('Invalid native bundle path')
                if info.is_dir():target.mkdir(parents=True,exist_ok=True)
                else:
                    target.parent.mkdir(parents=True,exist_ok=True)
                    target.write_bytes(archive.read(info.filename))
        native_config = json.loads((ROOT/'native'/'cheerpx'/'bundle-manifest.json').read_text(encoding='utf-8'))
        native_config.update({'version':version,'fsRoot':filesystem.name})
        (bundle.parent/'config.json').write_text(json.dumps(native_config,indent=2)+'\n',encoding='utf-8')
        bundle.unlink()
    runtime = dist / 'runtime'
    runtime.mkdir()
    shutil.copyfile(wine, runtime / 'boxedwine.zip')
    with zipfile.ZipFile(build) as archive:
        for mode, folder in [('jit', 'SingleThreadedJit'), ('mt', 'MultiThreadedJit')]:
            target = runtime / mode
            target.mkdir()
            for name in ['boxedwine.html', 'boxedwine-shell.js', 'boxedwine.js', 'boxedwine.wasm', 'boxedwine.css']:
                (target / name).write_bytes(archive.read(f'Web/{folder}/{name}'))
            if mode == 'jit':
                script_path = target / 'boxedwine.js'
                script = script_path.read_text(encoding='utf-8')
                marker = 'function _emscripten_get_canvas_element_size(target,width,height){'
                if marker not in script:
                    raise RuntimeError('Pinned single-thread presentation probe changed')
                # resizeWebGLCanvas queries this sibling exactly once per swap.
                # Unlike requestAnimationFrame, this counts guest presentations.
                script = script.replace(marker, marker + 'if(Module.vascoletFrame&&UTF8ToString(target)==="#boxedwine-webgl-canvas-0")Module.vascoletFrame();')
                script_path.write_text(script, encoding='utf-8')
            html = (target / 'boxedwine.html').read_text(encoding='utf-8')
            html = html.replace('</head>', '<link rel="stylesheet" href="../../runner.css"></head>')
            marker = '<script src=boxedwine.js async>'
            if marker not in html:
                raise RuntimeError('Upstream runner changed: check integration before publishing')
            html = html.replace(marker, '<script src="../../runner.js"></script>' + marker)
            (target / 'boxedwine.html').write_text(html, encoding='utf-8')
            shutil.copyfile(ROOT / 'vendor' / 'fsfs.js', target / 'fsfs.js')
    print('Static site ready in', dist)


if __name__ == '__main__':
    main()
