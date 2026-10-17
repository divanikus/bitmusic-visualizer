# General MIDI

The MIDI row offers Auto, General MIDI and MT-32. Auto recognizes Roland MT-32
SysEx; see [MT-32 playback](MT32.md) for ROMs, parts and its limitations.

Open a Standard MIDI file (.mid or .midi, formats 0, 1 or 2) with a user-provided
uncompressed SF2 SoundFont. The SoundFont row appears only for MIDI in the main
player. Selection is saved and changing the bank restarts the current song.
Selecting the current bank or MIDI engine again preserves playback and position.
The folder button opens the resource directory; Refresh rereads the list and
clears cached bank input for the next load, without interrupting playback.
See [platform locations](../packaging/SOUNDFONTS.txt). No bank is downloaded or
included at runtime. **Browse...** in the SoundFont dropdown selects a bank or list
from any folder, without copying it. Local selections are stored by filename;
external selections by absolute path. With no saved choice, the first available SF2 or list alphabetically
is selected; a missing saved bank produces an error on MIDI load.

Formats 0/1 play their tracks together as one song. Format 2 lists musical
patterns as separate subsongs, with their own titles and channel usage. Missing
titles use Pattern 1, Pattern 2, etc. Tracks without positive-velocity Note On
events are treated as setup: they stay out of the playlist, while their final
tempo, controllers, bank/program choices, pitch/pressure and SysEx initialize
following musical patterns. Later setup tracks update that initialization;
musical patterns can override it with their own events. Setup messages retain
their order but run at time zero, without the setup track's silent duration.
Note releases and setup titles are not inherited. A file containing only setup
reports that it has no notes.

This is a compatibility policy for setup-plus-pattern files, not simultaneous
track merging or a claim that SMF 2 requires shared state. Changes inside a
musical pattern stay local to that pattern. Without an own or setup tempo,
120 BPM applies; SMPTE timing remains fixed. Direct subsong selection and seeking
restore the same setup, including GM/MT-32 detection from inherited SysEx.
Sixteen stable cards represent MIDI channels; channel 10 is percussion.
On file load, channels with no positive-velocity Note On anywhere in the song
start unchecked in the card editor. Late entrances remain visible; controllers,
program changes and note releases alone do not count. Cards and their original
numbers stay available: check them again or use **Show all** to restore them.
This changes visualization only, not audio mutes. Reset uses the same defaults.
The automatic grid fits the enabled cards; **Keep grid** and saved preset grids
retain their dimensions. Empty cards are also unchecked when a file's default
preset loads, without rewriting it. Manually selecting a preset restores its
saved visibility. Selecting another pattern rechecks automatic hiding: channels
with notes reappear, while manual hides, removed cards and view settings stay.
An automatic grid follows the new usage; user-sized and preset grids stay fixed.
Playback and seeking within the same pattern never re-hide manually restored cards.
Some files contain a whole soundtrack as one continuous sequence. Leading rests
and the declared endpoint are kept; tracks are not guessed to be separate songs.
Several notes and instruments can share a channel. These are musical parts,
not sixteen hardware oscillators. Preset identities do not depend on the bank.
Card headings and the main player's Channels buttons show the current SoundFont
preset name. Labels follow program changes at the playhead, including after a
seek, without changing channel numbers or resetting the layout. A channel can
still contain a sustained note from a previous preset; the label names its
current selection, not every sounding voice. With no name, the channel label stays.

Each card has an independent stereo dry waveform, spectrum and existing
mute/Solo controls. The keyboard uses active MIDI keys, sustain/sostenuto and
channel pitch bend; it does not measure sample tuning or synthesis modulation.
Percussion and Full mix retain the existing experimental pitch estimator.

Full mix includes enabled channels and their chorus/reverb returns, followed by
the player's volume and L/R switches. Each MIDI channel has a separate effects
group so muting it also removes its effects without stopping its voices. This
is a player mixing policy, not a claim to reproduce a particular hardware unit.
The independent scope synthesizer disables effects and captures all dry channels
in one pass; hiding a card never stops its source. No synth instance per card.

The timeline follows MIDI tempo events (including changes) or SMPTE time
division. At the end of the sequence, held notes/pedals are released and two
seconds are allowed for releases and effects. This tail is included in the
displayed duration; unusually long releases can be cut off. Repeat restarts
from the beginning. Play and F7 also restart a completed track from zero.

For long jumps and rewinds, seeking reconstructs channel controls, programs,
pitch bend and held notes/pedals from MIDI events, then synthesizes up to two
seconds before the target. Audio and scopes use this same bounded preparation;
they do not render every skipped minute. Short forward seeks render normally.
This is an approximate seek: old held samples restart, so their phase, envelopes,
modulation and effects longer than the preparation window can differ from
continuous playback. Ordinary playback is unchanged. Loading a large bank can still
take time and is not interruptible inside FluidSynth's loader.

The parsed sequence and bank bytes form one immutable resource snapshot shared
by audio and scopes. Rewind uses that snapshot even if the original files have
changed or disappeared. New loads reuse the most recently loaded set's input
bytes while each bank's canonical path, size and filesystem timestamps match.
Files are still checked for availability, and SFLIST contents are reread so edits
and bank ordering take effect. Changed banks are read and hashed again; unchanged
banks skip that expensive work. If an external tool preserves file timestamps,
use **Refresh** before reopening the MIDI to force a fresh read. SoundFont
sample-cache identity includes the content hash of the loaded bytes.

## SoundFont sets (SFLIST)

Select an existing UTF-8 `.sflist` with **Browse...**, or place it in the SoundFonts
folder; sets have a **(set)** label. Each line names an uncompressed SF2 file or another list.
Relative paths start at the list's folder; absolute paths also work. For example:

```text
MySet/Melodic-1.sf2
MySet/Melodic-2.sf2
MySet/Drums.sf2
```

Banks retain their embedded bank/program numbers. Later entries take priority
for matching programs; other instruments fall back to earlier banks. This supports
sets split into melodic and percussion banks without changing the music files.
Blank lines, `#` comments, a UTF-8 BOM and quoted paths are accepted. Missing or
invalid banks stop loading with an error; the player does not silently skip them.
Nested lists resolve paths relative to their own folders and expand in place.
Repeated references to the same SF2 retain its last position.

The path-only subset of [foo_midi's JSON list format](https://github.com/stuerp/foo_midi/blob/main/docs/README.md#soundfont-lists-sflist)
also works in `.sflist`, `.json` or `.sflist.json` files:

```json
{
  "soundFonts": [
    { "fileName": "MySet/Melodic-1.sf2" },
    { "fileName": "MySet/Melodic-2.sf2" },
    { "fileName": "MySet/Drums.sf2" }
  ]
}
```

Existing SF2 sets using these forms need no conversion. This does not import
foobar2000's other playback settings. BASSMIDI-specific `gain`, `channels` and
`patchMappings`, legacy text mapping syntax and unknown JSON fields produce an
explicit error rather than being silently ignored. SF3, DLS and SFZ banks remain
unsupported. JSON requires a `soundFonts` array of objects with `fileName` strings.
No bank files are bundled, downloaded or modified.

Lists are limited to 8 nesting levels, 32 unique banks / 64 KiB of combined list text,
with up to 2 GiB per bank and
4 GiB of total SF2 input. Source bytes and decoded samples require additional RAM;
large sets can take several seconds to load initially. Subsequent tracks reuse
the unchanged bank input, while their synthesizers and MIDI state remain separate.
The input cache retains only the latest set in memory, not every bank used during
the session. Refresh clears the cache; active playback keeps its snapshot.
Audio and scopes share the immutable
bank snapshot; backward seeking does not reread the files.

## Limits

- Standard MIDI formats 0/1/2, one MIDI port per song, maximum one-hour sequence,
  64 MiB input, two million events and 1 MiB per SysEx message.
  RIFF/RMID, XMI, live MIDI input and standalone .syx loading are not implemented.
- A sole used output may have a nonzero MIDI port prefix; unaddressed conductor
  SysEx follows it. Empty tracks do not create extra outputs. Conflicting
  explicit output ports are rejected.
- Split SysEx packets are joined at their completion time; unsupported system
  escape events and malformed tracks are reported, not repaired.
- SMF format 2 musical patterns remain separate; note-free setup is handled as
  described above. A `.mid` extension alone does not identify
  a Standard MIDI file; unrelated data with that extension remains unsupported.
- GM-compatible arrangements are the target. FluidSynth handles its supported
  SysEx subset; full Roland GS/Yamaha XG device behavior is not promised.
  Auto routes recognized MT-32 SysEx to Munt. Explicit General MIDI rejects it
  with a request to select Auto/MT-32. MIDI without identifying SysEx cannot be
  reliably classified; choose MT-32 manually for those arrangements.
- SF3/compressed samples and DLS are not enabled. Maximum SF2 input is 2 GiB per bank;
  decoded samples and the source snapshot also require memory.
- Polyphony is limited to 256 synthesis voices per decoder. SoundFont layers
  may use several voices for one MIDI note; dense music can steal voices.
- FluidSynth's internal blocks limit event response to its synthesis block
  boundary (normally 64 samples). This is not zero-latency hardware emulation.

## Implementation and verification

The original MIT `midisequence` parser/timeline is independent of the synth,
and is shared with the MT-32 backend. `MidiTrack` uses FluidSynth 2.5.4
through its public APIs without core patches. CMake selects its C++ OS layer,
static library, SF2 loader and built-in effects; Qt still owns audio devices.
GCEM is separately hash-pinned. Both complete sources and license notices are
included in release packaging. No GLib, external audio driver or additional
runtime DLL is required by this configuration.

Generate original fixtures with `fixtures.make_fixtures`, then run
`bitmusic_checks --midi-checks <fixture-directory> --decoder-only`.
Omit `--decoder-only` for the muted live player check; `--soundfont <path>` adds
a read-only smoke check of a local bank or SFLIST set. These fixtures
contain an authored sine SoundFont, short multichannel MIDI, an endpoint between
sample frames and long seek/pedal cases, not game music. With `--soundfont`, optional
`--midi <path>` measures each subsong's load and distant seeks without modifying it.
The checks cover cache reuse across tracks, concurrent reads, replacement and
missing banks, list ordering, bounded retention and Refresh without restarting
playback. The local-bank smoke check reports initial and subsequent load times.
Windows decoder and live results are recorded in the development handoff;
Linux/macOS runtime and real collection listening remain separate validation.
