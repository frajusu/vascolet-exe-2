"""Build the static browser site from site/ (WebAssembly AOT + CheerpX)."""
import hashlib
import json
import pathlib
import shutil
import zipfile
from native.create_openal_shim import create_shim

ROOT = pathlib.Path(__file__).resolve().parent


def main():
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
    print('Static site ready in', dist)


if __name__ == '__main__':
    main()
