# Munt part capture

`patches.json` contains LGPL-2.1-or-later modifications to libmt32emu 2.8.3.
Copyright (c) 2026 Bit Music Visualizer contributors. Upstream copyright and
license headers are retained. See [license](../../LICENSES/Munt-LGPL-2.1.txt)
and [authors](../../LICENSES/Munt-AUTHORS.txt).

CMake verifies the SHA-256 of each pristine source file and reapplies exact
anchors before configuring Munt (which copies public headers). This is a private
static integration, not a binary-compatible replacement for system libmt32emu.
The complete pinned upstream archive and this patch are release source inputs.

- The Float partial mixer calls a nullable callback with native 32 kHz timestamp,
  owner part and L/R samples. Ring-modulated pairs are captured after coupling;
  their slave is not counted twice. Summing by owner produces nine musical parts.
- The callback precedes the part mute gate. Mute does not skip any partial or
  envelope generation. The unmuted Float mixing arithmetic is unchanged.
- Taps precede shared DAC, reverb and analogue output. The player doubles raw
  tap amplitude to match the selected NICE DAC scale, then resamples all lanes
  together. They are not nine independently processed hardware outputs.
- TVP's process-wide `rand()` is replaced by a per-synth 32-bit LCG, initialized
  to one on construction. Its upper bits retain the 0–3-sample timer jitter.
  This avoids cross-thread global RNG coupling and makes audio/scopes and cold
  restarts reproducible across hosts. It changes the particular jitter sequence;
  a claim of bit-identical output to an unmodified Munt is not made.

No ROM data is present in these files. The MIT application adapter lives in
`native/mt32track.*`; the derived modifications here retain LGPL terms.
