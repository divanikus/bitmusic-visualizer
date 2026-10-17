Bit Music Visualizer 0.10.0

Linux x64: make the AppImage executable, then run it.
Build baseline: Ubuntu 22.04 / glibc 2.35; requires system X11/XWayland,
OpenGL/EGL drivers, PulseAudio (or PipeWire's PulseAudio compatibility) and the
standard C/C++ runtimes. No root access or installation is required.
If FUSE is unavailable, use ./BitMusicVisualizer-0.10.0-linux-x64.AppImage --appimage-extract-and-run
To inspect licenses/sources or replace libraries, use --appimage-extract, then
run squashfs-root/AppRun. The image includes Qt/libgme/ICU and an AppImage runtime;
it does not bundle the operating system or graphics drivers.

macOS Apple Silicon: extract the ZIP and open BitMusicVisualizer.app.
This build has only an ad-hoc signature; it is not Apple-notarized. If Gatekeeper
blocks it, follow Apple's Privacy & Security instructions for a trusted app.
Do not disable Gatekeeper globally. macOS 13 or newer is the build target.

Ctrl+O opens a file. The scopes button toggles the waveform window.
The platform and sound configuration appear below the track title.
Hover over the title to see the full title and filename.
Pencil enters layout/theme editing. See docs/VISUALIZATION.md in the source repo.
The Preset row saves grid/card/stereo layouts; the star sets or clears the default
for this chip configuration. Related presets can be adapted manually.
F1 or the main player's keyboard icon opens the shortcut guide. On macOS, use
Command for Ctrl and Option for Alt; the guide displays native key labels.
--software-scopes disables GPU rendering; --diagnostics enables bounded local logs.
There is no telemetry or upload. Review diagnostic logs for paths before sharing.

Linux settings: ~/.config/BitMusicVisualizer/BitMusicVisualizer.ini
Linux themes: ~/.local/share/BitMusicVisualizer/themes
(XDG_CONFIG_HOME and XDG_DATA_HOME override these roots.)
macOS settings: ~/Library/Preferences/org.bitmusicvisualizer.BitMusicVisualizer.plist
macOS themes: ~/Library/Application Support/BitMusicVisualizer/themes
Theme format is described in THEMES.txt.
Layout presets use a presets directory alongside themes under the same data root.

Original application code is MIT. SID-enabled combined builds are GPLv3 (COPYING).
Qt is used under LGPLv3, libgme under LGPLv2.1 or later, and libsidplayfp under GPLv2
or later. The full application/build source and dependency archives are included
in third-party-sources/; keep COPYING with the bundle.
Libraries are replaceable with compatible modified builds. Reverse engineering
for debugging modifications to these libraries is not restricted.
Keep LICENSE, licenses/ and third-party-sources/ with the bundle when sharing it.
BUILD.txt identifies the source version. Music is not included.

https://github.com/divanikus/bitmusic-visualizer
