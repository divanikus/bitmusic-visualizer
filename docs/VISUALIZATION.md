# Visualization cards

The pencil opens three tabs in the existing side panel:

- **Cards:** visibility, adding/removing cards and the selected card's view/options.
- **Layout:** grid dimensions, Numbers/Names, Keep grid and saved presets.
- **Display:** color themes and global Waves/Effects.
- **Settings → Rendering** in the main player: renderer and FPS.

Stereo, Levels, background color and exact window size remain in the top strip.
Changes apply live. **Done** closes the editor without saving a preset or theme;
use their explicit Save buttons. **Reset layout** preserves colors and saved presets.
The card list uses the available height alongside its selected-card settings;
each scrolls only when needed. **About this view** expands the explanation and
limitations for Keyboard, Piano roll or Pattern.

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
order, last for the current file/session unless explicitly saved as a preset.
Opening another file without an assigned preset, or pressing Reset,
restores the original waveform cards. Keep grid preserves only rows and columns.
Changing subsongs within a file keeps the layout. Themes remain color presets.

**Shift-click** anywhere on a voice card to Solo its source. Shift-click another
source to switch Solo, or the same source to restore previous mutes. This works
with all card views, including duplicates, in normal,
edit and fullscreen modes. Full mix is not a voice and cannot be soloed.
Unmodified editor clicks still drag, resize, hide cards or edit colors.

The keyboard icon in the visualizer title bar or **F1** opens the [shortcut guide](../README.md#keyboard-shortcuts).
Digits refer to audio channel numbers, not grid slots. If several cards share
a channel, a digit cycles them together based on its original card's current
view (or the first remaining card if the original was removed). Alt+digit hides
all of that channel's cards when any are enabled, and shows them when all are
hidden. Removed cards stay removed until added again or reset.
**0 / Alt+0** applies the same view/visibility actions to Full mix cards.
**Ctrl+Left / Ctrl+Right** toggles the left/right audio output, like the player's L/R buttons.
F2–F4 also change hidden cards and Full mix, preserving their visibility and size.
**Ctrl+Shift+R** additionally unmutes all voices and both audio outputs; the editor's
**Reset** button continues to reset only the layout.

## Layout presets

**Preset** is in **Layout**, with **Save preset…** and **Use by default**.
The dropdown applies immediately. Save preset asks only for a name;
Save changes to Replace when that name already exists for this configuration.
A different configuration's preset cannot be overwritten under the same name.
An asterisk after a selected name means there are unsaved edits. No edits are
written automatically, including changes made with shortcuts.

Use by default assigns the **saved version** for the current sound configuration.
**Clear default** removes that assignment; the current default's name is shown
below the button. Reset changes the current layout and leaves saved
presets/defaults intact; Clear default also works after Reset
or when the assigned file is missing. To replace an existing default, select a
different saved preset and click Use by default.

Defaults apply when opening a file with the same hardware/channel configuration.
Subsong switches keep current edits. Configurations without an assigned valid
default retain existing initialization, including Keep grid, label/stereo
preferences and the saved Full mix choice. A preset supplies its own grid even
when Keep grid is checked. Mono sources still draw one lane even if the preset
has Stereo enabled. Presets do not change audio mutes/outputs, volume, theme,
wave/effect/FPS/renderer settings or window geometry.

The list contains exact matches first, then **(adapt)** presets from the same
platform family. NES variants share the five base voices; expansion channels
match by their chip and role, not ambiguous names such as Square 3. Transfer
retains matching cards, duplicates, order, sizes and hidden states, skips absent
sources and appends waveform cards for newly available channels. Intentionally
removed cards stay removed. Full mix is transferable. The saved grid is kept;
cards that do not fit remain in the list as Overflow. An adaptation exceeding
the 128-card limit is rejected. Save an adapted layout under its own name before
assigning it as the new configuration's default. Cross-configuration transfer
is always manual; base NES defaults never automatically apply to NES + VRC6.

VGM/VGZ share configuration IDs; NSF/NSFE with the same chips do too. Song titles,
file paths, compression and display system tags do not determine compatibility.
VGM groups use chip families (YM2612, or PSG/YM2413), since metadata cannot reliably
identify a console. Separate PSG voices and a grouped PSG source are distinct
roles and are not merged during transfer. Unverified backend variants use opaque
source identities and do not borrow another configuration's channel mappings.

Presets are portable UTF-8 INI files in `presets/` alongside `themes/`, using the
same platform storage policy. Version 1 stores a `[Preset]` group with a hardware
configuration ID/family/source list, Rows/Columns, Numbers/Names/Stereo and card
count. Ordered `[Card1]`, `[Card2]`, etc. groups store Source, View (0 waveform,
1 spectrum, 2 keyboard, 3 piano roll, 4 pattern), Width/Height in cells, Visible,
Bars and Grid. Optional Horizontal, Commands and FirstChannel fields retain
note-roll direction and pattern options. KeyboardSize stores the piano keyboard's
percentage of the plot, defaulting to 25% when absent.
A view unavailable for the loaded file falls back to Waveform. The file
contains no music paths. Files are validated before applying and saved atomically.
Invalid default files use the ordinary initial layout; Clear default removes their
assignment. Defaults are bound to exact configurations in application settings.

## Window size

Click the window-size icon (a rectangle with diagonal arrows) next to the
background eyedropper. **Width × height** fields expand beside it on the same
row, without a separate size-control row. Typing a valid number or using
the arrows resizes the window immediately. Click the icon again to collapse the
fields. There is no dialog or separate Apply step. Incomplete numbers do not
resize the window while you type. Changing a dimension exits maximized mode.

Dimensions are screen pixels for the whole window, including its title bar and
any visible editor controls. Dragging a window edge also updates the fields.

Display scaling can make some pixel sizes unavailable: **≈** marks rounding;
hover over it or either field to see the actual dimensions. The fields keep
your input while you type and show the actual size when you finish editing.
The existing minimum window size still applies. Size and position are remembered
on player exit, independently of themes and the card grid. Moving to a display
with a different scale may change the pixel dimensions.

## Presentation

**Display → Waves… → Line width** changes waveform and spectrum lines from 0.5 to 8
display-scaled pixels (default 1.6). It applies immediately, including while
paused, to CPU and GPU drawing. Spectrum bars and keyboard keys keep their
dimensions. Glow remains a separate effect.

**Levels**, beside **Stereo** in the editor toolbar, enables compact peak meters
on the left of every visible card's plot, including Keyboard and Full mix. The
fixed -60 to 0 dBFS scale is independent of waveform autoscaling. Stereo sources show L/R even if
the waveform's Stereo display is off; mono sources show one M meter. The top is
0 dBFS and each tick represents 12 dB. Very small cards omit the meters.

Meters use the captured PCM window, without extra decoding. Voice meters share
the waveform's capture stage (often a dry signal), so they are not measurements
of the final audible mix. Muted voice/output lanes remain visible but dimmed;
Full mix follows the actual final volume and mutes. Silence empties the meter.
Meters have no glow or fading trail. Line width and Levels survive file changes
and restarts, independently of color themes and layout presets. Restore Defaults
in Waves resets line width but preserves Levels and the separate application frame rate.

The full-screen icon beside the pencil, or **F11** from either player window,
expands the visualizer on its current display and hides the title bar/editor.
The themed window background fills the screen edges without an outer window border.
Double-click the visualization outside edit mode to toggle fullscreen. Editor
controls keep their normal behavior; double-clicking the title bar still maximizes.
**Esc/F11** restores the previous window size, position, maximized state and editor.
Move the pointer to the top edge to reveal a temporary exit button. Playback
shortcuts remain available; fullscreen does not change audio or the card layout.
Closing the application while fullscreen preserves the previous window geometry;
the next launch opens in windowed mode.

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
C1–B7; pitches outside that range are still named above it. All supported audio
sources, including Full mix, can use this view. It has two automatic modes:

- **Chip pitch:** NES/NSFE Square 1, Square 2 and Triangle, Sega PSG tones in
  VGM/VGZ, GBS Square 1/2 and Wave, and AY A/B/C use oscillator periods and gates.
  A combined PSG card alongside
  YM2612 or YM2413 can show its three tones simultaneously. PSG-only files expose
  separate Square cards, each with one tone. Noise is excluded from chip notes;
  No note means no active tonal oscillator. Dual-PSG/T6W28 configurations still
  use Estimated mode. GBS respects length, envelope, sweep and hardware routing;
  Wave reports the wavetable cycle rate, which need not be its strongest harmonic.
  AY respects tone enable, volume and envelope zero gates, including CPC clocks.
  Noise-only and envelope-only AY sounds have no tone-generator note. Dedicated
  noise and beeper cards remain Estimated.
- **Estimated:** other voices, expansion chips and Full mix estimate one periodic
  pitch from their audio. The readout and editor explicitly mark this mode.
  Chords, drums, noise, strong harmonics and quick note changes can confuse it,
  including octave errors. No stable pitch means the signal was not periodic
  enough; silence shows No note. This is an experimental visual aid, not a score
  transcription or a display of all notes in a chord.

The card itself accepts multiple notes for any source; the number available
depends on its backend. FM chip-note extraction is not connected yet. Full mix
currently estimates one pitch rather than combining chip notes.

Chip-pitch history follows the decoder's actual fixed-point resampling clock
and filter delay, accounting for look-ahead. Estimates use up to 4096 samples
(about 93 ms), analyze the stronger stereo side and refresh at up to 30 Hz per
source. Duplicate views share analysis. Rapid arpeggios can change between
display frames. Voice/output mutes dim the display while retaining source state;
Full mix reflects the actual delivered audio, including volume and all mutes.
Pause freezes it; seeks and file changes replace old state. This view does not
expose a chip-register inspector or add support for new file formats.

## Piano roll and Pattern

These views appear in **Cards → View** and **Add card…** when the file provides
the required data. They use the same grid, resize handles, duplicate cards and
presets. Start with a tall **Full mix / Piano roll** for MIDI, or a wide
**Full mix / Pattern** for a tracker. Other cards can keep their waveforms or
spectra. Digit shortcuts include the new views when available.

**Piano roll** shows General MIDI channel or MT-32 part key presses over the next
four seconds, moving toward a keyboard. **Notes move left** changes its direction
without rotating labels. The keyboard covers at least A0–C8, extending for notes
outside that range; its range stays fixed during playback. Note colors come from
theme palettes by MIDI program/bank identity, with separate percussion-key colors.
Colors can repeat. MT-32 SysEx part routing is respected, but custom SysEx timbre
contents are not assigned unique colors.

**Keyboard size** sets its height from 5–80% of the plot, default 25%, without
changing the note range. It updates immediately and grows with the card instead
of using a fixed pixel-height limit. In horizontal mode it controls keyboard
width. This per-card setting is saved in layout presets.

Bars run from note-on to note-off. They show key presses, not acoustic duration:
pedal sustain, release tails, pitch bend and timbre tuning are not drawn. A card
draws at most 4096 intersecting bars to bound work on very dense files.

**Pattern** shows the tracker's current pattern, following its actual order/row
at the audible playhead, including jumps and tempo changes. The highlighted row
stays in the center; space beyond the current pattern is blank. Columns contain
note and instrument numbers. **Volume and effects** adds volume and two effect
columns. Numbers are hexadecimal; notes/effects use libxmp's normalized format,
so their octave labels or command codes can differ from the original tracker.
This is a read-only display, not a pattern editor or a reconstruction from audio.

On **Full mix**, Pattern fits as many channel columns as the card allows.
Enlarge it or change **First channel** to inspect later columns. A voice card
shows only that channel. Pattern is omitted for modules over two million cells;
playback is still available. See [tracker details](TRACKERS.md).

In these two views, **Full mix** combines musical events from all sources.
Muted sources remain visible but dimmed, unlike the audible PCM used by its
waveform and spectrum. Stereo does not split event displays into L/R lanes.
Pause freezes the cursor; seeks replace it. Optional Levels still measure PCM.
Glow and trail apply to signal plots, not note bars or pattern text.

## Waveform controls

In the main player, click the gear and open **Settings → Rendering**. **Frame rate** selects
a target of **30, 60 or 120 FPS**, applied immediately and remembered across
restarts. The default is 30. It controls both channel snapshot preparation and
scope presentation, including GPU trail fading; audio playback is independent.
Higher rates increase CPU/GPU work. Actual fresh-wave and display rates depend
on the track, audio device, renderer, system load and monitor refresh rate.
The display can repeat a snapshot when no newer audio is available. Trail length
and Hold/Release durations remain measured in time, not frames.

Open **Display → Waves…** for the global waveform options.
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
from color themes. Grid/card layout remains session-local unless saved as a preset. A theme contains 256
voice palettes, Full mix colors and the window background. Colors stay with their
original channel when cards move; a newly opened file assigns palettes by channel
number. Apply to all fills unused palettes too. Existing 32-palette themes load
unchanged for their first 32 channels and repeat those colors for higher channels.
The color dialog lists at least 32 palettes, or all voices in the loaded file.
Full mix has its own palette and never takes a voice's color slot.

Large configurations use a default grid with at most eight rows and eight columns
to leave room for waveforms. Extra cards remain in the editor; hiding or moving
cards frees positions for them. Grid dimensions can still be set up to 16 × 8.
