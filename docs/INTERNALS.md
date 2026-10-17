# Native code responsibilities

## Resource lifetime

Prefer value members, Qt parents and the existing scoped C handles. Do not wrap
parent-owned widgets in another owner. The top-level `ScopeWindow` is a value
member of `PlayerWindow`; its callbacks are detached before member teardown.
`PlayerTransport` and the page/control widgets use the window's Qt parent tree.
`PlaylistLoader` cancels and joins before its member queues are destroyed.

Audio and scope workers own separate decoder instances. `TrackInfo` shares
immutable resource snapshots, keeping the same bytes available for capture and
cold seeks after files or resource preferences change. GME release destroys the
emulator before tap/note handles, including construction failure. GM declaration
order destroys the synth before settings/resources; the synth owns its loader,
and each open SoundFont cursor retains shared bank bytes until close. MT-32
destroys the synth before ROM wrappers; a wrapper frees its ROM image before its
borrowed-byte `ArrayFile`, while the resource snapshot still exists.

Scene-graph child nodes and textures have different owners. `SceneRoot` and its
overlay nodes explicitly release textures on replacement/destruction. Keep that
cleanup on the existing scene-graph path, including allocation failures.

## Decoder and presentation boundaries

`midimetadata.*` shares MIDI title fallback, the existing sample-rounded two-second
release-tail duration and bounded instrument-name history. Unchanged names still
prune, the state preceding the strict cutoff is retained, and equal-frame lookup
chooses the last state. GM keeps its event-position label sampling; MT-32 keeps
its native-rendered-position sampling. Stable engine-specific voice/source lists,
configuration hashes, resource pointers, masks and roll construction stay in the
backends. Titles/models retain their original trimming policy.
GM reset names synth/settings creation, synchronous in-memory SoundFont loading
and cursor/history reset. The synth owns its registered loader; callback binding
is cleared before reporting load failure. MT-32 reset names synth/ROM opening,
buffer/history reset and filter preparation, preserving destruction and setup
order. GM advances due events and end release before rendering; MT-32 retains
native 32 kHz scheduling and queue-full drain/retry without consuming the rejected event.
GM/MT-32 dispatch, SysEx/reset validation, chase/pedals, effect mixing and resource
admission policies remain engine-specific. Failure checks retain exact errors
and verify that partially constructed synthesizers release their snapshots.

`yamahastream.*` owns Yamaha command events, timed sample blocks and loop/duration
facts parsed from a validated VGM. Sample-block reads and chip lookup share the
existing bounds/errors, bank/instance rules and event limits. Declared duration
remains distinct from the actual stream end: zero preserves unknown metadata.
YamahaTrack keeps chip construction, resampling, captures and the playback cursor;
rewind recreates cores/history and replays the same parsed stream. ROM snapshot
resolution stays before parsing, and metadata/core initialization stays after it.
YamahaTrack's named metadata phase builds tags, declared-duration/loop values,
voices, fixed mix headroom, configuration and optional playlist before resetting
the cores. It retains the previously resolved ROM snapshot in TrackInfo.
`applyVgmTags` in `vgm.cpp` shares Yamaha/POKEY's identical optional GD3 policy:
bounded even UTF-16LE payload, English fields 0/4, trimming and silent fallback.
Each backend still supplies its own filename/system defaults and duration rules.

`vgm.cpp` inspects header bounds, chip clocks, chip combinations and Yamaha extra
tables in named phases, in their existing order. One private little-endian word
reader serves these phases. Version/field bounds, unsupported-list order and
exact malformed-header errors are compatibility boundaries; inspection still
reports unsupported chips separately from `requireSupportedVgm` throwing.
Extra clock/volume tables are inspected only when Yamaha is declared, keeping
relative offsets, instance selection and ordered volume updates unchanged.
The ordered private chip table also owns Yamaha IDs, register-write ranges,
base stereo capability and FM slot counts. `yamahaRegisterCommand` supplies chip/instance/register
bank to the Yamaha playback parser, replacing its duplicate opcode mapping.
Waits, blocks, operand/error checks and command limits remain in the decoder.
Voice construction separates FM labels from native slot/role assembly, retaining
YM2610 gaps, noise/rhythm sharing, OPL3 pairs and PSG/ADPCM indices. Unknown direct
caller IDs keep their prior fallback. Total voice counts still derive from the
same voice list, avoiding a second aggregate-count policy.

`soundconfig.cpp` derives preset identities and ordered hardware roles. Concrete
NES, Sega and Yamaha builders keep format rules separate from the shared source
numbering and identity/opaque-layout finalization. `soundConfiguration` parses
VGM once when needed; GME/POKEY/Yamaha decoders use `vgmSoundConfiguration` with
their already inspected header from the same immutable bytes. Header reuse is
local to construction; factory/backend support checks keep their boundaries.
VGM selection still prefers Yamaha, then POKEY, then Sega. Yamaha identity uses
inspected chips; Sega keeps original raw clock/version fields, including legacy/
variant fallbacks. Identification does not add playback-support validation.
Fixed ID/name/role checks cover standalone and reused-header paths. Exact UTF-8
hash fields/separators/order and source roles are persisted compatibility data;
titles, paths, compression and display labels for verified roles do not affect
them. Unknown layouts retain voice-label-based opaque roles.

`musicfile.cpp` owns bounded content-based file loading/gzip decoding and the
concrete decoder factory. One GME signature predicate serves file admission and
factory routing; routing retains its existing additional four-byte NESM probe.
Keep factory precedence: MT-32/GM snapshots bypass disk, raw SID probes precede
generic loading, then TTT, MIDI, validated VGM Yamaha/POKEY, GME or tracker.
The 64 MiB packed/decoded limits, first-gzip-member/trailing-byte policy and exact
errors remain intact. Raw SID retains its separate reader/limits; compressed SID
does not acquire a new route. Backends still validate their own contents/resources.

`gmetrack.cpp` owns GME decoding/capture. `readMetadata` keeps selected-only/full
queries, title/game/filename/Track NN fallbacks, raw system text, unknown durations
and intro/loop arithmetic together. Each libgme info allocation has a scoped
deleter. Constructor failure and destruction share decoder/taps/notes release;
initialization, capture-scope lifetime, mute restoration and seek order stay the
same. Metadata never uses libgme's invented `play_length` or changes source tags.

`gmevgm.*` contains pure GME boundary and PSG stereo probes using the already
validated VGM data offset. GmeTrack also reads its three clock fields once for
note/tap capability checks. Timing stays conservative for unsupported commands,
truncation and invalid loop targets; stereo keeps its wider skip/marker policy.
Data-block bytes and trailing metadata cannot become pan commands. These probes
do not replace Yamaha/POKEY playback parsers or infer extra format support.

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
Freezing delivered output applies only to pause/seek commands within the existing
30 ms tolerance. Load and unload never retain that frozen output; otherwise an
early unload could leave Full mix history attached to an empty player.

`enginecapture.cpp` groups the public chip/output capture controls and snapshot
readers. Visibility changes clear the whole channel snapshot; mask edits call
`ScopeFrame::retainChannels` to intersect readiness and clear hidden mono/L/R
rows together. Retention preserves metadata, row counts and the original channel
iteration bounds even with shorter/longer physical-side arrays. Equal enabled/mask
values return before revision/reset/wake work. Keep the request mutex and snapshot mutex
as separate phases, with the revision update before snapshot invalidation and
notification after it. FPS only reschedules waiting; chip/output capture remain
independent at this API boundary.

`outputhistory.h` shares enabled-bit and frame-identity predicates among the
callback, pause retention and output reader. Matching compares generation/token
only; the existing enabled/busy gates and live-before-frozen priority stay with
their callers. Token arithmetic, atomics/memory ordering, packed PCM storage,
three-attempt sequence validation and padding remain unchanged. Reading history
still precedes the state lock and its token/busy recheck; freezing still requires
the existing pause proximity check and excludes load/unload.

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

`engine.h` forward-declares the libgme handle; only the GME implementation and
tap integrations include its C API. `playlist.h` declares its Qt/thread/container
dependencies directly. The metadata reader in `playlist.cpp` depends on the
decoder factory; queue and playlist-view declarations do not.

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
request PCM only when their own Levels option is enabled. The window applies this single request
to the existing capture callbacks after committing a layout.

`ScopeCardInspector` owns selected-card controls, contextual help, adaptive
scroll sizing and signal suppression during selection updates. It receives card
options, supported views and display hints, without access to playback, source
PCM, presets or renderer state. Its callbacks report user edits; `ScopeWindow`
applies only the relevant option group and invalidates layout/analysis images
as needed. The inspector never retains a pointer into the model. Card settings reuses it for
live edits with Save/Cancel rollback; a card revision invalidates rollback after a
file or preset replacement. Levels belongs to each card; the old global key is
retained as the default for newly created cards and legacy presets. Optional
Levels fields keep version-1 preset compatibility.

`scopeselection.cpp` owns temporary canvas selection, grid-neighbor navigation and
selected-card actions. It reuses the Cards list's stable ID instead of inventing
a second selection model. The player shortcut filter applies text/dialog/popup
guards; selected-card commands additionally require canvas/list focus. Source
mute/Solo use callbacks into the player, while view/visibility/Levels update only
one card. Selection changes refresh chrome without invalidating the GPU trace
history key or touching audio/capture. Reset, presets and new files clear selection.

`ScopeViewSupport::nextView` supplies the same supported-view cycle for one card
and all copies of a numbered source. `clampScopeSpan` bounds sizes in grid cells
for packing, committed cards, mouse previews and keyboard proposals.
`ScopeWindow::applyCardSize` commits a size through the existing layout/capture/
preset and animation paths. Mouse previews remain uncommitted; a rejected or
outside release settles back to the committed size. Keyboard resizing ignores
unchanged sizes and growth that would displace the selected visible card, while
hidden/overflow cards remain adjustable through the list.

Preset card conversion belongs to `ScopeCards`; `ScopePreset` retains the
versioned INI format, validation and hardware compatibility/adaptation. The window
coordinates grid/label controls, explicit saves and default assignments. Themes,
wave/effect preferences and window geometry remain separate from presets.

The developer runner's `--self-test <fixtures> --layout-only` checks card state
and grid gestures. `--presets-only` checks serialization, hardware transfer and
live application. `--views-only` checks source-cache contracts, pure plot geometry
and partial stereo input alongside the existing live visualization checks.
See [build and check commands](BUILDING.md).
