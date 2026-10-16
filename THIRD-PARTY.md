# Licenses and third-party components

The application's own code, scripts, original artwork and test fixture generator
are MIT, except for the LGPL libgme/Munt, GPL SID and BSD ymfm-derived integrations below. The top-level
LICENSE does not relicense dependencies or modified upstream code.

The combined executable containing libsidplayfp is distributed under **GPLv3**
(COPYING). The GPL-2.0-or-later SID code permits this version, which is compatible
with the selected LGPLv3 Qt modules. Individual original application files remain
available under MIT. This applies to both static and dynamic SID integration.

| Component | Version | License / distribution |
| --- | --- | --- |
| Qt Base, Declarative, Multimedia | 6.11.2 | LGPL-3.0; shared libraries and required plugins |
| Qt Shader Tools | 6.11.2 | Build dependency; upstream license terms, source included to rebuild Qt Quick |
| Game Music Emu | 0.6.5 + local patches | LGPL-2.1-or-later; replaceable shared library |
| libsidplayfp / bundled reSIDfp | 2.16.1 + local patches | GPL-2.0-or-later; static, combined application GPLv3 |
| ymfm | 81aec25ccbb98f4873a255f7551ac4dadac59b4a + local voice capture | BSD-3-Clause; static |
| zlib | 1.3.2 | zlib license; static |
| FluidSynth | 2.5.4, unchanged core | LGPL-2.1-or-later; static, combined application GPLv3 |
| Munt / libmt32emu | 2.8.3 + local part capture/mute and deterministic jitter patches | LGPL-2.1-or-later; static, combined application GPLv3 |
| libxmp | 4.7.3 + local channel capture | MIT; static |
| GCEM | 012ae73c6d0a2cb09ffe86475f5c6fba3926e200 | Apache-2.0; FluidSynth table-generation headers |
| ICU (Linux Qt runtime) | 73.2 | Unicode/ICU permissive licenses; shared |
| AppImage type-2 runtime (Linux) | 20251108 | MIT runtime; static libfuse LGPL-2.1, other permissive runtime components |
| MinGW GCC runtimes (Windows) | 13.1.0 | GPL-3.0 with GCC Runtime Library Exception 3.1 |
| MinGW-w64 / winpthreads (Windows) | Qt toolchain distribution | Upstream permissive notices |

Qt embeds additional third-party components. Packages include the relevant Qt
LICENSES directories and copyright/license information from each installed
module's SPDX SBOM. Qt Shader Tools contains tools with Qt's GPL exception; these
tools are not linked into the player. No GPL-only Qt application module is used.

The Linux Qt kit requires ICU 73.2, which is included as shared libraries rather
than relying on a distribution's different ICU ABI. Its complete upstream license
and embedded third-party notices are in [LICENSES/ICU-73.2.txt](LICENSES/ICU-73.2.txt).
Source: [ICU release-73-2](https://github.com/unicode-org/icu/tree/release-73-2).

Linux AppImages add the official type2-runtime, with musl, libfuse, squashfuse,
zstd, zlib and mimalloc. Runtime/dependency notices are in `licenses/appimage/`.
The complete pinned runtime source, its libfuse patch/build scripts, libfuse 3.15.0
and squashfuse 0.5.2 sources are in `third-party-sources/appimage/`. See the pinned
[input list](packaging/appimage-inputs.json) and
[runtime source](https://github.com/AppImage/type2-runtime/tree/dd6cebedcbddde9c82f89b011e8e1d40b6e43868).
The runtime is a separate executable launcher; it does not change the application's
MIT license. appimagetool 1.9.1 is a build tool and is not shipped in the player.

The selected YM2612 core is **Nuked OPN2** (LGPL-2.1-or-later). The backend also
contains third-party emulation code, including emu2413 with its upstream notices.
Complete libgme sources retain those notices. No emulator ROMs, game soundtracks,
MIDI ROMs or runtime sound banks are bundled. The unmodified FluidSynth source
archive includes its upstream test banks and their own notices; these are not
installed into the player's SoundFonts folder.

## Tracker modules

[libxmp 4.7.3](https://github.com/libxmp/libxmp/tree/libxmp-4.7.3) is built
statically with native format loaders and the Amiga/Paula mixer. Archive
depackers, external unpacker helpers and ProWizard are disabled.
The [capture patch](native/xmp-taps/README.md) observes individual mixer voices
and groups them by tracker channel. Original adapter/capture code is MIT;
modified upstream files retain their notices. The pinned complete archive and
all modifications accompany packages. See [MIT license](LICENSES/libxmp-MIT.txt)
and [credits and embedded third-party notices](LICENSES/libxmp-CREDITS.txt).
Modules, sample collections and emulator ROMs are not shipped.

## Roland MT-32

[Munt/libmt32emu 2.8.3](https://github.com/munt/munt/tree/libmt32emu_2_8_3)
(commit `3b05ec276f9e605af86b0eaef7f5eda43477a31f`) is linked statically.
The [LGPL-2.1-or-later patch](native/munt-taps/README.md) adds part capture/mute
and deterministic per-synth timer jitter. Upstream headers and copyright notices
remain intact. The Float renderer is used; no Munt Qt frontend is built or shipped.
See [LGPL](LICENSES/Munt-LGPL-2.1.txt), [accompanying GPL text](LICENSES/Munt-GPL-2.0.txt)
and [authors](LICENSES/Munt-AUTHORS.txt). The pinned complete upstream archive and
all modifications accompany distributions. `native/mt32track.*` is original MIT
adapter code. User ROMs retain their own terms and are excluded from Git/packages.

## General MIDI

[FluidSynth 2.5.4](https://github.com/FluidSynth/fluidsynth/tree/v2.5.4) is built
statically with its C++ OS abstraction, SF2 loader and effects, without GLib,
external audio drivers, libsndfile or DLS. Its source is unchanged; CMake omits
the unnecessary static-library Windows version resource for MinGW paths with
spaces. Public APIs supply separate channel and effect buses.
See [license](LICENSES/FluidSynth-LGPL-2.1.txt) and
[authors](LICENSES/FluidSynth-AUTHORS.txt).

GCEM generates numerical tables during compilation. Its pinned complete source,
[Apache license](LICENSES/GCEM-Apache-2.0.txt) and
[notice](LICENSES/GCEM-NOTICES.txt) are included. `native/midisequence.*` and
`native/miditrack.*` are original MIT application code. Full corresponding
application/dependency sources and rebuild instructions accompany the GPLv3
combined executable. User-supplied banks retain their own terms and are excluded
from source control and release packages.

## Yamaha playback and voice capture

The Yamaha VGM backend uses Aaron Giles's [ymfm](https://github.com/aaronsgiles/ymfm)
under BSD-3-Clause. Its complete notice is in
[LICENSES/ymfm-BSD-3-Clause.txt](LICENSES/ymfm-BSD-3-Clause.txt).
`cmake/Ymfm.cmake` pins the revision/archive hash; packaging includes its complete
upstream source. The derived OPN/OPM/OPL capture in `native/ymfm-voices/` is also
BSD-3-Clause, copyright Aaron Giles and Bit Music Visualizer contributors;
its complete notice is in
[LICENSES/ymfm-voices-BSD-3-Clause.txt](LICENSES/ymfm-voices-BSD-3-Clause.txt).
The wrappers compile the unchanged upstream implementations with derived
classes and an SSG resampler specialization, exposing individual FM, PSG and
sample voices without altering the upstream source files. These
additions are included in the complete corresponding application-source bundle.
`native/yamahatrack.cpp` is original MIT scheduling, resampling and mixing code.
No libvgm source is used.

## Local libgme modifications

`native/gme-taps/` (the tap implementation, YM2413 adapter and patches.json) is
licensed LGPL-2.1-or-later, copyright 2026 Bit Music Visualizer contributors,
with original notices retained in modified upstream files. The changes add
per-voice capture, timestamped NES, Sega PSG, Game Boy and AY pitch-state capture,
hardware channel grouping and YM2413 support. Patch application
is hash-checked by `cmake/PatchGme.cmake`; that original build script is MIT.
The LGPL text is in [LICENSES/LGPL-2.1.txt](LICENSES/LGPL-2.1.txt).
The POKEY adapter in the same directory exposes the existing LGPL SAP sound core
with four simultaneously advancing voice buffers. The application's original
VGM parser, transport and mixing code remains MIT. No libvgm code is incorporated.

## Redistribution and replacement

`native/sid-taps/` and the patched libsidplayfp files are GPL-2.0-or-later.
They add pre-filter taps, oscillator-preserving mute and an independent dither
cursor for each decoder. `cmake/Sid.cmake` builds the pinned complete release
using its supplied generated 6502 drivers; no assembler is required for an
unchanged driver. Full driver source and upstream build rules are retained.
See [SID notices](LICENSES/libsidplayfp-NOTICES.txt) for credits and the embedded
MD5 implementation's permissive notice. Software reSIDfp is the only builder
linked; no hardware/USB driver is linked.

Release packaging uses shared Qt and libgme. Compatible replacement libraries
can be used; reverse engineering for debugging modifications to those libraries
is not restricted. No activation or developer signature is required by the app.
macOS may require a new local ad-hoc signature after modifying a bundle.

Every binary archive includes:

- the application MIT LICENSE;
- GPLv3 COPYING for the combined program;
- library license texts and notices in `licenses/`;
- complete corresponding Qt/libgme/libsidplayfp/ymfm/FluidSynth/GCEM/Munt/zlib source archives, exact URLs and hashes,
  and all local libgme patches in `third-party-sources/`;
- `third-party-sources/bitmusic-visualizer-source.tar.xz`, containing the complete
  application source, all SID patches, build and packaging scripts used for the
  build, with a source-file checksum manifest and no Git history;
- instructions for rebuilding/replacing the libraries.

Keep these files together when redistributing a binary. Default development
builds may use static libgme; use the packaging scripts for release artifacts.
Linux uses the operating system's C/C++ runtime, graphics, X11 and audio libraries;
these system libraries are not redistributed by the AppImage. macOS system
frameworks are likewise not bundled.

## Sources

- [Qt 6.11 licensing](https://doc.qt.io/qt-6.11/licensing.html)
- [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations)
- [Qt third-party code](https://doc.qt.io/qt-6.11/licenses-used-in-qt.html)
- [Game Music Emu 0.6.5](https://github.com/libgme/game-music-emu/tree/0.6.5)
- [libsidplayfp 2.16.1](https://github.com/libsidplayfp/libsidplayfp/releases/tag/v2.16.1)
- [zlib 1.3.2](https://github.com/madler/zlib/tree/v1.3.2)

Source archive hashes are pinned in `packaging/sources.json` and CMakeLists.txt.
