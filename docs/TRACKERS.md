# Tracker music

Open a module with Ctrl+O or drag it onto the player. No ROM or SoundFont is
needed for a self-contained module: its samples are inside the file.
libxmp 4.7.3 detects the contents, including Amiga filenames such as `mod.song`.

| Family | Files | Playback |
| --- | --- | --- |
| Soundtracker / Noisetracker / ProTracker | MOD, M15 (15 or 31 samples) | Amiga/Paula mixer for recognized Amiga MODs, including filter commands |
| MED / OctaMED | MED, MMD0–MMD3 | Native tracker replay |
| DigiBooster Pro | DBM | Native tracker replay |
| FastTracker / Scream Tracker / Impulse Tracker | XM, S3M, IT, multichannel MOD | Native tracker replay |

Other native libxmp formats can be tried with **All files**. This does not imply
that every Amiga game format is a tracker module. Custom executable replayers,
external instrument files and packed archives are outside this first integration.
Unpack ZIP/LHA/ICE/PowerPacker files first. Gzip input uses the player's existing
bounded decompressor. ProWizard packed-module conversion is disabled.

## Cards and transport

- Each card represents a tracker channel, not one instrument or a hardware chip.
  Its title keeps the channel name, followed by the current sample/instrument:
  `Channel 1 · Bass`. Unnamed samples get a number.
- Channels without notes or instrument events anywhere in the module start
  unchecked in Cards. This scans all patterns, including late entrances;
  instrument-only triggers are kept conservatively. Audio is unchanged, and
  cards can be restored manually or by a saved preset.
- Waveform, spectrum, stereo display, meters, Full mix, mute/Solo and existing
  keyboard shortcuts work as usual. IT background voices belong to their
  originating channel, rather than creating extra cards.
- Recognized Amiga MODs use the original left/right/right/left layout. Native
  libxmp panning retains a small 1/256 contribution on the opposite side at far right.
- Duration and subsongs are calculated from the pattern/order sequence.
  The Tracks list contains subsongs, not patterns. Repeat follows the player toggle.
- Seeking replays from the start when rewinding to restore effects, sample
  positions and filter state deterministically. Very long/complex seeks can take
  longer; they are cancellable. No approximate order-only seek is used.
- Keyboards use the existing experimental signal estimator. A sample may be a
  drum, speech or a chord, so a pattern note is not necessarily its audible pitch.
- **Pattern** displays actual note/instrument rows and optional volume/effects,
  centered on the audible playback row. Use Full mix for multiple columns,
  resize it and select First channel when needed. Commands use libxmp's
  normalized representation, not necessarily the original tracker's notation.
  This optional view is omitted above 2,097,152 cells. See the
  [visualization guide](VISUALIZATION.md#piano-roll-and-pattern).
- Presets retain stable channel numbers. Layouts can be adapted between tracker
  configurations using the existing preset selector.

Signal cards capture per-voice PCM before final clipping; Full mix is the audible sum.
Per-voice panning, filters and ramps remain in the cards. A detached anticlick
remainder without an owning voice belongs only to Full mix.
Pattern instead shows events, retaining muted columns in a dimmed state.

## Development checks

Generate the original fixtures with `fixtures.py`, then run:

```sh
bitmusic_checks --tracker-checks test-output --decoder-only
```

Omit `--decoder-only` to exercise Windows audio, live scopes, seek, repeat and
Solo. Additional local module paths after the options compare plain/tapped
decoding and rewind on other formats; they are not copied or distributed.

Fixtures cover 15/31-sample MOD, eight channels, two subsongs, independent stereo
signals, master reconstruction, unchanged capture PCM, mute/Solo and cancellation.
The dependency's local MED/MMD3, DBM, XM, S3M and IT fixtures provide additional
format checks. Linux/macOS retain their experimental status until their new
builds and physical playback are checked.
