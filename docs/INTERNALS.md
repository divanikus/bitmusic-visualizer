# Native code responsibilities

`NativePlayer` retains the public command mailbox, generation/state storage and
independent scope worker. Its private `AudioWorker` in `engineplayback.cpp` is
created inside `run()`, on the existing high-priority thread. It owns the active
decoder, audio sink, render buffer and per-load duration limit. The 3 ms timer,
command order and state publication guards are unchanged. Stop/reset the sink
before resetting the PCM ring. The real-time callback captures only the player
and its generation; it never accesses the decoder, locks or GUI. Shutdown stops
output before joining the separate scope thread.

The worker's timer handler takes one pending command and calls named command,
seek, output-start, playback-advance and metadata phases. One failure boundary
retains unchanged exception text and the existing load-versus-seek invalidation
policy. Generation-only publication guards stay distinct from cancellation,
which also observes shutdown. Output errors share counting while retaining the
different startup/runtime messages. Seek clamping and the published endpoint
share sample-to-millisecond ceiling conversion; ordinary positions still floor.
Keep the existing initial metadata reads under the publication lock and dynamic
reads before it. Cancellation does not introduce a new error or completion event.

The private `ScopeWorker` in `enginescopes.cpp` owns the shared capture decoder,
fallback per-voice decoders/history, resource identity and preparation pool.
It is created and destroyed within the existing normal-priority scope QThread.
Pool size/priority and the semaphore join after each bounded batch are unchanged;
all tasks finish before request-local references or worker storage are destroyed.
Audio decoders and the real-time callback remain independent. Public capture
controls, revision/state storage and published snapshots stay in `NativePlayer`.

Each scope iteration keeps one immutable request containing revision, FPS,
mask, player state and pacing start time. Named phases synchronize source identity,
advance shared taps or ready fallback voices, join a bounded preparation batch,
build a snapshot and publish after rechecking cancellation under the scope mutex.
Both paths share frame dimensions, the capture factory and pacing wait; fallback
pitch rows retain their `{-1}` default, shared rows start empty, and fallback
instruments stay empty. FPS only reschedules waits. Preparing voices skip pacing
between batches, and ready histories keep the existing half-second lag rule.
Error publication keeps its separate generation/revision guard and cache reset;
it never changes primary audio state. A preparation failure is rethrown only
after all scheduled tasks have released the batch semaphore.

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

`MidiControls` owns the MIDI resource row, current MIDI source path, bank list,
selection guards and saved bank/engine choices through the existing resource
helpers. It reports successful option changes to the window, which reloads the
same MIDI from zero. Reselecting the saved option and refreshing banks do not
reload playback. One engine-presentation rule selects both the visible controls
and resource directory, retaining Auto's valid MT-32 snapshot/error fallback.
Folder requests use a fresh player snapshot in the window. Settings refreshes
the same bank list; actual resource validation/snapshots remain in the backends.

`resourcestatus.*` builds the Settings resource catalog without widgets or
playback changes. It checks ROM presence/readability/size and counts SoundFont
filenames, retaining the existing ordering, size bounds and status text.
Directory rows are explicit data rather than inferred from their display label.
These checks never create folders or validate resource contents; backends still
validate ROM identities/models and banks only when opening relevant music.

`PlayerSettingsDialog` owns the three Settings pages, control initialization,
automatic preference saves and resource-table selection/details/folder actions.
It receives current FPS/renderer labels and reports saved limit changes, FPS
requests and resource refreshes through callbacks. PlayerWindow owns reuse/close
and applies those callbacks to playback, scopes and the MIDI bank row. Opening
Settings never applies a live change; a failed preference save never applies a
new playback limit. Renderer changes remain deferred until app restart.

`resourceerror.*` defines the four existing required-resource prefixes and their
warning titles. Backends supply their unchanged details through that text
boundary; the player recognizes only a case-sensitive prefix at the start of
the primary error. Ordinary validation errors remain in status text. The window
retains the existing warning generation gate, plain text, dialog name and lifetime;
dismissing a warning never makes it eligible again until a different generation.

C64 and YM2608 ROM snapshots share a private bounded fixed-size reader in
`romfiles.cpp`. It retains missing/unreadable/exact-size diagnostics, reads only
the expected size plus one byte, and records optional failures without throwing.
Backends still decide when the tune actually needs those bytes. GM/MT-32 keep
their distinct read-size, content and compatibility checks; there is no generic
resource loader or eager validation path.

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
