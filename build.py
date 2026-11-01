"""Build a static site with checksum-pinned Boxedwine and Wine downloads."""
import hashlib
import json
import pathlib
import shutil
import urllib.request
import zipfile

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
    runtime = dist / 'runtime'
    runtime.mkdir()
    shutil.copyfile(wine, runtime / 'boxedwine.zip')
    with zipfile.ZipFile(build) as archive:
        for mode, folder in [('jit', 'SingleThreadedJit'), ('st', 'SingleThreaded')]:
            target = runtime / mode
            target.mkdir()
            for name in ['boxedwine.html', 'boxedwine-shell.js', 'boxedwine.js', 'boxedwine.wasm', 'boxedwine.css']:
                (target / name).write_bytes(archive.read(f'Web/{folder}/{name}'))
            shutil.copyfile(ROOT / 'vendor' / 'fsfs.js', target / 'fsfs.js')
    print('Static site ready in', dist)


if __name__ == '__main__':
    main()
