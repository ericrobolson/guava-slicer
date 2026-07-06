# Debugging Notes — Island Detection

## QA: Sparkline not visible
- Canvas had `v-if` gated on `severityScores` which raced with the ref binding
- Fixed: always render canvas when slider is visible, size from `getBoundingClientRect()`
- Added visible background color so empty sparkline bar is still apparent

## QA: Worst layer clickable
- Added `jump-to-layer` emit from SlicePanel → App.vue `onSliceLayerChange`

## Refactor: Dead code removed
- `s_detecting` atomic in island_commands.cpp — written but never read
- `islandProgress` ref in App.vue — tracked but never displayed

## Refactor: RawLayerData eliminated
- Intermediate struct duplicated IslandLayer fields; now collects directly into IslandLayer

## Refactor: Combined watchers
- Merged sliceContours + islandContourIndices watchers to avoid double overlay rebuild

## Refactor: Clear stale island state
- Added `clearIslandState()` call at start of `triggerSlice()` to prevent stale overlays during re-detection

## Structural: island_count field removed
- `IslandLayer::island_count` was redundant with `islands.size()` — removed field, use vector size

## Structural: Simplified JSON serialization
- Replaced manual severity scores loop with nlohmann direct vector constructor

## Structural: Negative layer_index guard
- Added explicit check for negative layer_index in `handle_get_island_layer`

## Structural: Data race fix
- Read response fields from `result` before `std::move` into `s_cached_result`
