"""Bundle the application's corresponding source, without Git history or identities."""
import argparse
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def bundle(destination):
    def git(*args):
        return subprocess.check_output(['git', '-C', str(ROOT), *args])

    # Untracked source is not a reproducible/reviewed release input. Stage new
    # files first; modified tracked files are included as actually built.
    if git('ls-files', '--others', '--exclude-standard', '-z').strip(b'\0'):
        raise RuntimeError('Stage or remove untracked release inputs before packaging.')
    paths = [p.decode('utf-8') for p in git('ls-files', '-z').split(b'\0') if p]
    required = {'CMakeLists.txt', 'native/sidtrack.cpp', 'native/sid-taps/patches.json',
                'cmake/Sid.cmake', 'cmake/PatchSid.cmake', 'COPYING', 'LICENSE',
                'native/pokeytrack.cpp', 'native/vgm.cpp', 'native/gme-taps/pokey.cpp',
                'native/gme-taps/pokey.h', 'cmake/PatchGme.cmake',
                'native/yamahatrack.cpp', 'native/yamahasamplechecks.cpp', 'native/romfiles.cpp',
                'native/ymfm-voices/opn.cpp', 'native/ymfm-voices/ym2203.h',
                'native/ymfm-voices/opm.cpp', 'native/ymfm-voices/opl.cpp',
                'native/ymfm-voices/voices.h', 'native/voicemask.h',
                'native/yamahaallvoicechecks.cpp',
                'native/yamahavoicechecks.cpp', 'LICENSES/ymfm-voices-BSD-3-Clause.txt',
                'packaging/YAMAHA-ROMS.txt', 'cmake/Ymfm.cmake', 'LICENSES/ymfm-BSD-3-Clause.txt'}
    if not required.issubset(paths):
        raise RuntimeError('Application source bundle is incomplete.')
    forbidden = {'.sid', '.nsf', '.nsfe', '.vgm', '.vgz', '.spc', '.gbs', '.ay',
                 '.mdx', '.pdx', '.mid', '.rom', '.wav', '.exe', '.dll', '.pdb'}
    sources = destination / 'third-party-sources'
    sources.mkdir(parents=True, exist_ok=True)
    target = sources / 'bitmusic-visualizer-source.tar.xz'
    manifest = []
    with tarfile.open(target, 'w:xz') as archive:
        for name in sorted(paths):
            path = ROOT / name
            if path.is_symlink() or not path.is_file() or path.suffix.lower() in forbidden:
                raise RuntimeError('Unsupported application source input: ' + name)
            data = path.read_bytes()
            info = tarfile.TarInfo('bitmusic-visualizer-source/' + name)
            info.size = len(data)
            info.mode = 0o644
            archive.addfile(info, io.BytesIO(data))
            manifest.append(hashlib.sha256(data).hexdigest() + '  ' + name)
        data = ('\n'.join(manifest) + '\n').encode()
        info = tarfile.TarInfo('bitmusic-visualizer-source/SOURCE-SHA256SUMS.txt')
        info.size = len(data)
        archive.addfile(info, io.BytesIO(data))
    print('Corresponding application source: ' + target.name)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    bundle(parser.parse_args().destination)
