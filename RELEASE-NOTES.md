# Bit Music Visualizer 0.6.0

First public source snapshot of the native chiptune player and oscilloscope.

- NSF/NSFE, VGM/VGZ, SPC, GBS and ZXAYEMUL AY playback.
- Compact player, subsongs, seeks, repeat, voice mutes and L/R output controls.
- Mono/stereo voice cards and an optional final Full mix waveform.
- Editable grid with hiding, reordering and resizing across rows and columns.
- INI color themes, smooth/instant/fixed amplitude and configurable triggering.
- GPU glow/trails, renderer selection and CPU fallback.
- Saved theme, display settings and window geometry; platform-specific storage.

Archives include Qt/libgme libraries, licenses and corresponding source.
Music is not included. Windows EXE is unsigned; macOS uses an ad-hoc signature
and is not notarized. Linux uses a single AppImage, with an Ubuntu 22.04 / glibc
2.35 build baseline. It includes Qt/libgme/ICU; system graphics/X11/audio/C++
libraries remain required. Without FUSE, use --appimage-extract-and-run.

Known limits: not all VGM chips are supported; Mega Drive PSG is a combined group;
SPC voice cards exclude shared echo (included in Full mix); cold seeks can take
time; repeat is not seamless. Some GPU/driver combinations may need CPU rendering.

Windows validation: the relocated portable archive passes the full native,
channel-tap and GPU suites with SDK paths removed. Playback/seek/repeat checks
reported zero empty PCM callbacks and zero output errors on the test workstation.
The separate headless decoder mode also passes.

Linux and macOS desktop validation is pending. Before publishing their assets,
record CI results and real audio/GPU checks. A successful compilation or headless
decoder run alone does not validate desktop audio or GPU behavior.
BUILD.txt inside each archive identifies its exact source commit.
