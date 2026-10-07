# Licenses and third-party components

The application's own code, scripts, original artwork and test fixture generator
are MIT, except for the LGPL libgme integration described below. The top-level
LICENSE does not relicense dependencies or modified upstream code.

| Component | Version | License / distribution |
| --- | --- | --- |
| Qt Base, Declarative, Multimedia | 6.11.2 | LGPL-3.0; shared libraries and required plugins |
| Qt Shader Tools | 6.11.2 | Build dependency; upstream license terms, source included to rebuild Qt Quick |
| Game Music Emu | 0.6.5 + local patches | LGPL-2.1-or-later; replaceable shared library |
| zlib | 1.3.2 | zlib license; static |
| MinGW GCC runtimes (Windows) | 13.1.0 | GPL-3.0 with GCC Runtime Library Exception 3.1 |
| MinGW-w64 / winpthreads (Windows) | Qt toolchain distribution | Upstream permissive notices |

Qt embeds additional third-party components. Packages include the relevant Qt
LICENSES directories and copyright/license information from each installed
module's SPDX SBOM. Qt Shader Tools contains tools with Qt's GPL exception; these
tools are not linked into the player. No GPL-only Qt application module is used.

The selected YM2612 core is **Nuked OPN2** (LGPL-2.1-or-later). The backend also
contains third-party emulation code, including emu2413 with its upstream notices.
Complete libgme sources retain those notices. No emulator ROMs, game soundtracks,
MIDI ROMs or sound banks are bundled.

## Local libgme modifications

`native/gme-taps/` (the tap implementation, YM2413 adapter and patches.json) is
licensed LGPL-2.1-or-later, copyright 2026 Bit Music Visualizer contributors,
with original notices retained in modified upstream files. The changes add
per-voice capture, hardware channel grouping and YM2413 support. Patch application
is hash-checked by `cmake/PatchGme.cmake`; that original build script is MIT.
The LGPL text is in [LICENSES/LGPL-2.1.txt](LICENSES/LGPL-2.1.txt).

## Redistribution and replacement

Release packaging uses shared Qt and libgme. Compatible replacement libraries
can be used; reverse engineering for debugging modifications to those libraries
is not restricted. No activation or developer signature is required by the app.
macOS may require a new local ad-hoc signature after modifying a bundle.

Every binary archive includes:

- the application MIT LICENSE;
- library license texts and notices in `licenses/`;
- complete corresponding Qt/libgme/zlib source archives, exact URLs and hashes,
  and all local libgme patches in `third-party-sources/`;
- instructions for rebuilding/replacing the libraries.

Keep these files together when redistributing a binary. Default development
builds may use static libgme; use the packaging scripts for release artifacts.
Linux uses the operating system's C/C++ runtime, graphics, X11 and audio libraries;
these system libraries are not redistributed by the tar.gz. macOS system
frameworks are likewise not bundled.

## Sources

- [Qt 6.11 licensing](https://doc.qt.io/qt-6.11/licensing.html)
- [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations)
- [Qt third-party code](https://doc.qt.io/qt-6.11/licenses-used-in-qt.html)
- [Game Music Emu 0.6.5](https://github.com/libgme/game-music-emu/tree/0.6.5)
- [zlib 1.3.2](https://github.com/madler/zlib/tree/v1.3.2)

Source archive hashes are pinned in `packaging/sources.json` and CMakeLists.txt.
