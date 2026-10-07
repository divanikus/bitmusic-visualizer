"""Verify a relocated Unix package and exercise decoding without an audio device."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import subprocess
import sys
import tarfile
import tempfile

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
    else:
        with tarfile.open(args.archive) as archive:
            archive.extractall(test, filter='data')
    stage = test / 'BitMusicVisualizer'
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
                dependency = line.strip().split(' (', 1)[0]
                if dependency.startswith('/') and not dependency.startswith(('/System/Library/', '/usr/lib/')):
                    raise RuntimeError('Nonportable Mach-O dependency: ' + dependency)
    else:
        executable = stage / 'BitMusicVisualizer'
        dependency_env = dict(env, LD_LIBRARY_PATH=str(stage / 'lib'))
        for binary in stage.rglob('*'):
            if not binary.is_file() or binary.is_symlink():
                continue
            with binary.open('rb') as data:
                elf = data.read(4) == b'\x7fELF'
            if elf:
                result = subprocess.run(['ldd', str(binary)], capture_output=True, text=True, env=dependency_env)
                if result.returncode or 'not found' in result.stdout:
                    raise RuntimeError(f'Unresolved ELF dependencies: {binary}\n{result.stdout}')
                for line in result.stdout.splitlines():
                    if 'libQt6' in line or 'libgme' in line:
                        if str(stage) not in line:
                            raise RuntimeError('Library loaded outside relocated package: ' + line)
    subprocess.run([str(executable), '--tap-checks', str(fixtures), '--decoder-only'], env=env, check=True, timeout=120)
    print((fixtures / 'tap-checks.txt').read_text())
    print('Relocated package, manifest, dependency and decoder checks PASS.')
    print('Physical audio output, GPU drivers and desktop interaction still need manual testing.')


if __name__ == '__main__':
    main()
