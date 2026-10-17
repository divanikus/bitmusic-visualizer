# Commodore 64 SID

The SID backend accepts PSID and RSID files with one, two or three chips,
including subsongs.
It uses libsidplayfp 2.16.1 and its bundled reSIDfp core. This complete upstream
release includes generated 6502 drivers and keeps a small CMake integration
portable across the existing Windows, Linux and macOS build environments.
The 3.x split libraries can be evaluated separately; this is not a claim that
2.16.1 is the newest version.

The file chooses 6581/8580 and PAL/NTSC; unspecified/any uses 6581/PAL. The player
shows the selected chip models below the title. Layout presets distinguish
models and chip addresses and share the `sid` family so layouts can be transferred.
Existing single-chip source identities are retained; extra voices use the chip
address to avoid ambiguity when adapting presets between two and three chips. File titles
are preserved; unnamed subsongs are numbered. There are no invented durations.
No song-length database or C64 ROMs are supplied or downloaded.

## Signal boundary

Each chip provides three cards showing its oscillator DAC output multiplied by its ADSR
envelope, before the common filter and volume stage. Capture reuses the core's
existing waveform evaluation, including noise, combined waveforms, ring
modulation and oscillator sync. It does not call the stateful oscillator output
method a second time. Voice 3 can still show its internal signal when the tune
disconnects it from the mixer.

For visualization, chip-cycle values are averaged over each 44.1 kHz output
interval. This is a display trace, not an independently rendered audio stem or a
calibrated voltage. Averaging has limited anti-alias rejection at high frequencies;
the master audio continues using the upstream sinc resampler. The shared filter
and output circuit also introduce a small delay relative to the dry voice trace.
Full mix shows the actual delivered audio, including filter effects and
volume-register digi playback. Digi samples do not get a fourth hardware voice
card and are not muted by the three voice switches. Keyboard uses estimated
pitch, not SID register metadata in this increment.

Mute silences a voice at the filter input without writing its control registers,
so it does not disturb oscillators used as modulation/sync sources. Filter state
is shared; listening to one voice is not an additive decomposition of the mix.
Voice traces remain available and are dimmed using the existing mute UI.

Multi-SID uses libsidplayfp's fixed stereo mixer: for two chips, L/R weights
are (1, 0.5) and (0.5, 1); for three, (1, 0.5), (1, 1), (0.5, 1).
The common gain is 1/sqrt(chip count). Dry cards use the same pan weights.
This is a playback convention, not stereo hardware inside a SID or file pan data.
A single SID remains mono. Labels identify chip and voice; there are at most
nine voice cards, plus the existing optional Full mix.

## Transport and threading

The small `MusicTrack` interface selects GME or SID by header. Each worker owns
its decoder. SID uses one audible instance plus one instance for all visible
voice traces across all chips, independent of GUI drawing. Chip identity is
assigned in backend lock/install order, never allocator address order. Each chip
has its own bounded history, cycle sum and sample count. The latter sleeps when scopes are
hidden. Full mix is still captured at audio delivery.

Backward seeks create a fresh C64/SID instance from the bytes already loaded:
hardware reset intentionally retains SID oscillator/envelope state. Forward
seeks render/discard in bounded, cancellable blocks, so distant seeks can take
time but a newer command or closing the player can cancel them. Power-on timing
is fixed to keep independently emulated audio and graphics aligned. Pending PCM
and tap history use the same sample origin; tap reads exclude decoder lookahead.

The upstream filter-model singleton has a mutable dither cursor. Our patch gives
each decoder its own cursor and table-building threads separate fallback cursors.
Shared lookup tables remain read-only during playback. Filter-model tuning is not
exposed. Patches are hash-checked and replayed from pristine source on configure.

## Optional ROM files

See [filenames, sizes and platform locations](../packaging/ROMS.txt). ROMs are
read only from the fixed roms/c64 directory, with no download, picker or copying.
The loaded file owns an immutable image snapshot shared by audio and scope
instances, including backward seeks. Reopening takes a fresh snapshot.
Wrong-size/unreadable images count as unavailable, but do not block other tunes.

RSID's BASIC flag requires basic.rom. Otherwise the patched ROM banks detect
reads from unavailable data/code, excluding the upstream interrupt/reset hooks
and discarded CPU bus reads. A needed image stops playback with one warning per
load, naming the image and its location. Self-contained RSID and ordinary PSID
can use the upstream minimal hooks without any ROM images. This detects runtime
use, not all future dependencies in advance, and does not prove that a supplied
image of the correct size is authentic. No music-file repair is attempted.

## Boundaries and checks

MUS/STR and song-length databases remain outside this increment. Unsupported
inputs are rejected; the app does not repair source files. Compatibility depends
on the upstream C64 emulator and any required user-supplied ROMs; this is not a
promise of every C64 tune.

`bitmusic_checks --self-test <fixture-directory> --sid-only` exercises authored fixtures for both
models/clocks, metadata/subsongs, actual voice frequencies and isolation, exact
capture/master PCM agreement, rewind/irregular blocks, cancellation, mute/Solo,
ring/sync preservation, filter boundaries, concurrency and live transport.
Extended checks cover 2/3 chips with mixed models, self-contained RSID with its
own CIA interrupt, pan/mute isolation, ROM requirements, a tiny authored test ROM,
shared ROM snapshots and one-shot UI warnings. No proprietary ROM is a test fixture.
Add `--decoder-only` to avoid audio/window checks. The decoder portion is also
included in `--tap-checks ... --decoder-only`, used by cross-platform packaging CI.
Local music remains private, read-only test input.
