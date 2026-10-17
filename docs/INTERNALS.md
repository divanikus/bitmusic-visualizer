# Native UI responsibilities

`ScopeWindow` coordinates transport snapshots, capture requests, layout gestures
and CPU/GPU presentation. It does not own playback or decode music in painting.

`ScopeCards` owns session card state: stable IDs, source/view options, visibility,
cell spans and active order. Removing a card removes its ID from the active order;
hiding preserves it. Duplicates share a source, audio mutes, theme colors and
analysis, while keeping independent display options. Full mix has a reserved ID
outside the voice mask; extra card IDs start above it. Resize previews pack a
temporary span without changing committed state.

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
