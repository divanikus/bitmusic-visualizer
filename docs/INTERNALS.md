# Native UI responsibilities

The UI headers follow ownership: `playerwindow.h` includes `scopewindow.h`
because the player owns that window; scope code has no dependency on the player
or playlist UI. `timeline.h` contains the transport slider and time label helper.
Implementation files include the header they use, without an umbrella window header.

`PlaylistView` owns playlist painting, source-group hit regions, selection, drag
and drop, and row presentation. Named data roles retain their existing numeric
values. Rebuilds preserve selection by entry ID; the playing entry is independent
of the selected row. `PlayerWindow` coordinates import and playback, while
`Playlist` owns entry identities, ordering, removal and shuffle history, and
`PlaylistLoader` reads metadata asynchronously. The view never changes the queue
or calls the audio engine.

Playlist polling applies loader results before reading the audio state, then
expands a pending single file and updates entry metadata before handling natural
completion. A pending single file carries its path and generation together;
new file/navigation/replacement actions discard it at the existing cancellation
points. Completed appends stay after the expanded subsongs. Keep this order and
the generation-scoped completion check so an old EOF cannot override user Next.

`PlayerTransport` builds the existing timeline, clock, output switches, playback
buttons and volume controls directly in the window layout. It presents audio
and read-only playlist state, blocks feedback while synchronizing controls, and
reports user requests through callbacks. It owns the unknown-duration slider
range and volume-mute restoration. The window retains track selection and engine
commands: Play may start a selected paused entry, whereas F7 resumes the actual
current entry. The shared vector-icon implementation stays in `playercontrols.*`.

`PlayerChannels` owns the Channels page, button painting and snapshot presentation.
One label/tooltip path handles initial and changing instrument names. Snapshot
updates block mute feedback and keep source IDs/buttons stable; rebuilds still
occur only at the window's existing file/channel-count boundaries. The page
reports mute, Solo and all-channel requests; PlayerWindow applies them to the
current engine state. Source masks and saved card/theme/preset identities stay
independent of channel labels.

`ScopeWindow` coordinates transport snapshots, capture requests, layout gestures
and CPU/GPU presentation. It does not own playback or decode music in painting.

`ScopeAnalysis` owns waveform trigger/envelope state, FFT bins and estimated
pitch caches per source, including Full mix. Generation, position, output sequence
and stereo display identify snapshots. Waveforms preserve history within a
stream; spectra update for a changed key; pitch keeps its existing ~30 Hz cadence
and bypasses the throttle on rewind/generation changes. File/card-reset/preset/visibility
resets clear all analysis; wave controls/stereo reset waves only. Painting and
source snapshot selection remain in the window; PCM is never modified.

Waveform points and spectrum line/bar geometry are pure helpers beside the
presentation algorithms in `scopewave.*` and `spectrum.*`. Spectrum axes and
traces share lane bounds; line/bar styles share logarithmic FFT-band peak
selection. Building geometry does not advance analysis or alter PCM. Both CPU
and GPU consume the resulting lanes with the existing point limits and margins.

The window's source-sample accessor borrows the same mono/physical L/R buffers
for traces, meters and estimated pitch. Missing meter sides fall back to mono;
pitch retains its existing empty-right/missing-left-mono rule, independently of
the Stereo display preference. Source/output dimming conditions are shared by
card labels, traces, meters and musical events; their existing alpha values stay
with each painter. Readiness checks still gate all source-buffer access.

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
live application. `--views-only` checks source-cache contracts, pure plot geometry
and partial stereo input alongside the existing live visualization checks.
See [build and check commands](BUILDING.md).
