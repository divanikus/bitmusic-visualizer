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

## Limits

- One POKEY, mono. Dual/variant POKEY and POKEY combined with other chips are
  rejected explicitly, as are commands outside the POKEY/wait/end subset.
- SAP input is not implemented. POKEY is Atari 8-bit/5200 hardware, distinct from
  Atari 2600 TIA.
- This reuses GME's approximations; it is not a new cycle-accurate POKEY emulator.
  SAP core limitations include some joined-channel/high-pass corner cases and
  suppression of pure tones above its 12 kHz cutoff. Special SKCTL modes,
  including serial/two-tone operation and SKCTL reset, are rejected. Standard
  SKCTL value 3 and non-audio register writes are accepted.
- Unsupported declared chips, such as YM2203, WonderSwan and NES APU inside VGM,
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
