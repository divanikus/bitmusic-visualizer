Bit Music Visualizer 0.6.1 - portable Windows x64 build

1. Extract the whole ZIP to a folder on Windows 10 or Windows 11 (64-bit).
2. Run bitmusic_visualizer.exe from that folder.
3. Open a music file with Ctrl+O or drop it onto the player.

No installer, administrator rights, Qt SDK or Python installation is needed.
Keep the EXE, DLLs, platforms folder and qt.conf together. Do not run directly
from inside the ZIP. Music files are not included or modified by the player.

Supported formats: NSF, NSFE, VGM, VGZ, SPC, Game Boy GBS and Spectrum AY (ZXAYEMUL).
Version 0.6.1 adds a saved 30 / 60 / 120 FPS target in Pencil -> Waves... -> Frame rate.
Selection applies immediately; the default is 30. Actual rates depend on the
track, audio device, renderer, system load and monitor. Higher rates use more
CPU/GPU. Trail duration is preserved, including 999 ms at the 120 FPS target.
Every scope card supports resizing in whole grid cells.
Pencil -> Waves...: Smooth auto / Instant auto / Fixed amplitude; Hold and Release
control how quickly a fading note grows back on screen. Trigger offers Stable,
Rising edge and Off. These controls work with both GPU and CPU rendering.
Full mix is unchecked by default in the editor's Channel visibility list. It uses
one grid cell initially and supports resizing, reordering, hiding and its own colors. If it
overflows, move it higher in the list, hide another card or enlarge the grid.
Stereo splits it into L/R lanes for stereo sources; mono stays a single wave.
It includes volume, voice/output mutes and shared effects. This view captures the
PCM sent to the audio device, before the operating system's volume/effects.
Pause freezes the last delivered mix. A paused seek elsewhere shows No output
until playback resumes. Hidden/minimized scopes and a hidden/overflow mix stop
capture. The output view does not need any chip cards to be visible.
Waveform settings are remembered in [Waveforms] in BitMusicVisualizer.ini,
independently of themes. The Full mix checkbox is also remembered. Restore Defaults
in Waves resets only the display controls; grid Reset hides Full mix. Fixed
amplitude makes absolute level/volume comparisons possible; auto modes rescale.
Version 0.4.4 fixes intermittent SPC Preparing flashes at audio-buffer boundaries.
The fix adjusts channel timestamps; sound output and original music are unchanged.
Tracks lists the songs
inside the open file. Channels switches audio voices on/off. The scopes button
shows/hides the separate waveform window; closing that window keeps music playing.
Use the pencil to edit its grid; Apply finishes editing. Closing the main player exits.
Drag a card's right/bottom edge to resize its width/height, or its bottom-right
corner for both. Sizes snap to whole cells. The outline, size badge and sidebar
preview show the proposed layout. Release inside the grid to apply; Escape or
release outside cancels. Red 'Does not fit' leaves the size/order unchanged.
Other cards use the remaining free rectangles; smaller cards may fill gaps while
larger ones overflow. Audio continues; capture masks change only on release.
Reset and opening a new file restore 1x1 cards. Keep grid retains only row/column
counts; subsong changes and hiding/reordering retain sizes. Reducing the grid
clamps card sizes to its bounds. Layout remains session-local.
Both window positions, sizes and maximized state are saved on exit in the same INI
and restored next time. Hidden scopes keep their last geometry; off-screen
coordinates are corrected. Minimized windows reopen normally.
Effects... in the editor controls Fading trail and Line glow independently.
Both are enabled subtly by default; brightness/duration can be adjusted live.
Effects apply to all cards and are saved automatically when their dialog closes.
Both switches, trail duration (60-999 ms) and brightness values (0-100%) are restored
from [Effects] in BitMusicVisualizer.ini beside the EXE, independently of colors.
Pencil -> Renderer... offers Auto, Direct3D 11, OpenGL and CPU (no effects).
Save remembers the choice in [Renderer] Backend in BitMusicVisualizer.ini;
restart the player to apply it. Cancel leaves it unchanged. This dialog remains
available in CPU mode. Auto normally selects Direct3D 11 on Windows.
GPU rendering uses Qt Quick. Seek/file/layout
changes clear the trail; pause freezes it; hidden/minimized scopes stop rendering.
Set BITMUSIC_SOFTWARE_SCOPES=1 before launch for CPU rendering without effects.
If a visible GPU canvas stops completing frames for five seconds, the player
releases it and continues with CPU scopes (effects unavailable until restart).
This recovery is tested with simulated missing frame notifications; the reported
Intel HD 630 dual-monitor freeze is not yet reproduced on the developer machine.

For troubleshooting, close the player and use one of these optional launchers:
- Start-Diagnostics.cmd: normal GPU backend, with a diagnostic log.
- Start-OpenGL.cmd: OpenGL instead of Direct3D, retaining GPU effects, with a log.
- Start-Software.cmd: CPU scopes without trail/glow, with a log.
The switches override the saved renderer for that launch only; normal EXE startup
uses the saved choice. Renderer and effect preferences are not rewritten.
player-diagnostics.log is stored beside the EXE (about 2 MiB maximum); the previous
run is kept as player-diagnostics.log.previous. Logging is opt-in, local only,
and includes Qt/driver messages, screens and audio/scope/frame counters.
No log or music data is uploaded. Copy the log before the next diagnostic launch
if a freeze/crash happens. Diagnostic logs are not included in the ZIP.

Stereo in the editor shows L/R lanes for stereo sources with a shared scale.
Mono sources always show one waveform; the stereo preference is kept for the next file.
L/R buttons beside the time display independently enable the left/right speaker.
Muted stereo lanes stay visible but dimmed. Output choices persist during the session.
Reset returns scopes to a single waveform without changing the speaker switches.
Single-chip FM VGM and SPC use shared channel capture to reduce emulation work.
Master System YM2413 uses nine FM cards plus PSG. FM 7 / Bass drum,
FM 8 / Hi-hat + Snare and FM 9 / Tom + Cymbal share their hardware/mute controls.
A rhythm card shows the sum of its percussion voices. Other VGM chips still need support.
GBS has four independent scopes/mutes: Square 1, Square 2, Wave and Noise,
including the track's L/R routing. Unknown durations never trigger automatic stopping.
Spectrum AY exposes AY A, AY B, AY C and Beeper. This backend's output is mono;
unused channels remain silent and can be hidden with the editor. One AY file can
include both AY-chip and 48K beeper subsongs. PT3, VTX and YM are separate formats.
Each card's pipette opens its colors, including Axis. The pipette beside Reset/Apply sets the window
background. Both pickers accept HEX. Changes preview live; Cancel restores them.
Theme selects colors immediately. Save theme... asks only for a theme name and
saves it automatically in themes/. Existing names show Replace instead of Save;
see themes/README.txt for the format. Themes contain 32 tile palettes, a separate
Full mix palette and window background, and survive music file changes.
Apply to all includes unused tiles and Full mix. Older themes remain compatible.
Grid Reset keeps colors; select Default to restore them. Save custom colors before
selecting another theme or exiting. The last selected/saved theme is restored at
startup, including the window background. Missing or invalid files use Default.
The selection is stored in BitMusicVisualizer.ini beside the EXE as a filename,
resolved in this copy's themes/ folder. Keep the INI and themes when moving/updating
the app. No profile-directory or registry fallback is used. If this folder cannot
be written, the app reports it and keeps the chosen colors for this session only.
Unsaved edits are not automatically written to a theme.

Prototype limitations: some VGM chips are unsupported; Mega Drive PSG is a combined
group. Long seeks/new scope preparation can take time. Layout is session-local.
This build is unsigned. Its source/build identification is in BUILD.txt.

This application uses Qt 6.11.2 under LGPLv3 and Game Music Emu 0.6.5 under LGPLv2.1
or later. Their DLLs can be replaced with compatible modified builds; reverse
engineering for debugging modifications to these libraries is not restricted.
Licenses, copyright notices and compiler runtime notices are in licenses/.
Complete corresponding Qt/libgme source archives and rebuild information are in
third-party-sources/. Keep these materials with the bundle when passing it on.
The application's own code is under the MIT license; see LICENSE.
Local libgme modifications remain LGPL-2.1-or-later.

The application sources are maintained at:
https://github.com/divanikus/bitmusic-visualizer
