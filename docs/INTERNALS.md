# Native UI responsibilities

The UI headers follow ownership: `playerwindow.h` includes `scopewindow.h`
because the player owns that window; scope code has no dependency on the player
or playlist UI. `timeline.h` contains the transport slider and time label helper.
Implementation files include the header they use, without an umbrella window header.

`ScopeWindow` coordinates transport snapshots, capture requests, layout gestures
and CPU/GPU presentation. It does not own playback or decode music in painting.

`ScopeAnalysis` owns waveform trigger/envelope state, FFT bins and estimated
pitch caches per source, including Full mix. Generation, position, output sequence
and stereo display identify snapshots. Waveforms preserve history within a
stream; spectra update for a changed key; pitch keeps its existing ~30 Hz cadence
and bypasses the throttle on rewind/generation changes. File/layout/visibility
resets clear all analysis; wave controls/stereo reset waves only. Painting and
source snapshot selection remain in the window; PCM is never modified.

`ScopeCards` owns session card state: stable IDs, source/view options, visibility,
cell spans and active order. Removing a card removes its ID from the active order;
hiding preserves it. Duplicates share a source, audio mutes, theme colors and
analysis, while keeping independent display options. Full mix has a reserved ID
outside the voice mask; extra card IDs start above it. Resize previews pack a
temporary span without changing committed state. Capture policy unions the
sources of packed cards and requests Full mix separately. Piano roll/Pattern
request PCM only when Levels is enabled. The window applies this single request
to the existing capture callbacks after committing a layout.

`ScopeCardInspector` owns selected-card controls, contextual help, adaptive
scroll sizing and signal suppression during selection updates. It receives card
options, supported views and display hints, without access to playback, source
PCM, presets or renderer state. Its callbacks report user edits; `ScopeWindow`
applies only the relevant option group and invalidates layout/analysis images
as needed. The inspector never retains a pointer into the model.

Preset card conversion belongs to `ScopeCards`; `ScopePreset` retains the
versioned INI format, validation and hardware compatibility/adaptation. The window
coordinates grid/label controls, explicit saves and default assignments. Themes,
wave/effect preferences and window geometry remain separate from presets.

The developer runner's `--self-test <fixtures> --layout-only` checks card state
and grid gestures. `--presets-only` checks serialization, hardware transfer and
live application. See [build and check commands](BUILDING.md).
