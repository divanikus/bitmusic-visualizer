# Bit Music Visualizer

A compact chiptune player with a separate, customizable oscilloscope window.
Explore the voices inside retro game music, mute channels, and compare them
with the final audio mix.

![Bit Music Visualizer playing a Sega Genesis track with stereo channel oscilloscopes and the full mix.](docs/images/player-and-oscilloscopes.png)

Built with C++17, Qt 6, Game Music Emu, libsidplayfp and ymfm. Music is not included.

See the [changelog](CHANGELOG.md) for version history.

## Features

- Play, pause, stop, seek, repeat, and select subsongs within a music file.
- Independent voice mutes, temporary Solo and left/right output switches.
- Platform and sound configuration appear together below the track title;
  hover over the title to see the filename.
- Mono or stereo waveforms; optional Full mix showing the PCM sent to the audio
  device, including active voices, volume and shared effects.
- Editable grid: hide, reorder and resize cards in whole grid cells.
- Named layout presets with optional defaults per sound configuration and transfer
  between related configurations, such as NES and NES + VRC6.
- Waveform, frequency spectrum and keyboard cards; add multiple views of a source.
  Keyboard can highlight several notes at once. NES pulse/triangle, Sega PSG,
  Game Boy Square/Wave and AY A/B/C use chip pitches; other voices and Full mix
  use experimental single-pitch estimates.
- Saved 30 / 60 / 120 FPS target, selectable in the scope editor's Waves dialog.
- Per-card colors, HEX input, named INI themes and a configurable window background.
- Smooth/instant/fixed amplitude, stable/rising/off trigger, GPU glow and fading
  trails. CPU rendering is available as a fallback.
- Remembered theme, waveform/effect/renderer preferences and window geometry.

## Formats

| System | Files | Visualization boundary |
| --- | --- | --- |
| NES / Famicom | NSF, NSFE | Base voices and backend-supported expansion audio |
| Mega Drive / Genesis | VGM, VGZ | FM and DAC voices; PSG is one combined group |
| Master System | VGM, VGZ | PSG and YM2413 FM; rhythm voices share FM 7–9 |
| SNES | SPC | Eight dry DSP voices; shared echo appears in Full mix |
| Game Boy | GBS | Square 1, Square 2, Wave and Noise, with L/R routing |
| ZX Spectrum | AY (ZXAYEMUL) | AY A/B/C and beeper; mono output |
| Commodore 64 | SID (PSID / RSID, up to three chips) | Three pre-filter voices per chip; filter and volume-register samples in Full mix |
| Atari 400/800 / 5200 | VGM, VGZ with one POKEY | Four mono hardware voices, mute/Solo and Full mix |
| Yamaha FM/sample systems | VGM, VGZ with YM2203, YM2151, YM2608, YM2610/2610B, YM3526, YM3812, Y8950 or YMF262 | YM2203: three FM and three PSG voices; other chips: one mix card per chip. Supported chips can be combined or doubled |

VGM is a container: accepting it does **not** mean every chip is supported. PT3,
VTX, YM, Amiga/tracker modules and MIDI are not supported yet. There are no
invented durations or titles: files without duration data keep playing until
stopped. Cold seeks can require emulation from an earlier point. Voice traces
are not guaranteed to be exact separately audible stems after shared effects.

Unsupported declared VGM chips are reported by name instead of silently skipped.
YM2203 has separate **FM 1–3** and **PSG A/B/C** cards with individual mute/Solo.
The other new Yamaha cards are labeled **mix**: they contain the whole chip,
including the SSG and sample sections where present. Mute/Solo controls that chip.
YM2151, YM2608, YM2610/2610B and YMF262 support stereo; YM2203, Y8950 and ordinary
OPL/OPL2 are mono. Mixing these new chips with the existing Sega/POKEY paths is
not supported yet. This does not add MDX or other non-VGM formats.

YM2608 rhythm playback needs your **roms/ymfm/ym2608_adpcm_rom.bin** (8192 bytes).
It is requested only when the track uses those rhythm samples. FM/SSG playback
and sample data embedded in VGM do not need it. No ROM is bundled or downloaded.
See [ROM locations](packaging/YAMAHA-ROMS.txt) and [VGM limits](docs/VGM.md).

POKEY uses GME's SAP sound core: ordinary tones, polynomial noise, volume-only
audio, joined dividers and high-pass effects. One POKEY is supported; dual POKEY,
mixed POKEY/other chips and special SKCTL/serial modes are rejected. SAP files are
not yet accepted. These Atari VGM files need no ROM. See [VGM boundaries](docs/VGM.md).

SID uses the file's 6581/8580 and PAL/NTSC flags, with 6581/PAL defaults when
unspecified. Each chip exposes three hardware voices, with subsongs, seeking,
mute and Solo. Voice cards show the envelope-modulated signal before the common
filter, volume control and output circuitry. Full mix includes these shared
stages and volume-register digi samples; the latter have no separate voice card
and can remain audible with all three voices muted. SID keyboard uses the
experimental waveform estimate. A single SID stays mono; two or three chips use
six or nine cards and a fixed left/right or left/center/right pan with crossfeed.
This is a playback convention, not panning metadata in the file.

Optional C64 ROMs go in **roms/c64/**: `kernal.rom` (8192 bytes), `basic.rom`
(8192 bytes), `chargen.rom` (4096 bytes). Windows uses the folder beside the EXE;
Linux/macOS use the application data directory alongside themes. No setup dialog
is needed. A missing or invalid image is reported only when the loaded tune
needs it; the warning shows the exact path. Add the file and reopen the tune.
Ordinary PSID and self-contained RSID run without ROMs. No C64 ROM or song-length
database is included. See [ROM locations](packaging/ROMS.txt).
See [SID implementation boundaries](docs/SID.md).

## Run

See [Releases](https://github.com/divanikus/bitmusic-visualizer/releases).
Extract the complete archive and keep its libraries, licenses and source bundle.

- **Windows x64:** run `bitmusic_visualizer.exe`. No installation or Qt SDK is needed.
- **Linux x64:** download the AppImage, allow execution (`chmod +x *.AppImage`),
  then run it. It bundles Qt, libgme and ICU; system graphics, X11 and audio libraries
  remain required. The build baseline is Ubuntu 22.04 / glibc 2.35, with a Fedora
  compatibility check. Use `--appimage-extract-and-run` if FUSE is unavailable.
  See [Linux requirements](docs/BUILDING.md). XWayland is needed on Wayland desktops.
- **macOS Apple Silicon:** open `BitMusicVisualizer.app`. Builds are not notarized
  or signed with an Apple Developer ID. Permit the application in Privacy & Security
  only if you trust its source. Intel Mac binaries are not currently provided.

Windows is locally tested. Linux/macOS packaging and CI are new; check a release's
notes for its actual platform validation, rather than treating a successful
compilation as a listening or GPU compatibility test.

Open a file with Ctrl+O or drag it onto the player. Tracks lists subsongs within
that file; double-click to play one. Channels controls sound. The scopes button
shows/hides the separate window; closing that window keeps playback running.

**Shift-click a channel button** in Channels to listen to it alone. A **SOLO**
badge marks the selected voice. Shift-click another channel to switch Solo;
repeat on the same one to restore the previous mute selection. Ordinary mute
controls leave Solo and edit the currently audible set. Solo survives transport
and subsong changes, but resets when opening a different file and is not saved.

In the scope editor, drag cards to reorder and drag their edges/corners to resize.
Apply finishes editing. Reset restores the grid and visibility, keeping colors.
Keep grid retains only row/column counts when opening a new file without an
assigned preset. Full mix starts hidden and can be enabled in Cards.
Theme selection applies immediately; Save theme asks for a name.
Click the window-size icon next to the background eyedropper to reveal
**width × height** fields beside it in screen pixels. Changes resize the whole window immediately;
click the icon again to collapse the fields. The size is saved on exit.

Select a card in the editor's **Cards** list, then change **View** to Waveform,
Spectrum or Keyboard. **Add card…** creates another view of any voice or Full mix,
so a waveform and its keyboard can sit next to each other. Added cards use the same
grid, resizing and source colors. Reset or opening a new file restores the original
waveform cards unless a preset is assigned to the new file's sound configuration.

The editor sidebar's **Preset** selector applies a saved layout immediately.
The **disk icon** saves under a name; **★** assigns it as the default for this
sound configuration. Click the star again to clear that default. Configurations
without an assigned preset keep the previous file-opening behavior, including
Keep grid. Changing subsongs preserves current edits.

Presets save the grid, card sources/views/order/sizes/visibility, spectrum options,
labels and stereo display. A `*` marks unsaved edits; saving is explicit. The list
also shows presets from the same platform family marked **(adapt)**. Shared
hardware channels keep their cards, missing channels are skipped and new channels
start as waveforms. Save the adapted layout under another name to keep it or
assign it as a default. See [preset details](docs/VISUALIZATION.md#layout-presets).

See [waveform controls](docs/VISUALIZATION.md) and [theme format](packaging/THEMES.txt).

## Keyboard shortcuts

Click the keyboard icon in the main player's title bar, or press **F1** in either
window, to open the shortcut guide.

| Key | Action |
| --- | --- |
| 1–9 | Cycle that channel's cards: Waveform → Spectrum → Keyboard |
| Ctrl+1–9 | Mute / unmute that channel |
| Ctrl+Shift+1–9 | Solo that channel / restore previous mutes |
| Alt+1–9 | Hide / show that channel's cards |
| 0 / Alt+0 | Full mix: cycle view / hide or show |
| Ctrl+← / Ctrl+→ | Toggle left / right audio output |
| F2 / F3 / F4 | Set all cards to Waveform / Spectrum / Keyboard |
| F5 | Play the current track from the beginning |
| F7 / F8 | Resume / pause |
| Ctrl+Shift+R | Restore default cards/layout and enable all voices and L/R outputs |

These work in either player window, with the number row or numpad (Num Lock on).
On macOS, use Command for Ctrl and Option for Alt; the in-app guide uses native key labels.
Numbers follow original channel numbers, even after reordering; duplicate cards
of a channel change together. Shortcuts leave text/number fields, dropdowns and
dialogs alone. Reset keeps colors, effects, volume and window size; Full mix
returns to hidden. View/layout edits remain session-local until saved as a preset.

## Settings and diagnostics

| Platform | Settings | Themes |
| --- | --- | --- |
| Windows | `BitMusicVisualizer.ini` beside the EXE | `themes/` beside the EXE |
| Linux | `$XDG_CONFIG_HOME/BitMusicVisualizer/BitMusicVisualizer.ini` | `$XDG_DATA_HOME/BitMusicVisualizer/themes/` |
| macOS | `~/Library/Preferences/org.bitmusicvisualizer.BitMusicVisualizer.plist` | `~/Library/Application Support/BitMusicVisualizer/themes/` |

Linux defaults are `~/.config` and `~/.local/share`. Missing/corrupt saved themes
fall back to Default. Unsaved color edits are not written automatically.
Layout presets are INI files in **presets/** alongside the themes directory.
Missing, corrupt or incompatible default presets fall back to ordinary layout
initialization. Default assignments are stored in the application's settings.

`--software-scopes` disables GPU effects for that launch. `--diagnostics` enables
bounded local logs (beside the EXE on Windows, in the app's data directory elsewhere).
Logs can contain device information and file paths; review them before sharing.
There is no telemetry, automatic upload or updater. A GPU timeout falls back to
CPU scopes; driver-specific failures may still require restarting the player.

## Build and license

[Build/test instructions](docs/BUILDING.md) · [Third-party notices](THIRD-PARTY.md)

Application code, original artwork and authored fixture generators are under
the [MIT license](LICENSE). The libgme modifications in `native/gme-taps/` are
**LGPL-2.1-or-later**, including patch content applied by `cmake/PatchGme.cmake`.
The ymfm-derived voice capture in `native/ymfm-voices/` is **BSD-3-Clause**.
Qt, libgme, zlib and compiler runtimes keep their own licenses. Release packages
dynamically link Qt and libgme and include their corresponding source and notices.

The SID engine and `native/sid-taps/` are **GPL-2.0-or-later**. The combined
SID-enabled application is distributed under **GPLv3**; see [COPYING](COPYING).
This does not remove the MIT license from our original source files. Packages
also include the complete corresponding application source and build scripts.
