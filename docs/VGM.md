# VGM playback boundaries

The existing GME path retains Sega PSG, YM2413 and YM2612 playback and its voice
cards. The new single-POKEY path uses the same pinned GME dependency's Sap_Apu
sound core, without emulating an Atari CPU or requiring system ROMs.

Four hardware oscillators run together with independent mono buffers. Audio
sums the enabled voices; capture records the same samples before user mutes.
Coupled dividers and high-pass effects keep advancing when a channel is muted.
Full mix uses the existing final-output history, including volume and L/R
switches. Keyboard cards use the experimental waveform estimate.

VGM register writes and waits are scheduled against the header's POKEY clock.
Gzip, GD3 titles/system names, file duration, loops and cancellable forward/backward
seeks are supported. Independent audio and scope workers each use one decoder;
adding four cards does not run four emulators. Backward seek resets the complete
chip/buffer state and replays commands. Source files are never changed.

## Yamaha chip mixes

The separate ymfm backend accepts YM2203, YM2151, YM3526 (OPL), YM3812 (OPL2)
and YMF262 (OPL3). Each complete chip is one source with a `mix` label. These
types may be combined, with up to two instances of each. Existing Sega files
retain their detailed GME voice cards. Mixed new-Yamaha/Sega/POKEY streams are
explicitly rejected rather than partially played.

YM2203 combines FM and its three SSG outputs into mono. OPL/OPL2 are mono;
YM2151 uses native L/R. OPL3's A+C outputs map to left and B+D to right.
The dual-OPL2 header flag hard-pans chip 1 left and chip 2 right. Ordinary dual
mono chips remain mono. Chip identity includes type/instance/pan convention,
with stable source keys for presets. Keyboard uses estimated pitch, not notes
read from registers; a chip mix may contain many simultaneous pitches.

Register writes use VGM's 44.1 kHz timeline. Native synthesis uses exact clock
dividers and scheduled timer callbacks, including CSM. A causal 64-tap,
128-phase windowed-sinc filter converts native chip outputs to 44.1 kHz.
Its short rate-dependent delay is identical in audible PCM and captured samples.
YM2203 uses ymfm's minimum-output-rate mode (clock/24), with the core's internal
FM/SSG rate conversion. This is not a claim of cycle-perfect hardware output.

All chips keep running when muted; capture precedes chip mutes. Fixed mix
headroom and file-specified relative/absolute volumes are applied consistently,
including YM2203's paired SSG volume and alternate clocks for second chips.
Master clipping happens after summing enabled chip cards. The gain does not
change when Solo/mutes change. Full mix retains the normal final-output stage.

GD3 metadata, compressed input, repeat, rewind/replay and cancellable seeks are
supported. At a finite command-stream end output becomes silent. Unsupported
commands, undeclared chip targets, mismatched duration and malformed extended
headers are errors; no source file is rewritten. This subset needs no ROMs and
accepts no sample data blocks/stream commands. YM2608/YM2610, sample-based chips,
MDX and other non-VGM containers remain separate work.

Authored tests cover the five chip types, dual and mixed clocks, native stereo,
dual-OPL2 panning, FM/SSG and timer-driven CSM, expected SSG pitch, exact
PCM/chip-mix alignment, mute preservation, chunking/rewind, one-minute looping
history, concurrency, extra clocks/volumes, malformed input and live transport.
These are adapter checks; real music and hardware-fidelity comparisons are not
part of the redistributable fixtures.

## POKEY and common limits

- One POKEY, mono. Dual/variant POKEY and POKEY combined with other chips are
  rejected explicitly, as are commands outside the POKEY/wait/end subset.
- SAP input is not implemented. POKEY is Atari 8-bit/5200 hardware, distinct from
  Atari 2600 TIA.
- This reuses GME's approximations; it is not a new cycle-accurate POKEY emulator.
  SAP core limitations include some joined-channel/high-pass corner cases and
  suppression of pure tones above its 12 kHz cutoff. Special SKCTL modes,
  including serial/two-tone operation and SKCTL reset, are rejected. Standard
  SKCTL value 3 and non-audio register writes are accepted.
- Unsupported declared chips, such as YM2608, WonderSwan and NES APU inside VGM,
  are reported before playback. NSF support does not imply NES VGM support.
  Partial playback that silently drops a declared unsupported chip is rejected.
- Header inspection respects file version, command-data start and the extra
  header boundary; extra metadata is not interpreted as sound chips.

## Validation

Authored register fixtures exercise four distinct tones, noise, DAC, joined
dividers, high-pass operation, expected pitch, exact tap/master synchronization,
mute/unmute state preservation, chunk sizes, rewind, loops, long history,
concurrent instances, compressed input and malformed/unsupported input.
Live checks cover four cards, mono controls, Solo, paused seek, restart,
hide/resume and recovery after an unsupported-chip error.

These tests establish the adapter's behavior, not complete hardware equivalence
or compatibility with every Atari rip. Real music stays outside the repository.
