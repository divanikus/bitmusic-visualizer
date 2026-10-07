"""Verify a relocated Unix package and exercise decoding without an audio device."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import json

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from fixtures import make_fixtures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    args = parser.parse_args()
    output = ROOT / 'test-output'
    output.mkdir(exist_ok=True)
    test = Path(tempfile.mkdtemp(prefix='portable space-', dir=output))
    if platform.system() == 'Darwin':
        subprocess.run(['ditto', '-x', '-k', str(args.archive.resolve()), str(test)], check=True)
        stage = test / 'BitMusicVisualizer'
    else:
        # Exercise the actual runtime from a relocated path containing spaces.
        import shutil
        image = test / args.archive.name
        shutil.copy2(args.archive, image)
        image.chmod(0o755)
        if image.read_bytes()[8:11] != b'AI\x02':
            raise RuntimeError('Not a type-2 AppImage.')
        subprocess.run([str(image.resolve()), '--appimage-extract'], cwd=test,
                       stdout=subprocess.DEVNULL, check=True, timeout=120)
        stage = test / 'squashfs-root'
    forbidden = {'.nsf', '.nsfe', '.vgm', '.vgz', '.spc', '.ay', '.gbs', '.sid', '.wav', '.pdb', '.obj'}
    for path in stage.rglob('*'):
        if path.is_symlink() and not path.resolve().is_relative_to(stage.resolve()):
            raise RuntimeError(f'Escaping bundle symlink: {path}')
        if path.suffix.lower() in forbidden or path.name == 'BitMusicVisualizer.ini':
            raise RuntimeError(f'Unwanted package content: {path}')
    listed = set()
    for line in (stage / 'SHA256SUMS.txt').read_text().splitlines():
        expected, relative = line.split('  ', 1)
        path = stage / relative
        if not path.resolve().is_relative_to(stage.resolve()):
            raise RuntimeError('Manifest path escapes package.')
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise RuntimeError('Manifest hash mismatch: ' + relative)
        listed.add(relative)
    actual = {p.relative_to(stage).as_posix() for p in stage.rglob('*')
              if p.is_file() and not p.is_symlink() and p.name != 'SHA256SUMS.txt'}
    if actual != listed:
        raise RuntimeError('Manifest does not cover the complete package.')
    for required in ('LICENSE', 'licenses/THIRD-PARTY.md', 'third-party-sources/sources.json'):
        if required not in listed:
            raise RuntimeError('Missing notices: ' + required)
    if platform.system() == 'Linux':
        for required in ('AppRun', 'bitmusic-visualizer.desktop', 'bitmusic-visualizer.png',
                         'third-party-sources/appimage/appimage-inputs.json'):
            if required not in listed:
                raise RuntimeError('Missing AppImage content: ' + required)
        inputs = json.loads((stage / 'third-party-sources/appimage/appimage-inputs.json').read_text())
        offset = int(subprocess.check_output([str(image.resolve()), '--appimage-offset'], text=True))
        if not 0 < offset < image.stat().st_size:
            raise RuntimeError('Invalid AppImage payload offset.')
        with image.open('rb') as data:
            data.seek(offset)
            if data.read(4) != b'hsqs':
                raise RuntimeError('Missing SquashFS payload.')
        for item in inputs['sources']:
            source = stage / 'third-party-sources/appimage' / item['file']
            if hashlib.sha256(source.read_bytes()).hexdigest() != item['sha256']:
                raise RuntimeError('AppImage source checksum mismatch.')
    fixtures = test / 'fixtures'
    make_fixtures(fixtures)
    env = dict(os.environ)
    for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML_IMPORT_PATH',
                'QML2_IMPORT_PATH', 'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH'):
        env.pop(key, None)
    env['QT_QPA_PLATFORM'] = 'offscreen'
    env['BITMUSIC_SOFTWARE_SCOPES'] = '1'
    if platform.system() == 'Darwin':
        app = stage / 'BitMusicVisualizer.app'
        executable = app / 'Contents/MacOS/bitmusic_visualizer'
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
        for binary in app.rglob('*'):
            if not binary.is_file() or binary.is_symlink():
                continue
            with binary.open('rb') as data:
                magic = data.read(4)
            if magic not in (b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
                             b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
                             b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca'):
                continue
            result = subprocess.run(['otool', '-L', str(binary)], capture_output=True, text=True)
            if result.returncode != 0:
                raise RuntimeError('Cannot inspect Mach-O binary: ' + str(binary))
            for line in result.stdout.splitlines()[1:]:
                # Universal Qt binaries repeat an unindented absolute filename
                # header for each architecture; only indented lines are imports.
                if not line.startswith('\t'):
                    continue
                dependency = line.strip().split(' (', 1)[0]
                if dependency.startswith('/') and not dependency.startswith(('/System/Library/', '/usr/lib/')):
                    raise RuntimeError('Nonportable Mach-O dependency: ' + dependency)
    else:
        executable = stage / 'AppRun'
        dependency_env = dict(env, LD_LIBRARY_PATH=str(stage / 'lib'))
        dependency_errors = []
        for binary in stage.rglob('*'):
            if not binary.is_file() or binary.is_symlink():
                continue
            with binary.open('rb') as data:
                elf = data.read(4) == b'\x7fELF'
            if elf:
                result = subprocess.run(['ldd', str(binary)], capture_output=True, text=True, env=dependency_env)
                if result.returncode or 'not found' in result.stdout:
                    dependency_errors.append(f'Unresolved ELF dependencies: {binary}\n{result.stdout}{result.stderr}')
                for line in result.stdout.splitlines():
                    if 'libQt6' in line or 'libgme' in line:
                        if str(stage) not in line:
                            dependency_errors.append('Library loaded outside relocated package: ' + line)
        if dependency_errors:
            raise RuntimeError('\n'.join(dependency_errors))
    subprocess.run([str(executable), '--tap-checks', str(fixtures), '--decoder-only'], env=env, check=True, timeout=120)
    if platform.system() == 'Linux':
        subprocess.run([str(image.resolve()), '--appimage-extract-and-run', '--tap-checks',
                        str(fixtures.resolve()), '--decoder-only'], env=env, check=True, timeout=180)
        print('Type-2 AppImage runtime and extract-and-run without FUSE PASS.')
    print((fixtures / 'tap-checks.txt').read_text())
    print('Relocated package, manifest, dependency and decoder checks PASS.')
    print('Physical audio output, GPU drivers and desktop interaction still need manual testing.')


if __name__ == '__main__':
    main()
