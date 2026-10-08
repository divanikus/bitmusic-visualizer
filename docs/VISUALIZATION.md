# Waveform controls

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

Renderer, effects, waveform controls (including frame rate) and Full mix visibility are saved separately
from color themes. Grid/card layout remains session-local. A theme contains 32
voice palettes, Full mix colors and the window background. Colors stay with their
original channel when cards move; a newly opened file assigns palettes by channel
number. Apply to all fills unused palettes too.
