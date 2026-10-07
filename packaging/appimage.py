"""Prepare an AppDir and its pinned AppImage runtime/source/license materials."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
INPUTS = ROOT / 'packaging/appimage-inputs.json'


def fetch(item):
    cache = ROOT / '.runtime/appimage'
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / item['file']
    if not path.exists():
        temporary = path.with_name(path.name + '.download')
        with urllib.request.urlopen(item['url'], timeout=120) as response, temporary.open('wb') as output:
            shutil.copyfileobj(response, output)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != item['sha256']:
            raise RuntimeError('AppImage input checksum mismatch: ' + item['file'])
        temporary.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
        raise RuntimeError('AppImage input checksum mismatch: ' + item['file'])
    return path


def prepare(stage):
    inputs = json.loads(INPUTS.read_text())
    tool, runtime = [fetch(item) for item in inputs['tools']]
    tool.chmod(0o755)
    runtime.chmod(0o755)
    shutil.copy2(ROOT / 'packaging/BitMusicVisualizer', stage / 'AppRun')
    (stage / 'AppRun').chmod(0o755)
    shutil.copy2(ROOT / 'packaging/bitmusic-visualizer.desktop', stage)
    shutil.copy2(ROOT / 'assets/player.png', stage / 'bitmusic-visualizer.png')
    (stage / '.DirIcon').symlink_to('bitmusic-visualizer.png')
    sources = stage / 'third-party-sources/appimage'
    notices = stage / 'licenses/appimage'
    sources.mkdir(parents=True)
    notices.mkdir(parents=True)
    shutil.copy2(INPUTS, sources / INPUTS.name)
    for item in inputs['sources']:
        archive_path = fetch(item)
        shutil.copy2(archive_path, sources)
        with tarfile.open(archive_path) as archive:
            for member in archive:
                relative = PurePosixPath(member.name)
                if member.isfile() and relative.name in {'LICENSE', 'LICENSE.txt', 'COPYING', 'COPYING.LIB', 'LGPL2.txt', 'COPYRIGHT'}:
                    if relative.is_absolute() or '..' in relative.parts:
                        raise RuntimeError('Invalid license archive path.')
                    target = notices.joinpath(*relative.parts)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.extractfile(member).read())
    for item in inputs['notices']:
        shutil.copy2(fetch(item), notices / item['file'])
    return tool, runtime


def package(stage, output, tool, runtime):
    env = dict(os.environ, ARCH='x86_64')
    # Do not let appimagetool rewrite desktop metadata after the manifest is made.
    env.pop('VERSION', None)
    subprocess.run([str(tool), '--appimage-extract-and-run', '--no-appstream',
                    '--runtime-file', str(runtime), str(stage), str(output)],
                   env=env, check=True, timeout=300)
    output.chmod(0o755)
