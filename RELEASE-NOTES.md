# Bit Music Visualizer 0.6.1

Choose how often the oscilloscopes update: **30, 60 or 120 FPS**, under
**pencil → Waves… → Frame rate**. The setting applies immediately and is
remembered across restarts, independently of color themes. The default is 30.

- Coordinates channel snapshot preparation and scope presentation, including
  GPU trail fading, without restarting playback.
- Preserves time-based trail duration up to 999 ms at the 120 FPS target.
- Suspends scope capture and the presentation timer while hidden or minimized.
- Adds a README screenshot and public changelog.

These are target rates. Actual refresh and fresh-wave rates depend on the track,
audio device, CPU/GPU load, renderer and display. Higher targets use more resources;
they do not change the sound. Existing settings and themes remain compatible.

## Downloads

- **Windows x64:** extract the complete portable ZIP and run the EXE.
- **Linux x64 (experimental):** AppImage, Ubuntu 22.04 / glibc 2.35 baseline.
  Make it executable; without FUSE use `--appimage-extract-and-run`.
- **macOS Apple Silicon (experimental):** extract the ZIP and open the app.
  Requires macOS 13 or newer; ad-hoc signed, not notarized.

Packages include Qt/libgme libraries, licenses and corresponding source.
Music is not included. SHA256 files are provided for each download.
Linux still needs system graphics/X11/audio/C++ libraries. The Windows EXE
is unsigned. See [the README](https://github.com/divanikus/bitmusic-visualizer#run)
for platform launch details.

Windows checks cover live 30/60/120 FPS changes with NSF/VGZ/SPC, saved preferences,
seek/reload, CPU and GPU rendering, hidden-window suspension and trail lifetime.
Linux/macOS builds and relocated decoder checks do not replace real desktop,
audio and GPU validation; those platforms remain experimental.

Existing limits include combined Mega Drive PSG, dry SPC voice cards (shared
echo is in Full mix), cold-seek preparation and non-seamless repeat. Some GPU
drivers may require CPU rendering. See the
[changelog](https://github.com/divanikus/bitmusic-visualizer/blob/v0.6.1/CHANGELOG.md).
BUILD.txt inside each package identifies its exact source commit.
