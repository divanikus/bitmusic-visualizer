# Visualization cards

Click the pencil, select a card in **Cards**, then choose its **View**. The change
is immediate. **Add card…** asks for a source and view, keeping existing cards.
For example, keep Square 1's waveform, add Square 1 / Keyboard, then add a Full mix
/ Spectrum. Drag and resize them in the same grid. Increase Rows/Columns or hide
other cards if the editor reports overflow. **Remove** deletes any selected card
without muting its audio source. Use **Add card…** to bring that source back, or
**Reset** to restore the default cards. Checkboxes and header crosses just hide
cards, keeping their entries in the list.

Cards of the same source share colors, audio capture and mute state. Moving or
hiding a card never mutes its source. Added cards and view choices, like sizes and
order, last for the current file/session. Opening another file or pressing Reset
restores the original waveform cards. Keep grid preserves only rows and columns.
Changing subsongs within a file keeps the layout. Themes remain color presets.

## Spectrum

Spectrum shows frequency content on a logarithmic 20 Hz–20 kHz axis, with a fixed
-80 to 0 dBFS amplitude range. It works with all supported sources, including
Full mix. Stereo uses separate L/R plots; mono stays one plot. Full mix follows
output volume and mutes, so a zero-volume player has a flat Full mix spectrum.

Selecting a Spectrum card reveals a compact **Style: Line / Bars** selector and
**Grid** checkbox below View. These apply to that card immediately, including
while paused. Grid hides both grid lines and scale labels, freeing that space for
the plot. Defaults are Line with Grid on. Like the card's view, these options
survive rearranging and switching views, but reset on Reset or opening a new file.

Bars group frequencies into up to 96 logarithmic bands, adapting their count to
card width and keeping the strongest FFT peak in each band. They use a vertical
gradient based on the source's theme color. Stereo, channel/output dimming and
GPU trail/glow effects work with both styles; CPU draws the same bars without
effects. Duplicate cards can use different styles without additional decoding.

The analysis uses the latest 2048 samples (about 46 ms) with a Hann window and DC
removal. Its bin spacing is about 21.5 Hz; closely spaced low-frequency tones may
not be distinguishable. This is a view of the existing PCM, not an instrument or
note detector. Duplicate cards reuse analysis. Wave height/trigger controls apply
to waveforms only.

## Keyboard

Keyboard highlights all notes supplied by its source, rounded to the nearest
equal-tempered keys (A4 = 440 Hz). Simultaneous notes are named together, for
example C4 · 261.6 Hz + E4 · 329.6 Hz + G4 · 392.0 Hz; each note shows its
frequency, and unison voices light the same key once. The fixed keyboard covers
C1–B7; pitches outside that range
are still named above it. All supported audio sources, including Full mix, can
use this view. It has two automatic modes:

- **Chip pitch:** NES/NSFE Square 1, Square 2 and Triangle, and Sega PSG tones in
  VGM/VGZ, use oscillator periods and gates. A combined PSG card alongside
  YM2612 or YM2413 can show its three tones simultaneously. PSG-only files expose
  separate Square cards, each with one tone. Noise is excluded from chip notes;
  No note means no active tonal oscillator. Dual-PSG/T6W28 configurations still
  use Estimated mode.
- **Estimated:** other voices, expansion chips and Full mix estimate one periodic
  pitch from their audio. The readout and editor explicitly mark this mode.
  Chords, drums, noise, strong harmonics and quick note changes can confuse it,
  including octave errors. No stable pitch means the signal was not periodic
  enough; silence shows No note. This is an experimental visual aid, not a score
  transcription or a display of all notes in a chord.

The card itself accepts multiple notes for any source; the number available
depends on its backend. AY, Game Boy and FM chip-note extraction is not connected
yet. Full mix currently estimates one pitch rather than combining chip notes.

Chip-pitch history follows the decoder's actual fixed-point resampling clock
and filter delay, accounting for look-ahead. Estimates use up to 4096 samples
(about 93 ms), analyze the stronger stereo side and refresh at up to 30 Hz per
source. Duplicate views share analysis. Rapid arpeggios can change between
display frames. Voice/output mutes dim the display while retaining source state;
Full mix reflects the actual delivered audio, including volume and all mutes.
Pause freezes it; seeks and file changes replace old state. This view does not
expose a chip-register inspector or add support for new file formats.

## Waveform controls

In the scope window, click the pencil and open **Waves…**. **Frame rate** selects
a target of **30, 60 or 120 FPS**, applied immediately and remembered across
restarts. The default is 30. It controls both channel snapshot preparation and
scope presentation, including GPU trail fading; audio playback is independent.
Higher rates increase CPU/GPU work. Actual fresh-wave and display rates depend
on the track, audio device, renderer, system load and monitor refresh rate.
The display can repeat a snapshot when no newer audio is available. Trail length
and Hold/Release durations remain measured in time, not frames.

**Smooth auto** scales each card to its recent signal level. **Hold** delays
shrinking the remembered peak; **Release** controls how slowly it then falls.
This makes quiet notes grow back gradually. **Instant auto** rescales every frame,
so a rapidly decaying note may still look large. **Fixed** uses a constant range,
making amplitude and volume comparisons more meaningful.

**Trigger** chooses where a visible wave begins. **Stable** tries to retain a
consistent phase for a periodic waveform. **Rising edge** begins near an upward
crossing. **Off** displays the current sample window without aligning it.
Noise, percussion and changing timbres may keep moving with any trigger.
These controls change the display, not the sound.

**Stereo** draws separate L/R lanes only for sources that provide stereo.
Mono remains one trace. Speaker mutes dim the corresponding lane. Individual
voice mutes dim their cards while keeping the unmuted voice signal available.

**Full mix** instead captures the actual PCM sent to the audio device after
voice mutes, L/R switches and volume. It includes shared effects such as SPC echo,
before the operating system's volume/effects. Pause freezes the delivered mix;
after a paused seek there is no new output until playback resumes.

**Fading trail** retains older waves for up to 999 ms. **Line glow** adds a soft
halo. Both support brightness up to 100%. Effects apply only to GPU rendering
and increase graphics load. Hidden/minimized windows suspend scope work.

Renderer, effects, waveform controls (including frame rate) and the original Full mix card's visibility are saved separately
from color themes. Grid/card layout remains session-local. A theme contains 32
voice palettes, Full mix colors and the window background. Colors stay with their
original channel when cards move; a newly opened file assigns palettes by channel
number. Apply to all fills unused palettes too.
