# Building and packaging

Use Qt **6.11.2** (Core/Gui/Widgets, Multimedia and Declarative/QuickWidgets),
CMake 3.24+, Ninja and a C++17 compiler. Qt 6.11's QAudioSink callback API is used;
older distribution Qt packages will not build this application. Python 3.12+ is
used by packaging/check scripts and by the original test-tune generator.
Python is not an application runtime dependency.

## Windows

Install the Qt MinGW 64-bit kit and its GCC 13.1 toolchain. Defaults below match
the usual `C:\Qt` layout; script parameters override every tool path.
Packaging needs Python on PATH, or `package-native.ps1 -Python <path-to-python>`.
The shared standard-library license collector avoids Windows Server tar stalls.

```powershell
./build-native.ps1 -Deploy
./launch.cmd
./package-native.ps1
python -c "from fixtures import make_fixtures; make_fixtures('test-output')"
./test-portable.ps1 -Archive dist/BitMusicVisualizer-0.6.0-windows-x64.zip
```

Portable tests use a real output device at zero volume, isolate preferences and
SDK paths, and verify playback, seeks, loops, scopes, themes, geometry, taps and
GPU output. They need an interactive desktop/audio device. Hosted Windows CI
runs decoder/tap checks; it is not a substitute for the full workstation suite.

## Linux / macOS

Set `QT_ROOT_DIR` to the Qt kit (Linux `gcc_64`, macOS `macos`). With the compiler,
CMake and Ninja on PATH:

```sh
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" -DBITMUSIC_SHARED_GME=ON
cmake --build build/native --parallel 3
python3 packaging/package-unix.py --qt-root "$QT_ROOT_DIR"
python3 packaging/test-unix.py dist/BitMusicVisualizer-0.6.0-linux-x64.AppImage
# On Apple Silicon use the generated -macos-arm64.zip instead.
```

The Linux AppImage bundles Qt, libgme and the Qt kit's ICU 73.2; libc, libstdc++, graphics/X11 and audio
libraries come from the operating system. It builds on Ubuntu 22.04 x64 (glibc
2.35), using X11 or XWayland, and is checked in a Fedora 43 container without an
SDK or FUSE. This does not promise compatibility with every Linux distribution.
Typical Ubuntu runtime packages include `libgl1`, `libegl1`, `libpulse0`,
`libasound2` (or `libasound2t64` on newer systems), `libxcb-cursor0`, `libxkbcommon-x11-0`,
`libxcb-icccm4`, `libxcb-keysyms1`, `libxcb-shape0` and `libxcb-xinerama0`.
The build job installs development equivalents. Use system PulseAudio or
PipeWire's PulseAudio compatibility service. Native Wayland packaging is deferred.

The AppDir contains AppRun, the desktop entry, icon, replaceable libraries,
licenses and corresponding sources. `packaging/appimage-inputs.json` pins
appimagetool 1.9.1 and type2-runtime 20251108 by SHA256 and stable release URL.
Packaging does not use mutable continuous downloads. The statically linked
runtime needs no separately installed libfuse2. For hosts without FUSE access:

```sh
chmod +x BitMusicVisualizer-0.6.0-linux-x64.AppImage
./BitMusicVisualizer-0.6.0-linux-x64.AppImage --appimage-extract-and-run
```

`--appimage-extract` exposes the bundle as `squashfs-root/`, including all sources
and notices. Run `squashfs-root/AppRun` after replacing compatible libraries;
the application does not prevent modifications. Settings still use XDG paths,
not the read-only AppImage mount. No desktop integration or update daemon is installed.

The macOS target is Apple Silicon, macOS 13+. Qt libraries, the Cocoa platform
plugin and libgme are inside the .app. A local ad-hoc signature permits ARM64
execution without embedding a developer identity; there is no Developer ID or
notarization. The ZIP preserves framework links. Gatekeeper approval may be
needed after downloading. Test real playback and GPU/Retina behavior on a Mac.

`test-unix.py` extracts to a new path, checks the complete SHA256 manifest, rejects
music/settings and escaping symlinks, audits dynamic dependencies and runs the
decoder/tap suite without needing an audio device. On Linux it also executes the
actual relocated AppImage with `--appimage-extract-and-run`, verifies the runtime
header/SquashFS payload and pinned runtime-library sources. This is **not** a physical
audio/GPU/desktop test. Both Unix platforms need that manual validation before
their first release is described as tested.

## Dependencies and sources

CMake fetches hash-pinned libgme and zlib source archives. `.runtime/sources/`
caches them. Packaging additionally downloads the exact Qt source archives in
`packaging/sources.json`, verifies hashes and includes them with notices generated
from the installed SDK's `sbom/` files. Missing source or notices fails packaging.
The libgme modifications are hash-checked and replayed on each configure.

Qt and libgme stay shared in all distributable packages. See
[THIRD-PARTY.md](../THIRD-PARTY.md) and
[library replacement instructions](../packaging/REBUILD-LIBRARIES.md).

## CI and first release

`Build packages` runs on pushes to main, pull requests and manual dispatch. It
produces Windows x64, Linux x64 and macOS arm64 archives and adjacent SHA256 files.
Actions are pinned to commits; the workflow has read-only repository permission
and does not publish releases. Download artifacts from the Actions run for the
exact commit being released. Retain the third-party source/notices inside them.

The Windows job pins an upstream aqtinstall revision that understands Qt 6.11's
separate MinGW/compiler repository folders; released aqtinstall 3.3.0 selects a
nonexistent path for that SDK. Linux explicitly installs the kit's ICU archive.

Before publishing v0.6.0:

1. Confirm all selected platform jobs pass and BUILD.txt identifies the intended
   clean source commit. Run full Windows tests and Unix desktop listening checks.
2. Inspect tracked files, commit author metadata and artifacts for private data.
   Never add personal music, preferences or logs to a release.
3. Create tag `v0.6.0` at that commit and a draft release using RELEASE-NOTES.md.
4. Attach tested archives and their `.sha256` files. Mark any platform's incomplete
   runtime validation explicitly, or defer its asset. Publish after review.

There is no installer or automatic update service. Future releases follow the
same process; source changes after an initial public import use normal commits.
