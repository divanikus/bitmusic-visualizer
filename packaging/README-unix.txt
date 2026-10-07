Bit Music Visualizer 0.6.0

Linux x64: make the AppImage executable, then run it.
Build baseline: Ubuntu 22.04 / glibc 2.35; requires system X11/XWayland,
OpenGL/EGL drivers, PulseAudio (or PipeWire's PulseAudio compatibility) and the
standard C/C++ runtimes. No root access or installation is required.
If FUSE is unavailable, use ./BitMusicVisualizer-0.6.0-linux-x64.AppImage --appimage-extract-and-run
To inspect licenses/sources or replace libraries, use --appimage-extract, then
run squashfs-root/AppRun. The image includes Qt/libgme/ICU and an AppImage runtime;
it does not bundle the operating system or graphics drivers.

macOS Apple Silicon: extract the ZIP and open BitMusicVisualizer.app.
This build has only an ad-hoc signature; it is not Apple-notarized. If Gatekeeper
blocks it, follow Apple's Privacy & Security instructions for a trusted app.
Do not disable Gatekeeper globally. macOS 13 or newer is the build target.

Ctrl+O opens a file. The scopes button toggles the waveform window.
Pencil enters layout/theme editing. See docs/VISUALIZATION.md in the source repo.
--software-scopes disables GPU rendering; --diagnostics enables bounded local logs.
There is no telemetry or upload. Review diagnostic logs for paths before sharing.

Linux settings: ~/.config/BitMusicVisualizer/BitMusicVisualizer.ini
Linux themes: ~/.local/share/BitMusicVisualizer/themes
(XDG_CONFIG_HOME and XDG_DATA_HOME override these roots.)
macOS settings: ~/Library/Preferences/org.bitmusicvisualizer.BitMusicVisualizer.plist
macOS themes: ~/Library/Application Support/BitMusicVisualizer/themes
Theme format is described in THEMES.txt.

Application code is MIT. Qt is used under LGPLv3 and libgme under LGPLv2.1 or later.
Libraries are replaceable with compatible modified builds. Reverse engineering
for debugging modifications to these libraries is not restricted.
Keep LICENSE, licenses/ and third-party-sources/ with the bundle when sharing it.
BUILD.txt identifies the source version. Music is not included.

https://github.com/divanikus/bitmusic-visualizer
