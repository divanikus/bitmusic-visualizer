# libxmp channel capture

libxmp 4.7.3 (MIT) is statically linked. The hash-checked patch observes the
32-bit stereo mixer before and after each voice, without changing its samples,
state or arithmetic. Deltas include per-voice panning, filters and ramps.
Background/NNA voices accumulate under their original tracker channel.
The public channel-mute API supplies audio mute/Solo; scope decoding remains
unmuted, just as it does for other backends.

Each decoder has a separate capture object, installed only during its own
render call via a thread-local scope. Tick buffers are copied to scope history
only as the caller consumes PCM, so the mixer cannot move the visible playhead
ahead by a tracker tick. Capture is disabled on the audio decoder.

Cards are pre-clipping voice contributions. A detached global anticlick
remainder with no owning voice is not assigned to a card; Full mix includes it.
The same Amiga/Paula mixer and filters run in both decoders. Tracker channels
are sample playback lanes, not a count of distinct instruments in the song.

Changes: `patches.json`, `capture.*`, `taps.h`, `cmake/PatchXmp.cmake`.
Original integration code is MIT; upstream copyright and permission notices
remain intact. See `LICENSES/libxmp-MIT.txt` and the complete source archive
listed in `packaging/sources.json`.
