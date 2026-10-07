"""Build a Linux tar.gz or Apple Silicon app ZIP with replaceable libraries."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(*arguments, **kwargs):
    subprocess.run([str(arg) for arg in arguments], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt-root', required=True, type=Path)
    args = parser.parse_args()
    qt = args.qt_root.resolve()
    system = platform.system()
    if system not in ('Linux', 'Darwin'):
        parser.error('Use package-native.ps1 on Windows.')
    if (system == 'Linux' and platform.machine() != 'x86_64') or (system == 'Darwin' and platform.machine() != 'arm64'):
        parser.error('Current packages target Linux x64 and macOS arm64 only.')
    version = re.search(r'project\(BitMusicVisualizer VERSION ([0-9.]+)', (ROOT / 'CMakeLists.txt').read_text())[1]
    label = 'linux-x64' if system == 'Linux' else 'macos-arm64'
    name = f'BitMusicVisualizer-{version}-{label}'
    extension = '.tar.gz' if system == 'Linux' else '.zip'
    output = ROOT / 'dist' / (name + extension)
    if output.exists():
        raise RuntimeError(f'Refusing to overwrite {output}')
    build = ROOT / 'build/portable-release'
    cmake_args = ['-DCMAKE_BUILD_TYPE=Release', '-DBITMUSIC_SHARED_GME=ON',
                  f'-DCMAKE_PREFIX_PATH={qt}', '-DCMAKE_INSTALL_LIBDIR=lib']
    if system == 'Darwin':
        cmake_args += ['-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0']
    run('cmake', '-S', ROOT, '-B', build, '-G', 'Ninja', *cmake_args)
    run('cmake', '--build', build, '--parallel', '3')
    parent = Path(tempfile.mkdtemp(prefix='package-', dir=ROOT / 'build'))
    stage = parent / 'BitMusicVisualizer'
    run('cmake', '--install', build, '--component', 'Runtime', '--prefix', stage, '--strip')
    if system == 'Darwin':
        app = stage / 'bitmusic_visualizer.app'
        plugins = app / 'Contents/PlugIns/platforms'
        plugins.mkdir(parents=True)
        plugin_args = []
        for name in ('libqcocoa.dylib', 'libqoffscreen.dylib'):
            plugin = plugins / name
            shutil.copy2(qt / 'plugins/platforms' / name, plugin)
            plugin_args.append(f'-executable={plugin}')
        run(qt / 'bin/macdeployqt', app, '-always-overwrite', '-no-plugins',
            f'-libpath={build / "_deps/gme-build/gme"}', *plugin_args)
        app.rename(stage / 'BitMusicVisualizer.app')
        app = stage / 'BitMusicVisualizer.app'
        # ARM64 requires code signatures. Ad-hoc signing is local and uses no identity.
        run('codesign', '--force', '--deep', '--sign', '-', app)
        run('codesign', '--verify', '--deep', '--strict', app)
    else:
        shutil.copy2(ROOT / 'packaging/BitMusicVisualizer', stage)
        (stage / 'BitMusicVisualizer').chmod(0o755)
    run(sys.executable, ROOT / 'packaging/collect-licenses.py', stage, '--qt-root', qt)
    shutil.copy2(ROOT / 'packaging/README-unix.txt', stage / 'README.txt')
    shutil.copy2(ROOT / 'packaging/THEMES.txt', stage / 'THEMES.txt')
    revision = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain', '--untracked-files=no'], text=True).strip()
    (stage / 'BUILD.txt').write_text(f'Version: {version}\nPlatform: {label}\nSource revision: {revision}\n'
                                   f'Tracked working tree changed: {bool(dirty)}\nQt: 6.11.2 (shared)\n'
                                   'libgme: 0.6.5 + taps/YM2413 (shared)\nzlib: 1.3.2 (static)\n')
    lines = [f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(stage).as_posix()}'
             for path in sorted(stage.rglob('*')) if path.is_file() and not path.is_symlink()]
    (stage / 'SHA256SUMS.txt').write_text('\n'.join(lines) + '\n')
    output.parent.mkdir(exist_ok=True)
    if system == 'Darwin':
        run('ditto', '-c', '-k', '--keepParent', stage, output)
    else:
        with tarfile.open(output, 'w:gz') as archive:
            archive.add(stage, arcname='BitMusicVisualizer')
    Path(str(output) + '.sha256').write_text(hashlib.sha256(output.read_bytes()).hexdigest() + '  ' + output.name + '\n')
    print(f'Package: {output}')


if __name__ == '__main__':
    main()
