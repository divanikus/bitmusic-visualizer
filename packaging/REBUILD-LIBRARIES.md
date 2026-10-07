# Replacing the libraries

The portable player links dynamically to Qt 6.11.2 and Game Music Emu 0.6.5. No signing key or activation is needed to run it with compatible replacement DLLs. Keep Windows x64 architecture, exported interfaces and the MinGW ABI compatible with the supplied build (GNU C++ 13.1.0). Close the player before replacing files. Reverse engineering for debugging library modifications is not restricted.

The exact upstream source archives are included here; URLs and SHA256 values are in `sources.json`. Qt Base supplies Core, Gui, Widgets, Network and the Windows platform plugin; Qt Multimedia supplies native audio output. The package does not ship FFmpeg, SVG, video or TLS plugins. Qt and zlib source is unmodified. The complete LGPL-2.1-or-later libgme tap/YM2413 changes and their CMake integration are in `bitmusic-gme/`. `windeployqt` may adjust the Qt installation prefix for deployment; `qt.conf` selects this folder for plugin lookup.

## Game Music Emu

Keep `bitmusic-gme/` beside the supplied `libgme-0.6.5.tar.gz`. With CMake, Ninja and MinGW available, run from this directory (no Qt or network access is needed):

```powershell
cmake -S bitmusic-gme -B gme-build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build gme-build
```

Replace the bundle's `libgme.dll` with `gme-build/_deps/gme-build/gme/libgme.dll`. This includes the `bm_taps_*` exports required by this version of the player; an unmodified upstream DLL does not provide them. Patches are replayed against hash-checked original files on configure. The application decompresses VGZ input itself through statically linked zlib 1.3.2 (also included as source, under its permissive license).

## Qt

Extract the Qt Base, Qt Shader Tools, Qt Declarative and Qt Multimedia archives. Qt's source trees include their complete CMake build scripts and third-party sources/notices. Build Qt Base first with the same MinGW toolchain, then Shader Tools (build tools for Quick), Declarative (Qml/Quick/QuickWidgets), and Multimedia. For example, from a toolchain shell with CMake and Ninja on PATH:

```powershell
cmake -S qtbase-everywhere-src-6.11.2 -B qtbase-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=C:/qt-rebuilt -DBUILD_SHARED_LIBS=ON -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
cmake --build qtbase-build
cmake --install qtbase-build
cmake -S qtshadertools-everywhere-src-6.11.2 -B qtshadertools-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/qt-rebuilt -DCMAKE_INSTALL_PREFIX=C:/qt-rebuilt -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
cmake --build qtshadertools-build
cmake --install qtshadertools-build
cmake -S qtdeclarative-everywhere-src-6.11.2 -B qtdeclarative-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/qt-rebuilt -DCMAKE_INSTALL_PREFIX=C:/qt-rebuilt -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
cmake --build qtdeclarative-build
cmake --install qtdeclarative-build
cmake -S qtmultimedia-everywhere-src-6.11.2 -B qtmultimedia-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/qt-rebuilt -DCMAKE_INSTALL_PREFIX=C:/qt-rebuilt -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF -DFEATURE_ffmpeg=OFF
cmake --build qtmultimedia-build
cmake --install qtmultimedia-build
```

Use the rebuilt installation's `windeployqt` to supply the matching DLLs and Windows platform plugin to a copy of the bundle. The packaging command/flags are recorded in the repository's `package-native.ps1`; native QAudioSink output requires no FFmpeg plugin. Retain `qt.conf` and test sound and file dialogs. Rebuild commands are guidance for replacing libraries, not a claim of a separately verified full Qt rebuild.

Official Qt build guidance: https://doc.qt.io/qt-6/windows-building.html

Official deployment guidance: https://doc.qt.io/qt-6/windows-deployment.html

## Linux and macOS packages

Linux is distributed as an AppImage. Extract it with
`./BitMusicVisualizer-0.6.0-linux-x64.AppImage --appimage-extract`; the paths below
are relative to `squashfs-root/`. Modified bundles can run directly through AppRun
or be repackaged with appimagetool. No signature check restricts replacements.

The independent AppImage runtime is also replaceable. Its source and libfuse patch
are in `third-party-sources/appimage/type2-runtime-20251108.tar.gz`, alongside the
exact libfuse and squashfuse archives pinned by its build scripts. Follow that
archive's BUILD.md and scripts/docker/build-with-docker.sh to build a modified
runtime, then pass `--runtime-file <modified-runtime>` to appimagetool when packing
the extracted AppDir. This is upstream rebuild guidance, not a claim that this
project separately rebuilt the supplied official runtime. The source/input hashes
and full notices accompany the AppImage.

The same standalone libgme CMake project builds a shared .so on Linux or .dylib on
macOS. Use the package architecture (Linux x64 / macOS arm64) and a compatible C++
runtime. On macOS add `-DCMAKE_OSX_ARCHITECTURES=arm64` and
`-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0` to configuration. Replace the versioned libgme
file in `lib/` (Linux) or `BitMusicVisualizer.app/Contents/Frameworks/` (macOS),
preserving its SONAME/install name and symlinks. The `bm_taps_*` exports are required.

Qt's source build order remains Base, Shader Tools, Declarative and Multimedia.
Use your native compiler and replace `C:/qt-rebuilt` above with a local Unix path.
Use Qt's platform build prerequisites and the package's Qt version. On Linux,
`packaging/package-unix.py` uses Qt's CMake deployment API; on macOS it uses
`macdeployqt`. Both package scripts accept `--qt-root` for a replacement SDK.
Zlib can be replaced by rebuilding the application against the changed source.

After changing libraries in a macOS bundle, renew its local ad-hoc signature:

```sh
codesign --force --deep --sign - BitMusicVisualizer.app
```

No paid certificate, developer account or signing identity is required for this
local signature. It does not provide Apple notarization. No application restriction
prevents running modified libraries. Verify audio and scopes after replacement.

Platform guidance: https://doc.qt.io/qt-6.11/linux-building.html and
https://doc.qt.io/qt-6.11/macos-building.html . These are rebuild instructions,
not a claim that the complete Qt source build has been independently tested.
