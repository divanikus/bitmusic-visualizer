# Atari 2600 / TIATracker

Open a **`.ttt` TIATracker project** with the file picker or drag-and-drop.
No ROMs, SoundFonts or external player are needed. A project contains one song;
its two hardware channels appear as **TIA 1** and **TIA 2**, with current
instrument names. The ordinary Waveform, Spectrum, estimated Keyboard and
Full mix views, mute/Solo, presets, seeking and Repeat controls work as usual.
TIA is mono, so the Stereo display switch does not duplicate its graph.

The file's PAL/NTSC setting selects 50/60 updates per second. Playback follows
its start patterns, global/per-pattern speed, alternating row speeds, melodic
envelopes, slides, percussion overlays and sequence jumps. Duration and loop
start are calculated from the sequencer; Repeat off stops after the first pass.
With Repeat on, the normal player transport returns to the calculated loop start.
Projects without a loop repeat from the beginning.

## Boundaries

- This supports TIATracker project format version 1, not arbitrary Atari games
  or `.bin`/`.a26` cartridges. SAP is Atari 8-bit/POKEY and remains unsupported.
- Playback uses the Stella core bundled with TIATracker 1.3, including its
  fixed 31,400 Hz sound clock. This targets the tracker preview; it is not a
  cycle-exact cartridge emulator or an analog hardware model. Host resampling
  and a 10 Hz DC filter apply equally to audio and channel signals.
- Keyboard uses the existing experimental audio pitch estimate. TIA frequency
  dividers and noisy timbres are not general MIDI notes. Pattern/Piano roll
  are not exposed for this format in this iteration.
- Malformed values and references are rejected, not repaired. Analysis is
  bounded to 16 MiB, 262,144 stored rows and two hours of control frames.
- Seeking reconstructs the core from the beginning when moving backward.
  Sound and visualization use independent instances with identical timing.

## Checks

Generate original fixtures with `fixtures.py`, then run:

```sh
bitmusic_checks --ttt-checks test-output --decoder-only
bitmusic_checks --ttt-checks test-output --software-scopes
```

Omit `--software-scopes` for the configured GPU default in the test process.
Extra TTT paths after the fixture directory are read-only local checks. Tests
cover PAL/NTSC, control-flow duration, finite/looping input, native-core mono/split
equality, exact PCM/tap alignment, chunk independence, mute continuity, seek,
concurrent decoders, invalid input and live transport. Fixtures require no
private collection. See [engine and licenses](../native/tia-replay/README.md).
