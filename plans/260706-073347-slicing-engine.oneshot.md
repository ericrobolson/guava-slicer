# One-Shot: Slicing Engine
Intersect mesh with Z-planes to produce contour polygons per layer, with threaded slicing, vertical layer slider, and 2D layer preview — groundwork for island detection and support generation.

**Target dir:** /Users/ericolson/dev/forge/projects/guava-slicer
**Mode:** existing-codebase

## Stage 0: Context & Use Cases

**Problem:** After loading and orienting an STL model, the user needs to inspect it layer-by-layer to understand where overhangs and unsupported regions will cause print failures. This requires slicing the mesh into horizontal contour polygons and visualizing them in-context within the 3D viewport.

**Project type:** application (existing — extends guava-slicer)

**Constraints:**
- Slicing logic is backend-only (C++). Frontend only visualizes contour data received over IPC.
- Slicing is read-only — no undo/redo needed.
- Must not block the IPC read loop — threaded with progress streaming.
- Contour output is vector polygons, never rasterized bitmaps.
- Clipper2 vendored and available for contour assembly/cleanup.

**Non-goals (this phase):**
- Island detection (Phase 6)
- Support generation (Phase 7)
- Infill patterns, wall offsets, G-code — Bambu Studio handles those
- Variable layer height UI (engine supports it, but UI exposes a single global layer height for now)

**Use Cases:**

**UC1: Auto-slice on mesh change**
- Actor: System
- Trigger: Mesh loaded, transformed, scaled, or re-oriented
- Flow: (1) Backend detects mesh state change. (2) Spawns background slice thread with current layer height. (3) Streams progress events to frontend. (4) Frontend caches slice result. (5) If layer inspection mode is active, updates the viewport live.
- Success: Slice result cached and ready when user enters layer inspection mode.
- Failure: Slice fails (degenerate mesh) — error displayed, no crash.

**UC2: Enter layer inspection mode**
- Actor: User
- Trigger: Clicks "Slice" button
- Flow: (1) If slice result is cached and current, viewport switches to layer inspection mode immediately. (2) If slice is in-progress, show progress bar and switch when done. (3) If no slice has run, trigger one now. (4) Viewport clips geometry above current layer Z. (5) Contour polygon highlighted at current slice plane. (6) Vertical slider appears on right edge of viewport. (7) Layer number and Z height displayed near slider.
- Success: User sees clipped model with highlighted contour at the selected layer.

**UC3: Scrub through layers**
- Actor: User
- Trigger: Drags vertical slider or uses arrow keys
- Flow: (1) Slider position maps to layer index. (2) Viewport clipping plane updates to new Z height. (3) Contour overlay updates to show the new layer's polygon(s). (4) Layer number and Z height label update.
- Success: Real-time scrubbing through layers with immediate visual feedback.

**UC4: Exit layer inspection mode**
- Actor: User
- Trigger: Clicks "Slice" button again (toggle) or presses Escape
- Flow: (1) Clipping plane removed. (2) Full mesh visible again. (3) Slider hidden. (4) Slice result stays cached.

## Stage 1: User-Facing Interfaces

**Decision: In-viewport overlay with sidebar controls**

Vertical layer slider on the right edge of the 3D viewport (tall thin track, draggable handle, layer number + Z height label). "Slice" button and layer height input in the existing right sidebar. Layer inspection mode clips geometry above the current Z via Three.js clipping plane and draws contour lines (LineSegments) at the current layer. Slider spatially aligned with model Z axis. Matches Lychee reference UI.

**UI elements:**
- Right sidebar: "Slice" toggle button, layer height numeric input (default 0.06mm)
- Viewport overlay: vertical slider (right edge), layer number + Z height label
- Viewport rendering: clipping plane at current Z, contour line overlay at slice plane
- Keyboard: Up/Down arrows to step layers, Escape to exit layer inspection mode

## Stage 2: Behavior

**Actions:**

| Action | Trigger | Inputs | Effect |
|--------|---------|--------|--------|
| Slice mesh | Mesh change (load/transform/scale/orient) or manual button | Mesh geometry, layer height | Produces array of layer contours, cached in backend |
| Cancel slice | New mesh change during active slice | — | Aborts current slice thread, starts new one |
| Enter layer inspection | User clicks "Slice" button | — | Enables clipping plane, shows slider, renders contour at current layer |
| Change layer | Slider drag, Up/Down arrows | Layer index | Updates clipping plane Z, swaps contour overlay. Additive — all geometry below the clipping plane is visible, contour highlight drawn at the cut plane |
| Exit layer inspection | "Slice" toggle or Escape | — | Removes clipping plane, hides slider, full mesh visible |
| Change layer height | User edits numeric input | New height (mm) | Invalidates cached slice, triggers background re-slice |

**Viewport behavior in layer inspection mode:**
- Clipping plane at current layer Z — all mesh geometry below is visible, everything above is hidden
- Contour line overlay drawn at the cut plane Z height, highlighting the cross-section outline
- As slider moves up, more model is revealed (additive). Slider at top = full model visible with top-layer contour highlighted.
- Slider at bottom = only the first layer slice visible

**Inputs:**
- Layer height: numeric input, default 0.06mm, range 0.01–1.0mm
- Layer slider: integer layer index, range 0 to (layer_count - 1)
- Keyboard: Up/Down step ±1 layer, Escape exits inspection mode

**Outputs:**
- IPC `slice` response: summary with layer count and height
- IPC `slice` progress events: `{ layer: N, total: M }`
- IPC `get_layer` response: contour polygons for the requested layer
- Frontend rendering: clipping plane position, contour line geometry at cut plane, layer label text

## Stage 3: Data Model

**Backend entities:**

```
struct Contour
  points: vector<Point2D>    // closed polygon, ordered CW or CCW
  is_hole: bool              // true = inner boundary (hole in cross-section)

struct SliceLayer
  z_height: float            // Z coordinate of this layer
  contours: vector<Contour>  // outer boundaries + holes at this Z

struct SliceResult
  layers: vector<SliceLayer>
  layer_height: float        // the height param used to generate this result
  mesh_hash: uint64          // hash of mesh state at slice time (for cache invalidation)
  layer_count: int           // total number of layers

struct SliceParams
  layer_height: float        // mm, default 0.06
  z_min: float               // from mesh bounding box
  z_max: float               // from mesh bounding box
```

**Frontend state:**

```
sliceState:
  is_slicing: bool           // background slice in progress
  slice_progress: {current, total}
  layer_inspection_active: bool
  current_layer_index: int
  layer_count: int
  layer_height: float        // mm
  contours: Point2D[][]      // current layer's contour data for contour highlight rendering
```

**IPC messages:**

```
→ { "cmd": "slice", "id": "...", "params": { "layer_height": 0.06 } }
← { "id": "...", "event": "progress", "data": { "layer": 42, "total": 1200 } }
← { "id": "...", "ok": true, "result": { "layer_count": 1200, "layer_height": 0.06 } }

→ { "cmd": "get_layer", "id": "...", "params": { "layer_index": 42 } }
← { "id": "...", "ok": true, "result": { "z_height": 2.52, "contours": [...] } }
```

**Layer data transfer:** Backend sends a summary on slice completion (layer count, height), then the frontend requests individual layers via `get_layer` as the user scrubs. Only the visible layer's contours are transmitted — the clipping plane and existing mesh geometry handle the additive reveal below.

**Invariants:**
- `SliceResult` is immutable once computed. Any mesh change invalidates it entirely.
- `mesh_hash` is recomputed on every mesh mutation — if it matches the cached result, no re-slice needed.
- Contour winding: outer boundaries are CCW, holes are CW (Clipper2 convention).
- Layer 0 is at `z_min + layer_height/2` (offset from bottom to avoid exact-boundary degenerate intersections).

## Stage 4: Error Handling

**Decision: Production-grade with recovery** (saved decision from _DECISIONS.md)

- Degenerate triangles that produce no intersection or degenerate segments at a Z plane are skipped with a warning count.
- Empty layers (zero contours) are valid, not errors.
- Contour assembly failure (non-manifold mesh, unclosed polygons) returns partial contours + warning rather than failing the entire slice.
- Cancellation (new slice triggered during active slice) is clean — old thread checks cancellation flag between layers.
- IPC error codes: `SLICE_ERROR`, `INVALID_PARAMS` (bad layer height), `NO_MESH` (slice requested with no loaded mesh).

## Stage 5: Replayability — N/A (read-only slicing, no mutations)

## Stage 6: Persistence — N/A (slice result cached in memory, invalidates on mesh change)

## Stage 7: Tech Stack

**Decision: Compile Clipper2 sources directly in build.zig**

Add the 3 Clipper2 `.cpp` files from `vendor/clipper2/src/` as compilation units in the existing `build.zig`. Include path `vendor/clipper2/include/`. Compiles alongside backend sources with the same flags and optimization level. Need to verify `-fno-exceptions` compatibility or provide a shim.

**Build & test commands:**
```
build:   make -s build
test:    make -s test
```

## Stage 8: Monitoring — N/A (desktop app, progress streaming is the observability)

## Stage 9: Networking — N/A (stdio IPC, no network transport)

## Post-Implementation Summary

**Files created:**
- `backend/vendor/clipper2/` — Clipper2 1.4.0 vendored (8 headers, 3 sources, license)
- `backend/src/slicer.h` — Types and API for mesh slicing
- `backend/src/slicer.cpp` — Mesh-Y-plane intersection + contour assembly
- `backend/src/slicer_commands.h` — IPC command registration header
- `backend/src/slicer_commands.cpp` — IPC handlers for `slice` and `get_layer`
- `backend/tests/test_slicer.cpp` — 8 test cases
- `frontend/src/components/SlicePanel.vue` — Sidebar panel (slice button, layer height, progress, info)

**Files modified:**
- `backend/build.zig` — Clipper2 sources + include path added
- `backend/src/commands.cpp` — Registered slicer commands
- `frontend/src/App.vue` — Slice state, auto-slice, IPC calls, keyboard handler
- `frontend/src/components/Viewport.vue` — Clipping plane, contour overlay, vertical slider, arrow keys

**What was NOT done:**
- Clipper2 is vendored but not called by the slicer (it's available for Phase 6 island detection)
- Variable layer height UI — engine supports it, but UI exposes a single global height
- `SliceLayer.z_height` field name is misleading (stores Y value) — deferred rename

**Debug notes:** [260706-073347-slicing-engine.debug.md](260706-073347-slicing-engine.debug.md)

## Status: finished
