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

## Yamaha voices and chip mixes

The separate ymfm backend accepts YM2203, YM2151, YM2608, YM2610/2610B,
YM3526 (OPL), YM3812 (OPL2), Y8950 and YMF262 (OPL3).
YM2203 exposes six voices; each other complete chip is one source with a `mix`
label. These types may be combined, with up to two instances of each. Existing Sega files
retain their detailed GME voice cards. Mixed new-Yamaha/Sega/POKEY streams are
explicitly rejected rather than partially played.

YM2203's FM and PSG voices are mono. OPL/OPL2 and Y8950 are
mono; YM2151 uses native L/R. YM2608 and YM2610/2610B combine stereo FM/ADPCM with
centered SSG. OPL3's A+C outputs map to left and B+D to right.
The dual-OPL2 header flag hard-pans chip 1 left and chip 2 right. Ordinary dual
mono chips remain mono. Chip identity includes type/instance/pan convention
and the YM2610B variant,
with stable source keys for presets. Keyboard uses estimated pitch, not notes
read from registers; a chip mix may contain many simultaneous pitches.

Register writes use VGM's 44.1 kHz timeline. Native synthesis uses exact clock
dividers and scheduled timer callbacks, including CSM. A causal 64-tap,
128-phase windowed-sinc filter converts native chip outputs to 44.1 kHz.
Its short rate-dependent delay is identical in audible PCM and captured samples.
OPN chips use ymfm's minimum-output-rate mode: clock/24 for YM2203, clock/48 for
YM2608 and clock/144 for YM2610/2610B, including internal FM/SSG rate conversion.
This is not a claim of cycle-perfect hardware output.

All chips keep running when muted; capture precedes user mutes. Fixed mix
headroom and file-specified relative/absolute volumes are applied consistently,
including paired SSG volumes and alternate clocks for second chips.
Master clipping happens after summing the enabled output of each chip. The gain does not
change when Solo/mutes change. Full mix retains the normal final-output stage.

GD3 metadata, compressed input, repeat, rewind/replay and cancellable seeks are
supported. At a finite command-stream end output becomes silent. Unsupported
commands, undeclared chip targets, mismatched duration and malformed extended
headers are errors; no source file is rewritten. Loops whose leading 0x62/63
waits would change length on later passes are rejected instead of mistimed.
DAC stream commands (0x90-95)
and compressed PCM stream banks are not implemented in this backend. Other
sample chips, MDX and non-VGM containers remain separate work.

### YM2203 voice cards

Each chip has FM 1–3 and PSG A/B/C, with individual mute/Solo and the normal
waveform, spectrum and estimated-pitch keyboard views. Two chips expose twelve
voices. PSG channels already include their tone/noise gates and shared envelope;
noise and envelope are not extra sound sources. Channel 3's special FM mode
remains one hardware voice, including its differently tuned operators.

One emulator advances all voices together. FM capture precedes the chip's
shared DAC quantization; PSG capture uses its native resampler. The audio mix
preserves the original shared-DAC output for every FM mute combination, then
adds enabled PSG voices. Summing the FM cards is therefore not guaranteed to
reproduce the audible mix exactly. Full mix shows the actual final PCM.

Each native frame retains the eight possible FM DAC combinations in a bounded
FIR history, allowing immediate mutes without restarting oscillators or leaking
previously enabled voices through the filter. This does not create an emulator
per voice. The original all-enabled sound and internal rate conversion remain
unchanged. No chip-register pitch metadata is exposed yet.

Preset source keys are `ym2203.<instance>.fm:0..2` and
`ym2203.<instance>.ssg:0..2`. This changes YM2203's configuration identity from
the earlier, unpublished chip-mix layout; its old default preset will not be
automatically applied to six voice cards. Other chips retain their identities.

The voice checks compare all eight FM algorithms and all mute combinations
against the pinned unmodified core, including feedback, prescaler changes,
PSG noise/envelopes, channel-3 special mode and timer-driven CSM. They also
verify six/twelve distinct mono signals, real PCM timing, mute/Solo state,
seeking/loops, chunking, concurrency and live scope suspend/resume.

### Sample memory and the optional rhythm ROM

VGM image blocks 0x81 (YM2608 ADPCM-B), 0x82/83 (YM2610 ADPCM-A/B) and 0x88
(Y8950 ADPCM-B) are applied at their stream positions, with independent banks
per chip. Bit 31 of block size selects the second chip. Sparse regions default
to 0xff; offsets, lengths and declarations are validated before playback. Each
bank is limited to the 24-bit address space (16 MiB). Register-driven Delta-T
RAM uploads are supported. Backward seeking recreates memory and replays the
same blocks/writes; it never retains data from a future point in the track.
Missing sample memory on an actual read is an error, not a silent missing part.

YM2608's internal rhythm ROM is separate from VGM's external ADPCM-B data.
Provide `roms/ymfm/ym2608_adpcm_rom.bin`, exactly 8192 bytes, at the
[platform-specific location](../packaging/YAMAHA-ROMS.txt). The warning appears
only on a rhythm read, gives the exact path and asks to reopen the track.
FM, SSG and external ADPCM-B do not require this ROM. A loaded tune retains one
immutable snapshot shared by audio and scopes, including rewind. No ROM is
bundled or downloaded; the size check does not authenticate its contents.

Authored tests cover all accepted chip types, dual and mixed clocks, native stereo,
dual-OPL2 panning, FM/SSG and timer-driven CSM, expected SSG pitch, exact
PCM/chip-mix alignment, mute preservation, chunking/rewind, one-minute looping
history, concurrency, extra clocks/volumes, malformed input and live transport.
Synthetic ADPCM tests cover both sample engines, sparse/timed/dual-chip banks,
CPU uploads, rewind/loops, YM2610B's extra FM channels and on-demand ROM warnings
with immutable cross-worker snapshots. Fixtures contain no game or ROM data.
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
- Unsupported declared chips, such as YMF278B, WonderSwan and NES APU inside VGM,
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
