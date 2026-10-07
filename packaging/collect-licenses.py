"""Collect exact corresponding sources and notices; Python standard library only."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MODULES = ('qtbase', 'qtmultimedia', 'qtdeclarative', 'qtshadertools')


def collect(destination, qt_root):
    licenses = destination / 'licenses'
    sources = destination / 'third-party-sources'
    licenses.mkdir(parents=True, exist_ok=True)
    sources.mkdir(parents=True, exist_ok=True)
    cache = ROOT / '.runtime/sources'
    cache.mkdir(parents=True, exist_ok=True)
    for item in json.loads((ROOT / 'packaging/sources.json').read_text()):
        archive = cache / item['file']
        if not archive.exists():
            temporary = archive.with_suffix(archive.suffix + '.download')
            with urllib.request.urlopen(item['url'], timeout=120) as response, temporary.open('wb') as output:
                shutil.copyfileobj(response, output)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != item['sha256']:
                raise RuntimeError('Source checksum mismatch: ' + item['file'])
            temporary.replace(archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != item['sha256']:
            raise RuntimeError('Source checksum mismatch: ' + item['file'])
        shutil.copy2(archive, sources)
    for name in ('sources.json', 'REBUILD-LIBRARIES.md'):
        shutil.copy2(ROOT / 'packaging' / name, sources)
    patch = sources / 'bitmusic-gme'
    (patch / 'native').mkdir(parents=True, exist_ok=True)
    (patch / 'cmake').mkdir(exist_ok=True)
    shutil.copytree(ROOT / 'native/gme-taps', patch / 'native/gme-taps')
    shutil.copy2(ROOT / 'cmake/PatchGme.cmake', patch / 'cmake')
    shutil.copy2(ROOT / 'packaging/gme-CMakeLists.txt', patch / 'CMakeLists.txt')
    shutil.copytree(ROOT / 'LICENSES', patch / 'LICENSES')
    shutil.copy2(ROOT / 'LICENSE', patch)
    for path in (ROOT / 'LICENSES').iterdir():
        if path.is_file():
            shutil.copy2(path, licenses)
    shutil.copy2(ROOT / 'LICENSE', destination)
    shutil.copy2(ROOT / 'THIRD-PARTY.md', licenses)
    for module in MODULES:
        module_licenses = licenses / module
        module_licenses.mkdir(exist_ok=True)
        with tarfile.open(cache / f'{module}-everywhere-src-6.11.2.tar.xz') as archive:
            prefix = f'{module}-everywhere-src-6.11.2/LICENSES/'
            for member in archive:
                if member.isfile() and member.name.startswith(prefix):
                    # Only license texts, never archive paths/symlinks or executables.
                    with archive.extractfile(member) as data:
                        (module_licenses / Path(member.name).name).write_bytes(data.read())
        sbom = json.loads((qt_root / f'sbom/{module}-6.11.2.spdx.json').read_text(encoding='utf-8'))
        notices = []
        for entry in sbom.get('packages', []):
            notices += [f"Component: {entry['name']} {entry.get('versionInfo', '')}",
                        'License: ' + entry.get('licenseConcluded', ''),
                        entry.get('copyrightText', ''), '']
        for entry in sbom.get('hasExtractedLicensingInfos', []):
            notices += ['License: ' + entry['licenseId'], entry['extractedText'], '']
        if not notices or not any(module_licenses.iterdir()):
            raise RuntimeError('Missing Qt notices: ' + module)
        (licenses / f'{module}-NOTICES.txt').write_text('\n'.join(notices), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--qt-root', required=True, type=Path)
    args = parser.parse_args()
    collect(args.destination, args.qt_root)
