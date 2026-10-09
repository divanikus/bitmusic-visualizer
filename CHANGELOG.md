# Changelog

Notable changes to Bit Music Visualizer, newest first.

Version 0.6.0 is the first public GitHub release. Earlier entries describe
pre-public development milestones; their dates mark implementation checkpoints,
not published GitHub releases. Unreleased lists changes made after the latest tag.

## Unreleased

### Added

- Window size controls in the scope editor: enter width and height in screen
  pixels, with the size remembered on exit.

## [0.7.2](https://github.com/divanikus/bitmusic-visualizer/releases/tag/v0.7.2) — 2026-10-09

### Added

- Chip-derived keyboard notes for Game Boy GBS Square/Wave and AY A/B/C channels.

## [0.7.1](https://github.com/divanikus/bitmusic-visualizer/releases/tag/v0.7.1) — 2026-10-09

### Added

- Keyboard cards can highlight multiple notes at once, with names and frequencies
  for each note in the chord readout.
- Chip-derived Sega PSG notes in VGM/VGZ, including all three tones in combined
  PSG cards alongside FM channels.

## [0.7.0](https://github.com/divanikus/bitmusic-visualizer/releases/tag/v0.7.0) — 2026-10-09

### Added

- Frequency spectrum cards for individual voices and Full mix, with optional L/R lanes.
- Per-card Line / Bars spectrum styles and an optional grid, under View in the editor.
- Keyboard cards with NES pulse/triangle chip notes and experimental pitch
  estimation for other voices and Full mix, marked Estimated.
- A View selector and Add card dialog in the existing grid editor: compare multiple
  views of one channel without opening extra windows.
- A scrollable card editor for smaller windows.

## [0.6.1](https://github.com/divanikus/bitmusic-visualizer/releases/tag/v0.6.1) — 2026-10-08

### Added

- Saved 30 / 60 / 120 FPS choice in the Waveforms dialog, applied immediately to
  scope capture and presentation. Hidden/minimized scopes suspend their timer;
  trails retain their selected duration up to 999 ms at 120 FPS.
- Player and oscilloscope screenshot in the README.
- This version history, linked from the README.

## [0.6.0](https://github.com/divanikus/bitmusic-visualizer/releases/tag/v0.6.0) — 2026-10-07

### Added

- Resizable oscilloscope cards spanning multiple grid rows and columns, including
  Full mix. Drag an edge or corner in edit mode; an animated preview shows placement.
- Windows x64 portable ZIP, Linux x64 AppImage and macOS Apple Silicon app ZIP,
  with adjacent SHA256 checksums and manifests of packaged files.
- Linux AppImage builds based on Ubuntu 22.04 / glibc 2.35, with bundled Qt,
  libgme and ICU, plus a launch option for systems without FUSE.
- Automated platform builds and relocated decoder checks, including AppImage
  execution on Ubuntu and Fedora without FUSE.
- Public source under the MIT license for application code, with LGPL libgme
  changes kept under their own license. Packages include dependency notices,
  corresponding library sources and rebuild instructions.

### Changed

- Card reordering, overflow and grid reset now account for card spans. Reset and
  file changes restore one-cell cards; Keep grid preserves grid dimensions only.

### Platform status

- Windows has local playback and GPU validation. Linux and macOS downloads are
  experimental: automated checks pass, but desktop audio, GPU and settings behavior
  still need real-device testing. macOS builds are ad-hoc signed and not notarized.

## 0.5.1 — 2026-10-03

- Made Full mix an ordinary, initially hidden card in the channel visibility list,
  with reordering, independent colors and the shared Stereo option.
- Added a FullMix theme group while retaining compatibility with earlier themes.
- Clarified waveform controls with descriptions of amplitude modes, trigger
  alignment, Hold and Release.

## 0.5.0 — 2026-10-02

- Added Smooth auto, Instant auto and Fixed waveform amplitude modes, with
  configurable Hold and Release for smooth scaling.
- Added Stable, Rising edge and Off trigger modes, with shared alignment for
  stereo lanes. CPU and GPU renderers use the same presentation logic.
- Added an optional final stereo output view, captured after voice mutes,
  volume, L/R output controls and shared effects.
- Added saved waveform preferences and Restore Defaults.
- Derived portable package version information from the application version.

## 0.4.4 — 2026-10-02

- Fixed intermittent Preparing messages during normal SPC playback and at certain
  seek positions by accounting for the decoder's initial output padding in
  waveform timestamps.

## 0.4.3 — 2026-10-02

- Added a saved renderer selector: Auto, Direct3D 11 on Windows, OpenGL and CPU.
  Renderer changes take effect on the next launch.

## 0.4.2 — 2026-10-02

- Added stalled-render detection and automatic fallback to CPU scopes.
- Hardened GPU resource cleanup and resizing during renderer recovery.
- Added opt-in diagnostics and Windows launchers for testing alternate renderers.

These changes add recovery paths; they do not establish that all GPU or
multi-monitor driver failures are resolved.

## 0.4.1 — 2026-10-02

- Saved effect settings independently of color themes.
- Remembered the positions and sizes of both windows.
- Expanded trail duration to 999 ms and glow strength to 100%.
- Added platform-specific preference locations for Linux and macOS, while
  retaining portable settings beside the executable on Windows.

## 0.4.0 — 2026-10-02

- Added GPU waveform rendering through Qt Quick, with fading trails and line glow.
- Added an Effects dialog with live controls.
- Retained CPU rendering as a fallback and suspended rendering while scopes are
  hidden or minimized.

## 0.3.5 — 2026-09-28

- Fixed the multi-second delay when closing the main player window by correcting
  scope-worker shutdown and cleanup.

## 0.3.4 — 2026-09-28

- Added ZXAYEMUL AY playback for Spectrum AY and beeper music, with subsongs,
  transport controls, independent scopes and channel mutes.
- Moved Windows settings beside the executable, keeping theme selection portable.

## 0.3.3 — 2026-09-28

- Added Game Boy GBS playback with Square 1, Square 2, Wave and Noise scopes,
  independent mutes and stereo routing.
- Restored the last selected theme at startup, including the window background;
  missing or invalid themes fall back to Default.

## 0.3.2 — 2026-09-28

- Grouped YM2413 percussion with its shared FM hardware channels: FM 7 / Bass drum,
  FM 8 / Hi-hat + Snare, and FM 9 / Tom + Cymbal.
- Matched scope labels and mute controls to those groups instead of presenting
  melodic and percussion modes as independent hardware voices.

## 0.3.1 — 2026-09-28

- Kept mono sources in a single waveform lane regardless of the Stereo setting.
- Added independent left/right audio output switches, with disabled lanes dimmed
  in stereo scopes.

## 0.3.0 — 2026-09-28

- Added optional stereo channel scopes.
- Replaced repeated per-voice emulation with shared channel capture for supported
  single-chip YM2612/YM2413 VGM/VGZ files and SPC, reducing scope processing cost.
  Other backends retain their existing capture path.
- Restored Master System YM2413 FM playback, including rhythm mode.
- Kept audio decoding independent of waveform capture and GUI rendering.

## 0.2.6 — 2026-09-23

- Simplified Save theme to a name-only dialog, with automatic INI storage in the
  application's theme directory and explicit replacement of existing themes.

## 0.2.5 — 2026-09-23

- Added named INI themes with 32 channel palettes and a window background color.
- Applied theme selection immediately from the dropdown and retained colors when
  opening another file or resetting the layout.
- Made Apply to all update every palette, including channels not currently shown.
- Fixed unreadable dropdowns when the operating system uses a light theme.

## 0.2.4 — 2026-09-23

- Added independent axis color for each oscilloscope card.

## 0.2.3 — 2026-09-23

- Added pipette buttons to card headers in edit mode, opening color settings for
  the selected card.
- Added a separate window-background picker in the editor toolbar.

## 0.2.2 — 2026-09-23

- Added per-channel waveform, label, border and card-background colors, with a
  color picker, HEX input, reset and Apply to all.

## 0.2.1 — 2026-09-23

- Fixed lost VGM/VGZ channel isolation after seeking or repeating a track.
- Improved waveform recovery after seeks and repeat, including long Preparing
  delays with VGZ and SPC.

## 0.2.0 — 2026-09-23

- Introduced the native C++/Qt player with independent audio processing, channel
  mutes and a separate oscilloscope window.
- Added a compact English interface with previous/next, stop, play/pause, repeat,
  volume, seeking and a full subsong list with available metadata.
- Kept tracks without duration metadata playing until stopped.
- Added custom window controls and an application icon. Closing the scope window
  hides it without stopping playback; hidden scopes suspend waveform work.
- Added an editable grid, independent channel number/name visibility, hidden
  cards, animated reordering, Apply, Reset and Keep grid.
- Kept existing waveforms live while channel visibility changes prepare new scopes.
- Added a portable Windows ZIP with replaceable runtime libraries and their
  source and license materials.

## Initial prototype — 2026-09-22

- Created a Python playback and per-channel oscilloscope prototype for NES NSF,
  Mega Drive VGM/VGZ and SNES SPC, with basic transport and seeking.

The Python prototype is historical and is not part of the current native application.
