# General MIDI

The MIDI row offers Auto, General MIDI and MT-32. Auto recognizes Roland MT-32
SysEx; see [MT-32 playback](MT32.md) for ROMs, parts and its limitations.

Open a Standard MIDI file (.mid or .midi, format 0 or 1) with a user-provided
uncompressed SF2 SoundFont. The SoundFont row appears only for MIDI in the main
player. Selection is remembered and changing the bank restarts the current song.
Selecting the current bank or MIDI engine again preserves playback and position.
The folder button opens the resource directory; Refresh rereads the list.
See [platform locations](../packaging/SOUNDFONTS.txt). No bank is downloaded or
included at runtime. With no saved choice, the first available SF2 alphabetically
is selected; a missing saved bank produces an error on MIDI load.

MIDI tracks play together as one song, rather than appearing as separate
subsongs. Sixteen stable cards represent MIDI channels; channel 10 is percussion.
On file load, channels with no positive-velocity Note On anywhere in the song
start unchecked in the card editor. Late entrances remain visible; controllers,
program changes and note releases alone do not count. Cards and their original
numbers stay available: check them again or use **Show all** to restore them.
This changes visualization only, not audio mutes. Reset uses the same defaults.
The automatic grid fits the enabled cards; **Keep grid** and saved preset grids
retain their dimensions. Empty cards are also unchecked when a file's default
preset loads, without rewriting it. Manually selecting a preset restores its
saved visibility, and playback/seeking never overrides your edits.
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
changed or disappeared. A new load reads the selected bank afresh. SoundFont
sample-cache identity includes its content hash to prevent stale samples.

## Limits

- Standard MIDI formats 0/1, one MIDI port, maximum one-hour sequence, 64 MiB
  input, two million retained events and 1 MiB per SysEx message. Format 2,
  RIFF/RMID, XMI, live MIDI input and standalone .syx loading are not implemented.
- A sole used output may have a nonzero MIDI port prefix; unaddressed conductor
  SysEx follows it. Empty tracks do not create extra outputs. Conflicting
  explicit output ports are rejected.
- Split SysEx packets are joined at their completion time; unsupported system
  escape events and malformed tracks are reported, not repaired.
- SMF format 2 stores independent patterns, rather than simultaneous parts.
  These are not silently merged as format 1, including when a file puts tempo
  in one pattern and notes in another. A `.mid` extension alone does not identify
  a Standard MIDI file; unrelated data with that extension remains unsupported.
- GM-compatible arrangements are the target. FluidSynth handles its supported
  SysEx subset; full Roland GS/Yamaha XG device behavior is not promised.
  Auto routes recognized MT-32 SysEx to Munt. Explicit General MIDI rejects it
  with a request to select Auto/MT-32. MIDI without identifying SysEx cannot be
  reliably classified; choose MT-32 manually for those arrangements.
- SF3/compressed samples and DLS are not enabled. Maximum SF2 input is 1 GiB;
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
a read-only smoke check of a local bank through a temporary copy. These fixtures
contain an authored sine SoundFont, short multichannel MIDI, an endpoint between
sample frames and long seek/pedal cases, not game music. With `--soundfont`, optional
`--midi <path>` measures a local file's load and distant seeks without modifying it.
Windows decoder and live results are recorded in the development handoff;
Linux/macOS runtime and real collection listening remain separate validation.
