# One-Shot: Island Detection
Detect unsupported regions per layer with per-layer red overlay and severity sparkline on the layer slider.

**Target dir:** /Users/ericolson/dev/forge/projects/guava-slicer
**Mode:** existing-codebase

## Stage 0: Context & Use Cases

**Project type:** application (existing — extends guava-slicer)

**Context:** After slicing a mesh, the user needs to find layers where printed material would have nothing beneath it — "islands." These are regions that would fail during FDM printing without supports. Currently the app slices and shows contours per layer, but doesn't flag which regions are unsupported.

**Scope:**
- Island detection algorithm in C++ backend (connected component labeling, overlap test against layer below)
- Per-layer red overlay on unsupported contour polygons in the layer slider view
- Severity sparkline adjacent to the vertical layer slider (combined count + area score per layer)
- Threaded detection with progress streaming
- **Not in scope:** support generation (Phase 7), 3D mesh heatmap overlay

**Use Cases:**

**UC1: Detect islands after slicing**
Actor: User
Flow:
1. User loads STL, slices mesh (already implemented)
2. Backend auto-detects islands on the cached slice result
3. Frontend receives island data and severity scores per layer
4. Sparkline appears adjacent to layer slider showing severity distribution
Success: User sees at-a-glance which layers have problems
Failure: Detection fails → structured error to frontend, slice view still works without island data

**UC2: Inspect islands per layer**
Actor: User
Flow:
1. User scrubs layer slider (or uses arrow keys)
2. Current layer's contours render as before (green lines)
3. Unsupported contour polygons on current layer render with red fill overlay
4. Sidebar shows island count + total unsupported area for current layer
Success: User can identify exactly which regions need supports
Failure: Layer has no islands → no red overlay, sparkline bar is empty at that layer

**UC3: Re-detect after transform**
Actor: User
Flow:
1. User rotates/repositions model
2. Auto-slice triggers (already implemented)
3. When new slice completes, island detection auto-runs
4. Sparkline and per-layer overlays update
Success: Island data stays in sync with model state

## Stage 1: User-Facing Interfaces

Using saved decision: In-viewport overlay with sidebar controls. Extends existing vertical layer slider with severity sparkline bar and red fill overlay on unsupported contour polygons. Sidebar shows island count + area for current layer.

## Stage 2: Behavior

**Actions:**

| Action | Trigger | Inputs | Effect |
|--------|---------|--------|--------|
| `detect_islands` | Auto after slice completes (or manual IPC call) | Cached `SliceResult` | Produces `IslandResult` — per-layer island count, area, contour indices, severity score |
| `get_island_layer` | Frontend requests on layer change | Layer index | Returns island contour polygons for that layer |
| `cancel_island_detection` | User triggers new slice or transform mid-detection | — | Aborts running detection thread |

**Inputs:**
- Cached `SliceResult` (layer contours from Phase 5)
- No additional user parameters — detection is fully automatic

**Outputs:**
- Per-layer: list of island contour indices, island count, total unsupported area (mm²)
- Per-layer severity score: `count * W_COUNT + normalized_area * W_AREA` (weights TBD, tunable constants)
- Summary: total island count across all layers, worst layer index
- Progress events streamed during detection
- Per-layer island contour point arrays (requested on demand via `get_island_layer`)

**Algorithm:**
1. For each layer N (starting at layer 1 — layer 0 is the build plate, always supported):
   - Extract contour polygons for layer N and layer N-1
   - Convert contours to Clipper2 `PathsD`
   - For each connected component in layer N, compute intersection with the union of layer N-1 polygons
   - If intersection area is zero (or below tolerance), flag that component as an island
2. Compute severity score per layer from island count + total island area
3. Normalize severity scores across all layers (0.0–1.0) for sparkline rendering

## Stage 3: Data Model

**Backend:**
```
struct IslandContour { contour_index, area }
struct IslandLayer { layer_index, islands[], island_count, total_island_area, severity }
struct IslandResult { layers[] (sparse), total_island_count, worst_layer_index, max_severity, severity_scores[] (dense) }
```

**Frontend state:**
- `islandResult` — summary from detection (totalCount, worstLayer, maxSeverity, severityScores[])
- `currentLayerIslands` — fetched on demand per layer via `get_island_layer`
- `islandDetecting` / `islandProgress` — progress tracking

**Invariants:**
- IslandResult invalidated on slice change
- Layer 0 never checked (build plate)
- severity_scores.length == slice layer count
- Island contour indices reference the slice layer's contour vector

## Stage 4: Error Handling

Using saved decision: Production-grade with recovery. Structured JSON errors at IPC boundary; detection failures don't break the slice view.

## Stage 5: Replayability

N/A — inferred from existing undo/redo system (island detection is read-only analysis).

## Stage 6: Persistence

N/A — inferred from ephemeral analysis (results recomputed on demand from cached slice).

## Stage 7: Tech Stack

Using saved decision: Existing Zig + Clipper2 stack. Island detection uses Clipper2 for polygon overlap tests, compiles as part of the backend.

Build: `make -s build`
Test: `make -s test`

## Stage 8: Monitoring

N/A — inferred from local desktop app.

## Stage 9: Networking

N/A — inferred from single-process stdio IPC.

## Post-Implementation Summary

**Files created:**
- `backend/src/island_detection.h` — pure detection algorithm header
- `backend/src/island_detection.cpp` — Clipper2-based island detection implementation
- `backend/src/island_commands.h` — IPC command registration header
- `backend/src/island_commands.cpp` — detect_islands, get_island_layer, cancel IPC handlers
- `backend/tests/test_island_detection.cpp` — 10 test cases for detection algorithm

**Files modified:**
- `backend/src/slicer_commands.h` — added `with_cached_result` accessor
- `backend/src/slicer_commands.cpp` — implemented `with_cached_result`
- `backend/src/commands.cpp` — registered island_commands
- `frontend/src/App.vue` — island state, auto-detect after slice, fetchIslandLayer, clearIslandState
- `frontend/src/components/Viewport.vue` — red island overlay, sparkline canvas, combined watchers
- `frontend/src/components/SlicePanel.vue` — island stats display, clickable worst-layer jump

**Not done (deferred to future phases):**
- 3D mesh heatmap overlay (out of scope per Stage 0)
- Island detection speed tiers (fast/normal/full) — not needed at current scale
- Island progress bar in sidebar (progress data streamed but not displayed)

**Debug log:** [260706-083357-island-detection.debug.md](260706-083357-island-detection.debug.md)

## Status: finished
