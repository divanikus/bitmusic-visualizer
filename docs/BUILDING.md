# Building and packaging

Use Qt **6.11.2** (Core/Gui/Widgets, Multimedia and Declarative/QuickWidgets),
CMake 3.24+, Ninja and a C++17 compiler. Qt 6.11's QAudioSink callback API is used;
older distribution Qt packages will not build this application. Python 3.12+ is
used by packaging/check scripts and by the original test-tune generator.
Python is not an application runtime dependency.

## Player and check runner

The default build produces two executables from the same player objects:

- `bitmusic_visualizer` is the user application; it contains no automated suites.
- `bitmusic_checks` is the developer runner for every check command below.
  Add `.exe` on Windows. It needs the same Qt/library deployment as the player.

Use `-DBITMUSIC_BUILD_TESTS=OFF` to configure a player-only build, or build just
`--target bitmusic_visualizer`. The player keeps `--diagnostics` and
`--software-scopes`, and supports `--help` / `--version`. It rejects check flags.

Packaging builds a matching runner in `build/portable-checks/` but **does not
include it in ZIPs or AppImages**. Portable tests first audit the unmodified
package, then inject this companion into the temporary extracted copy to test
its shipped libraries. The real player is separately smoke-tested with `--version`.
Use `test-portable.ps1 -ChecksExecutable <path>` or `test-unix.py --checks <path>`
when the matching runner is elsewhere; do not mix runners and unrelated releases.
The Unix companion is installed/relocated (and ad-hoc signed on macOS) before
being removed from staging. `Runtime` installs only the player; the separate
`CheckRunner` component is for packaging validation.

For native code ownership and the card/preset boundary, see [native internals](INTERNALS.md).

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
./test-portable.ps1 -Archive dist/BitMusicVisualizer-0.10.0-windows-x64.zip
```

Portable tests use a real output device at zero volume, isolate preferences and
SDK paths, and verify playback, seeks, loops, scopes, themes, geometry, taps and
GPU output. They need an interactive desktop/audio device. Hosted Windows CI
runs decoder/tap checks; it is not a substitute for the full workstation suite.

All bare check flags in this guide are arguments to `bitmusic_checks`.

`--self-test test-output --loading-only` checks content-based routing, gzip
first-member/trailing/error and 64 MiB size policies, raw SID precedence,
missing/unsupported errors and selected-only versus full GME metadata. It uses
private copies of authored fixtures and also runs in transport/full checks.
It also covers GME title/game/Unicode-filename fallbacks and pure VGM timing/
stereo probes at legacy/extended offsets, including waits, loop boundaries,
data-block/metadata exclusion and deliberately different unsupported-command rules.

`--playlist-checks test-output` checks queue identities/reordering, M3U8 subsongs,
Unicode/relative paths, shuffle history, background metadata cancellation and
live transport across files, repeats, edits, unknown durations and saved modes.
Mouse checks cover source-header group selection and independent first-song
selection/playback, stable selection during rebuilds and independent playback
markers. Live checks cover appends during single-file expansion, superseded opens,
navigation over pending replacement, selected-entry Play versus current-entry F7/F8,
and paused/playing timeline seeks. It also checks decoder end/loop boundaries without VGM header
timing, tracker loop/reset, the tabbed Settings window, resource presence/size
refresh, live FPS, preference restoration and optional unknown-duration limits
through natural queue advancement, replay and repeat.
Settings checks also cover the resource catalog's size/count bounds without
content validation, selection/path and MIDI-bank refresh, and saved-limit
callbacks suppressed after a write failure.
Resource-error checks cover exact required-message prefixes/titles, one warning
per load generation, retry/recovery, ordinary validation errors without a popup,
and bounded ROM reads with unchanged snapshots after file removal.
Add `--decoder-only` for headless model/import/boundary checks. The live check saves a
player screenshot and runs with isolated settings and silent audio.
`--self-test test-output --transport-only` runs the existing transport, scope
continuity, compact-player, layout and shutdown checks without unrelated backend
and presentation suites. The normal self-test still includes all of them.
Both also exercise command failures and cancellation; `--engine-only` runs just
those checks. They cover empty seeks, exact load errors and retained metadata,
recovery, paused/clamped positions, long seeks superseded by a seek, failed load
or unload, and shutdown cancellation with silent audio and isolated settings.
`--scope-worker-only` focuses on fallback NSF and shared SPC capture: paused
history retention across mask/FPS edits, empty-mask suspension, seek/revision
cancellation, snapshot shape/immutability and scope-only decoder/task error
recovery. These checks also run in the transport and full suites. Error probes remove/restore
only private copies of authored fixtures while the audio decoder remains loaded.
`--capture-only` checks idempotent controls, channel-mask filtering versus full
visibility resets, independent chip/output capture, paused token invalidation,
busy/paused seeks and unload. A private 20 ms NSFE copy checks unload inside the
pause tolerance; unloading must never freeze Full mix history. These checks also
cover wide/asymmetric channel rows, generation-only and token-only history resets,
stable-identity appends and a block larger than the history capacity. They also
run in the transport and full suites.

For a focused frame-rate check after generating fixtures, run
`build/native/bitmusic_checks --self-test test-output --fps-only`.
It uses temporary settings and silent playback, exercises 30/60/120 FPS with
NSF/VGZ/SPC, and reports refresh requests and fresh snapshots per second. These
counts are not monitor-presented FPS. Add `--software-scopes` to check CPU drawing.

`--self-test test-output --views-only` checks spectra, NES/PSG/GBS/AY pitch-state capture,
multi-note keyboards, duplicate cards, audio continuity and transport changes.
It also checks per-source analysis reuse/invalidation, pitch cadence and rewind,
wave/spectrum geometry, and missing physical-side fallbacks with Stereo off.
Add `--software-scopes` for CPU rendering. These checks also run in the complete native suite.

`--music-view-checks test-output` checks MIDI gate spans, program changes and
part routing, indexed note lookup, tracker rows at the audible cursor, empty/late/
instrument-only channels, presets, and Piano roll/Pattern painting through resize,
seek, fullscreen and hide/show. Add `--software-scopes` for CPU drawing or
`--decoder-only` for event-data checks without a display/audio device. It uses
original generated fixtures and isolated settings. MT-32 SysEx routing is also
checked by `--mt32-checks` when user ROMs are supplied. Portable Windows tests
run the view checks; hosted platform checks run their decoder-only portion.

`--self-test test-output --waves-only` checks waveform controls and output capture,
plus rendered line thickness, L/R/mono level meters, settings, and fullscreen
entry/exit with restored window/editor state. Run with and without
`--software-scopes`. `--gpu-checks test-output` also verifies thick GPU ribbons
and instantaneous meter fills without glow or stale trails.
It also checks visible trail decay with a 0.5 px stroke, a 120 FPS target,
Levels and Full mix enabled, before/during/after fullscreen.
GPU and window-settings checks isolate their settings under the output directory
so restricted system temporary folders do not trigger modal write warnings.

`--self-test test-output --shortcuts-only` exercises keyboard events in both
windows, number-row/numpad mapping, duplicate/reordered/hidden cards, independent
mute and visibility, Full mix on 0/Alt+0, L/R output toggles, input/dialog
protection, transport and reset. Solo checks cover Shift-click, Ctrl+Shift+1–9,
switching/restoring mutes, manual overrides and transport/file changes. It also checks
the visualizer help button and F1 guide in both windows, saving screenshots with isolated
preferences and silent playback. It is included in the complete native suite.
Channel-control checks additionally cover high source IDs, snapshot feedback
suppression and instrument-label changes without rebuilding buttons.

`--self-test test-output --presets-only` checks preset serialization/validation,
hardware identities, same-platform transfer, default assignment/removal/restart,
missing/corrupt defaults, Keep grid and live NSF/VGZ application without transport
or mute changes. It saves editor screenshots and isolates settings and presets.
It also runs in the complete native suite.

`--self-test test-output --configuration-only` checks fixed sound-configuration
IDs, names and ordered source roles captured before refactoring. Cases include
NES expansions/NSFE, opaque UTF-8 layouts, legacy/variant Sega VGM, POKEY and all
supported Yamaha types, dual/mixed/variant chips, extra clocks/gains and exact
malformed-header errors. Standalone and reused-header VGM paths must match the
same expectations. These pure checks also run in preset/full suites;
they do not open audio or use user presets/settings.

`--self-test test-output --vgm-only` checks clock version/header bounds, chip
descriptions and variants, unsupported-name/error ordering, extra-header clock/
volume rules and fixed Yamaha native-index/role/display-label snapshots. It also
classifies all 256 opcodes against the fixed Yamaha register-write mapping,
including second instances and register banks. It runs in configuration/preset/
full suites and uses authored header bytes only.

`--self-test test-output --layout-only` checks card spans and editor tab navigation,
selection/capture retention, adaptive card-list/help sizing, bounded sidebar/toolbar
geometry and reachable card controls at normal and minimum sizes. It saves screenshots
of Cards/Layout/Display.
`--settings-only` additionally checks expanded pixel-size controls at minimum size.

`--self-test test-output --sid-only` checks SID voices, models/clocks, mutes/Solo,
seeking, Full mix and cancellation. Add `--decoder-only` for headless tests.
See [SID boundaries and checks](SID.md).

`--self-test test-output --pokey-only` checks single-POKEY VGM/VGZ playback,
clock-derived pitch, all four synchronized signals, mutes/Solo, seeks/loops,
mono UI and named unsupported-chip errors. Add `--decoder-only` for headless
checks; optional additional POKEY file paths are read only. The decoder portion
also runs under `--tap-checks`. See [VGM boundaries](VGM.md).

`--self-test test-output --yamaha-only` checks YM2203/YM2151/OPL/OPL2/OPL3 and
YM2608/YM2610/2610B/Y8950 with synthetic ADPCM and on-demand ROM scenarios,
single/dual/mixed chips, actual PCM alignment, stereo, mute/Solo, timer-driven
CSM, clocks/volumes, seeking/repeat and live cards. Add `--decoder-only` for
headless checks. All accepted Yamaha cores are compared bit for bit with their
unmodified upstream output across FM algorithms, shared DAC/clipping, rhythm,
OPL3 four-operator pairs, noise and internal rate conversion. Individual voice
checks cover every single/dual type, including YM2610B, high-numbered mutes,
70-voice live controls and the maximum 184-source configuration. YM2203 additionally
compares every FM mute combination. Theme/preset checks cover expanded source
counts and older 32-palette files. Synthetic maximum-size decoding is a stress
check, not a guarantee of real-time performance for every combination.
Optional additional Yamaha VGM/VGZ paths are read only.

`--self-test test-output --yamaha-loading-only` checks exact constructor/error
precedence, waits/loop overrides, sample-bank bounds, the block/event limit
boundaries, GD3/Unicode fallbacks, selected-only metadata and stable identity.
It also checks shared GD3 behavior through POKEY and runs in Yamaha/full suites.
Authored bytes and an empty private rhythm snapshot keep this suite independent
of music files, user resources and audio devices.
The decoder portion also runs in `--tap-checks`. ymfm is a pinned static C++
dependency; no new SDK or runtime DLL is required. Tests need no proprietary
ROMs. Real YM2608 rhythm playback uses the user's optional ROM (see
[locations](../packaging/YAMAHA-ROMS.txt)).

## Linux / macOS

Set `QT_ROOT_DIR` to the Qt kit (Linux `gcc_64`, macOS `macos`). With the compiler,
CMake and Ninja on PATH:

```sh
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" -DBITMUSIC_SHARED_GME=ON
cmake --build build/native --parallel 3
python3 packaging/package-unix.py --qt-root "$QT_ROOT_DIR"
python3 packaging/test-unix.py dist/BitMusicVisualizer-0.10.0-linux-x64.AppImage
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
PipeWire's PulseAudio compatibility service. Native Wayland packaging is deferred;
unused Wayland and EGLFS helper plugins are excluded along with their platform plugins.

The AppDir contains AppRun, the desktop entry, icon, replaceable libraries,
licenses and corresponding sources. `packaging/appimage-inputs.json` pins
appimagetool 1.9.1 and type2-runtime 20251108 by SHA256 and stable release URL.
Packaging does not use mutable continuous downloads. The statically linked
runtime needs no separately installed libfuse2. For hosts without FUSE access:

```sh
chmod +x BitMusicVisualizer-0.10.0-linux-x64.AppImage
./BitMusicVisualizer-0.10.0-linux-x64.AppImage --appimage-extract-and-run
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
separate decoder/tap runner without needing an audio device. On Linux it also executes the
actual relocated AppImage with `--appimage-extract-and-run`, verifies the runtime
header/SquashFS payload and pinned runtime-library sources. This is **not** a physical
audio/GPU/desktop test. Both Unix platforms need that manual validation before
their first release is described as tested.

## Dependencies and sources

CMake fetches hash-pinned libgme, libsidplayfp, ymfm, FluidSynth, GCEM, Munt, libxmp, TIATracker and zlib source archives. `.runtime/sources/`
caches them. Packaging additionally downloads the exact Qt source archives in
`packaging/sources.json`, verifies hashes and includes them with notices generated
from the installed SDK's `sbom/` files. Missing source or notices fails packaging.
The libgme modifications are hash-checked and replayed on each configure.
The SID modifications follow the same approach. libsidplayfp 2.16.1 uses a local
CMake adapter and the release's generated 6502 driver; no autotools or xa is needed
unless changing that driver. SID is statically linked on all three platforms.
The combined binary is GPLv3; original MIT/LGPL source licenses are retained.
FluidSynth 2.5.4 uses its C++ OS abstraction and links statically without GLib or
extra audio drivers. GCEM is separately pinned for compile-time tables. SF2 is
enabled; libsndfile/SF3 and DLS are not. Run `--midi-checks <fixtures>` (or add
`--decoder-only`) after generating fixtures for MIDI timing, stereo channel
capture, sustain/chords, SoundFont snapshots, mutes, seeking and live controls.
It also checks long seeks in audio/scopes, instrument names, pedal reconstruction
and restarting after a natural endpoint between sample frames. Optional
`--soundfont <path> --midi <path>` measures read-only local-file load/seek timings.
The fixture generator authors its own sine bank; no user bank is needed for CI.
Development deployment includes Qt's offscreen plugin for headless decoder tests.
Packages include full corresponding application/build sources as well as the
dependency archives. New source files must be tracked before packaging; source
bundling refuses untracked inputs to avoid silently incomplete source archives.

Qt and libgme stay shared in all distributable packages. See
[THIRD-PARTY.md](../THIRD-PARTY.md) and
[library replacement instructions](../packaging/REBUILD-LIBRARIES.md).

## CI and releases

`Build packages` runs on pushes to main, pull requests and manual dispatch. It
produces Windows x64, Linux x64 and macOS arm64 archives and adjacent SHA256 files.
Actions are pinned to commits; the workflow has read-only repository permission
and does not publish releases. Download artifacts from the Actions run for the
exact commit being released. Retain the third-party source/notices inside them.

The Windows job pins an upstream aqtinstall revision that understands Qt 6.11's
separate MinGW/compiler repository folders; released aqtinstall 3.3.0 selects a
nonexistent path for that SDK. Linux explicitly installs the kit's ICU archive.

Before publishing a release:

1. Confirm all selected platform jobs pass and BUILD.txt identifies the intended
   clean source commit. Run full Windows tests and Unix desktop listening checks.
2. Inspect tracked files, commit author metadata and artifacts for private data.
   Never add personal music, preferences or logs to a release.
3. Create the version tag at that commit (currently `v0.10.0`) and a draft release
   using RELEASE-NOTES.md.
4. Attach tested archives and their `.sha256` files. Mark any platform's incomplete
   runtime validation explicitly, or defer its asset. Publish after review.

There is no installer or automatic update service. Future releases follow the
same process; source changes after an initial public import use normal commits.


Munt/libmt32emu 2.8.3 is a static headless C++ library (LGPL-2.1-or-later).
CMake applies hash-checked part capture/mute and per-synth jitter patches before
upstream configuration copies public headers. No additional audio driver or
runtime DLL is needed. ROMs are not build dependencies and are excluded from
packaging. `--mt32-checks <output>` without `--roms` runs resource/selection tests
and explicitly skips synthesis; `--roms <directory>` enables real ROM validation,
part isolation and transport checks. See [MT-32](MT32.md) for commands and limits.
Without `--decoder-only`, resource-row checks also cover Auto/manual engine
presentation and folder selection, missing saved banks, refresh/reselection
guards and source clearing. These checks require no ROMs or SoundFont.

GM/MT-32 suites also pin exact configuration hashes, ordered source/voice labels,
raw titles/models and Unicode filename fallbacks, sample-rounded release tails,
selected/full playlists, resource/roll ownership and invalid-subsong precedence.
Instrument-history checks cover strict pruning, unchanged/equal-frame labels,
independent returned names and reset. Actual MT-32 construction checks require
the same user-provided ROMs as synthesis; no resources are added to Git.
Hosted CI never receives user ROMs. Linux/macOS synthesis needs local validation.

libxmp 4.7.3 is a static C dependency with a hash-checked mixer observation patch.
It uses native loaders, with archive depackers and ProWizard disabled. No new
runtime DLL is needed. See [tracker checks and limits](TRACKERS.md).

TIATracker 1.3 supplies a static TIA sound core, with a GPL-2.0-or-later replay
adapter. No SDL or extra runtime library is needed. Use `--ttt-checks <fixtures>`
for the focused checks; add `--decoder-only` on headless systems.
See [TIATracker support and limits](TIATRACKER.md).

The Windows CI checks artifact includes the matching developer runner, separately
from the release ZIP, for testing the downloaded package on a real desktop.
