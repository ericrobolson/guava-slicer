# One-Shot: Overhang Detection
Detect and visually highlight overhang regions on a 3D mesh where the surface angle exceeds a configurable threshold relative to the build plate normal.

**Target dir:** /Users/ericolson/dev/forge/projects/guava-slicer
**Mode:** existing-codebase

## Stage 0: Context & Use Cases

**Who:** FDM 3D printing users preparing models for print.

**Problem:** Without overhang visualization, users import into Bambu Studio blind to problem areas. Guava Slicer shows overhangs in real-time as the user rotates, letting them find the optimal orientation before slicing — or auto-solve it.

**Constraints:**
- Backend owns all geometry math (pure function: mesh in → overhang triangle indices + area out)
- Frontend renders the overlay (Three.js checkerboard pattern on flagged faces)
- IPC via `analyze_overhangs` command; auto-solve via `auto_orient` command
- Overlay updates after transforms complete (debounced, not during active drag)
- Operates on raw triangle normals — no slicing required

**Non-goals:**
- Island detection (Phase 6, layer-based)
- Support placement
- Other mesh formats

**Project type:** application

**Use Cases:**
1. **View overhangs on load** — STL loads → auto-analyze → checkerboard overlay on faces exceeding 45° default
2. **Adjust threshold** — Slider 0°–90° → overlay updates in real-time
3. **Rotate to minimize** — User rotates model → overlay re-computes on mouse release
4. **Toggle overlay** — Show/hide without losing analysis data
5. **Read overhang metrics** — Sidebar shows total overhang area (mm²) and % of total surface area
6. **Auto-solve orientation** — Click "Auto Orient." Preferred axis dropdown defaults to "tilt backward" (rotation around X-axis). Backend samples/optimizes rotations biased toward the preferred axis, finds the orientation minimizing total overhang area, applies it, and streams progress. User can change the preferred axis before running (e.g., tilt forward, tilt left/right, any axis).
7. **Cancel auto-solve** — Abort mid-search, keep current orientation

## Stage 1: User-Facing Interfaces

**Decision: Sidebar panel + viewport overlay**

All overhang controls live in a dedicated sidebar panel alongside the existing UI pattern. The 3D viewport renders a yellow/black checkerboard overlay on overhang faces.

**Panel contents:**
- Angle threshold slider (0°–90°, default 45°)
- Toggle overhang overlay on/off
- Overhang metrics display (area in mm², % of total surface area)
- "Auto Orient" button with preferred axis dropdown (default: tilt backward / X-axis)
- Progress bar for auto-orient (inline, replaces button during operation, with cancel)

**Viewport overlay:**
- Checkerboard pattern (yellow/black) rendered on faces exceeding the threshold angle
- Overlay updates on threshold change and after transform completion (debounced)

Justification: Matches the existing sidebar-driven layout, keeps all controls discoverable in one panel, and avoids introducing floating UI that would break the established interaction pattern.

## Stage 2: Behavior

**Actions:**

| Action | Trigger | Inputs | Effect |
|--------|---------|--------|--------|
| Analyze overhangs | Mesh loaded, transform completed, threshold changed | Mesh vertices/normals/indices, angle threshold | Computes per-face overhang flag + total overhang area. Returns overhang triangle indices + area metric via IPC |
| Update overlay | `analyze_overhangs` response received | Overhang triangle indices | Frontend applies checkerboard material to flagged faces in viewport |
| Toggle overlay | User clicks toggle | Current visibility state | Shows/hides overlay geometry without discarding analysis data |
| Adjust threshold | User moves slider | New angle (0°–90°) | Sends `analyze_overhangs` with new threshold; debounced during drag |
| Auto-orient | User clicks "Auto Orient" | Preferred axis, angle threshold | Backend samples rotations biased toward preferred axis, streams progress, returns best orientation + applies it |
| Cancel auto-orient | User clicks cancel | — | Backend aborts search, keeps current orientation |

**Inputs:** Angle threshold (float, 0–90), preferred axis enum (tilt-back, tilt-forward, tilt-left, tilt-right, any), mesh state from backend.

**Outputs:** Overhang triangle indices (uint32 array), overhang area (float, mm²), total surface area (float, mm²), progress events during auto-orient.

## Stage 3: Data Model

```
OverhangResult
  - triangle_indices: vector<uint32_t>    // indices into mesh face array
  - overhang_area: float                  // mm² of overhang surface
  - total_area: float                     // mm² of total surface
  - threshold_deg: float                  // angle used for this analysis

AutoOrientResult
  - best_rotation: float[4]              // quaternion (x, y, z, w)
  - overhang_area: float                 // mm² at best orientation
  - total_area: float                    // mm² total
  - samples_evaluated: uint32_t          // how many orientations tested
  - threshold_deg: float                 // angle used

PreferredAxis (enum)
  - TiltBack       // +X rotation bias
  - TiltForward    // -X rotation bias
  - TiltLeft       // +Y rotation bias
  - TiltRight      // -Y rotation bias
  - Any            // unconstrained search
```

**Invariants:**
- `overhang_area <= total_area`
- `threshold_deg` in [0, 90]
- `triangle_indices` are valid indices into the current mesh face array
- Auto-orient result rotation is a unit quaternion
- Analysis results are invalidated on any mesh mutation (transform, load)

## Stage 4: Error Handling

Using saved decision for Stage 4: Production-grade with recovery → structured errors at IPC boundary with error codes, no-crash internally. One-sentence justification: overhang analysis can encounter degenerate triangles (zero-area, NaN normals) that must be skipped gracefully rather than crashing the backend.

## Stage 5: Replayability

N/A — inferred from feature type. Overhang analysis is stateless recomputation (pure function of mesh + threshold). Transforms already have undo/redo via the existing command pattern.

## Stage 6: Persistence

N/A — analysis results are ephemeral, recomputed on demand from mesh state + threshold. No persistence needed.

## Stage 7: Tech Stack

Existing codebase. C++17 backend (Zig build), Vue 3 + Electron + Three.js frontend. No new dependencies required — overhang math uses linalg.h already vendored.

### Build & Test Commands
```
build: make -s build -C /Users/ericolson/dev/forge/projects/guava-slicer
test:  make -s test -C /Users/ericolson/dev/forge/projects/guava-slicer
```

## Stage 8: Monitoring & Observability

N/A — inferred from project type (local desktop app, no server).

## Stage 9: Networking

N/A — inferred from project type (single-process with stdio IPC, no network).

## Post-Implementation Summary

**Files created:**
- `backend/src/overhang.h` — OverhangResult, AutoOrientResult, PreferredAxis types + pure function declarations
- `backend/src/overhang.cpp` — overhang analysis, multi-objective auto-orient (overhang area, bottom contact, height, tilt bias), Fibonacci + Euler sweep sampling
- `backend/tests/test_overhang.cpp` — 14 tests (analysis, auto-orient, cancellation, parsing)
- `frontend/src/components/OverhangPanel.vue` — sidebar panel with threshold slider, toggle, metrics, auto-orient axis list with overhang area per axis
- `designs/overhang-analysis.md` — design doc

**Files modified:**
- `backend/src/commands.cpp` — 5 new IPC handlers (analyze_overhangs, auto_orient, cancel_auto_orient, precompute_orientations, apply_orientation) + require_mesh/parse_threshold helpers
- `backend/Makefile` — build with `-Doptimize=ReleaseFast`
- `frontend/src/components/Viewport.vue` — checkerboard overlay shader, shared material
- `frontend/src/components/Sidebar.vue` — sidebar styles moved to container
- `frontend/src/App.vue` — overhang state, IPC wiring, precompute/cache/apply flow, debounced analysis
- `DESIGN.md` — added overhang analysis workflow step + design doc link
- `CLAUDE.md` — added 5 IPC commands
- `ROADMAP.md` — Phase 4 marked done, Phase 8 includes rotation caching

**Refactors applied:**
- Extracted `require_mesh()` and `parse_threshold()` helpers (eliminated 5x duplication)
- Removed dead `place_dy` field from AutoOrientCommand
- Shared ShaderMaterial instance in Viewport (avoid allocation per overlay update)
- Cached centroid Y values in evaluate_orientation pass 1 (avoid double qrot)

**Not done:**
- Format functions not moved to shared utils (single consumer)
- PI/DEG_TO_RAD constants not deduplicated across overhang.cpp and transform.h (trivial)
- AXIS_NAMES/AXIS_VALUES parallel arrays not consolidated with parse_preferred_axis (would need new API)

## Status: finished
