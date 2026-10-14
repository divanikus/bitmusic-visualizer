# General MIDI

Open a Standard MIDI file (.mid or .midi, format 0 or 1) with a user-provided
uncompressed SF2 SoundFont. The SoundFont row appears only for MIDI in the main
player. Selection is remembered and changing the bank restarts the current song.
The folder button opens the resource directory; Refresh rereads the list.
See [platform locations](../packaging/SOUNDFONTS.txt). No bank is downloaded or
included at runtime. With no saved choice, the first available SF2 alphabetically
is selected; a missing saved bank produces an error on MIDI load.

MIDI tracks play together as one song, rather than appearing as separate
subsongs. Sixteen stable cards represent MIDI channels; channel 10 is percussion.
Some files contain a whole soundtrack as one continuous sequence. Leading rests
and the declared endpoint are kept; tracks are not guessed to be separate songs.
For an initial rest of at least one second, the player's status line shows
`Intro pause · first note in … s`. It counts down to the first positive-velocity
Note On, freezes on pause and updates after seeking or repeating. This is a
scheduled note, not a measurement of audible sound; mutes and bank envelopes
can affect when sound is heard. The hint disappears when that note is reached.
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
- Split SysEx packets are joined at their completion time; unsupported system
  escape events and malformed tracks are reported, not repaired.
- GM-compatible arrangements are the target. FluidSynth handles its supported
  SysEx subset; full Roland GS/Yamaha XG device behavior is not promised.
  Recognized MT-32 SysEx produces an explicit unsupported-message error. MIDI
  without identifying SysEx cannot be reliably classified as MT-32 versus GM.
- SF3/compressed samples and DLS are not enabled. Maximum SF2 input is 1 GiB;
  decoded samples and the source snapshot also require memory.
- Polyphony is limited to 256 synthesis voices per decoder. SoundFont layers
  may use several voices for one MIDI note; dense music can steal voices.
- FluidSynth's internal blocks limit event response to its synthesis block
  boundary (normally 64 samples). This is not zero-latency hardware emulation.

## Implementation and verification

The original MIT `midisequence` parser/timeline is independent of the synth,
so a subsequent MT-32 backend can reuse it. `MidiTrack` uses FluidSynth 2.5.4
through its public APIs without core patches. CMake selects its C++ OS layer,
static library, SF2 loader and built-in effects; Qt still owns audio devices.
GCEM is separately hash-pinned. Both complete sources and license notices are
included in release packaging. No GLib, external audio driver or additional
runtime DLL is required by this configuration.

Generate original fixtures with `fixtures.make_fixtures`, then run
`bitmusic_visualizer --midi-checks <fixture-directory> --decoder-only`.
Omit `--decoder-only` for the muted live player check; `--soundfont <path>` adds
a read-only smoke check of a local bank through a temporary copy. These fixtures
contain an authored sine SoundFont, short multichannel MIDI, an endpoint between
sample frames and long seek/pedal cases, not game music. With `--soundfont`, optional
`--midi <path>` measures a local file's load and distant seeks without modifying it.
Windows decoder and live results are recorded in the development handoff;
Linux/macOS runtime and real collection listening remain separate validation.
