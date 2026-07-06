# One-Shot: Pillar Supports
Resin-style straight pillar support generation — Poisson-disc sampling over overhang faces + island centroids, pillar geometry (contact tip, shaft, base flare), separate support mesh in orange/yellow, click-to-place/remove, auto-generate, all undoable via command pattern.

**Target dir:** /Users/ericolson/dev/forge/projects/guava-slicer/
**Mode:** existing-codebase

## Stage 0: Context & Use Cases

**Project type:** application

### Context

Phase 7a of Guava Slicer — the first support generation phase. The app already detects overhangs (Phase 4) and islands (Phase 6). This phase adds the ability to generate straight pillar support geometry at those locations, rendered as a separate orange/yellow mesh in the viewport.

Support placement uses four categories (Lychee-style):
1. **Islands** — mandatory supports at unsupported region centroids. Print fails without these.
2. **Reinforcement** — additional supports above island regions to prevent deformation from overhanging mass.
3. **Overhangs** — density-based Poisson-disc sampling over overhang faces exceeding the angle threshold.
4. **Stabilization** — supports on tall/narrow features to prevent wobble during printing.

Each category is independently toggleable. All support operations are undoable via the existing command pattern.

**Non-goals for 7a:** tree branching, cross-bracing, raft generation (these are 7b–7d).

### Use Cases

**UC1: Auto-generate supports**
Actor: User with a loaded, sliced model showing islands/overhangs.
1. User clicks "Auto Support" button in sidebar.
2. Backend samples support points across all four categories (islands, reinforcement, overhangs, stabilization).
3. Backend generates pillar geometry (contact tip → shaft → base flare) for each point.
4. Progress streams to frontend.
5. Support mesh appears in viewport in orange/yellow, visually distinct from the blue model.
6. Sidebar shows support count and category breakdown.
**Success:** Supports visible, island count drops on re-detection.
**Failure:** Backend returns error with structured message.

**UC2: Click-to-place individual support**
Actor: User wants a support at a specific location.
1. User enters "place support" mode (button or hotkey).
2. User clicks on the model surface in the viewport.
3. Frontend sends click position + surface normal to backend.
4. Backend generates a single pillar at that position (undoable).
5. Support mesh updates in viewport.
**Success:** New pillar visible at click location.

**UC3: Click-to-remove individual support**
Actor: User wants to remove an unwanted support.
1. User enters "remove support" mode (button or hotkey).
2. User clicks on an existing support pillar in the viewport.
3. Frontend sends support ID to backend.
4. Backend removes the support (undoable).
5. Support mesh updates in viewport.
**Success:** Pillar disappears.

**UC4: Toggle support categories**
Actor: User wants only island supports, not overhang supports.
1. User toggles category checkboxes in sidebar (Islands / Reinforcement / Overhangs / Stabilization).
2. Backend regenerates supports with only enabled categories.
3. Viewport updates.

**UC5: Adjust support parameters**
Actor: User wants thicker supports or tighter spacing.
1. User adjusts parameters in sidebar (tip diameter, shaft diameter, base diameter, spacing density).
2. User clicks "Regenerate" or parameters auto-apply.
3. Backend regenerates with new parameters.

## Stage 1: User-Facing Interfaces

**Decision: Sidebar panel + viewport interaction modes, with new left sidebar for print-prep workflow**

Two-sidebar layout:

**Left sidebar (new) — print-prep workflow (top-to-bottom mirrors the workflow order):**
- Overhang analysis (angle threshold, auto-orient, axis orientations)
- Slice controls (layer height, slice button)
- Island detection results (count, worst-layer jump)
- Support controls: Auto Support button, category toggles (Islands / Reinforcement / Overhangs / Stabilization), parameter sliders (tip diameter, shaft diameter, base diameter, spacing), support count with per-category breakdown
- Place Support (P) and Remove Support (X) mode buttons

**Right sidebar (existing) — model info:**
- Mesh info (vertex/triangle count, dimensions, file size)
- Transform controls (rotate, translate, scale)

**Viewport:**
- Support mesh rendered as separate Three.js mesh in orange/yellow
- Layer slider stays on viewport right edge
- Place/Remove modes: click model surface to add pillar, click existing pillar to remove
- Axis gizmo stays in corner

Justification: Left sidebar groups the entire print-prep workflow in chronological order (overhang → slice → islands → supports), making it a natural top-to-bottom pipeline. Right sidebar keeps model-level info that's relevant at all times. Matches the "discoverability over menus" frontend rule — all controls visible at once.

## Stage 2: Behavior

**Actions:**

| Action | Trigger | Inputs | Effect |
|--------|---------|--------|--------|
| Auto-generate supports | "Auto Support" button | Category toggles, parameter values | Samples support points across enabled categories, generates pillar geometry, streams progress, returns support mesh |
| Place support | Click model surface in Place mode (P) | Click position, surface normal | Creates single pillar at position, adds to support collection |
| Remove support | Click support pillar in Remove mode (X) | Support ID (from raycast hit) | Removes pillar from collection, updates mesh |
| Toggle category | Category checkbox change | Category enum, enabled/disabled | Regenerates supports with updated category set |
| Update parameters | Slider change + Regenerate | Tip/shaft/base diameter, spacing | Regenerates all supports with new params |
| Undo/Redo support op | Cmd+Z / Cmd+Shift+Z | — | Restores/re-applies support state via command pattern |

**Inputs:**
- Click position + surface normal (raycast from viewport)
- Support parameters: tip diameter (mm), shaft diameter (mm), base diameter (mm), spacing (mm)
- Category toggles: islands, reinforcement, overhangs, stabilization (booleans)
- Overhang threshold angle (already exists, reused)

**Outputs:**
- Support mesh: flat float/uint32 arrays (same format as model mesh, sent via binary IPC frame)
- Support metadata: count, per-category breakdown, support point positions + IDs
- Progress events during generation

**Support point sampling per category:**
1. **Islands** — one support at each island centroid (from existing island detection)
2. **Reinforcement** — additional points in a ring around each island, sampled on the overhang face above it
3. **Overhangs** — Poisson-disc sampling across all overhang triangles, density proportional to overhang severity
4. **Stabilization** — detect tall narrow features (high aspect ratio bounding regions), place supports at regular intervals along their height

## Stage 3: Data Model

**Entities:**

```
struct SupportPoint
  position: float3       // contact point on model surface
  normal: float3         // surface normal at contact
  ground_pos: float3     // base position (projected to build plate, Y=0)
  category: Category     // which category generated this point
  id: uint32             // unique ID for click-to-remove

enum Category
  Island
  Reinforcement
  Overhang
  Stabilization

struct SupportParams
  tip_diameter: float    // contact tip sphere diameter (default 0.4mm)
  tip_penetration: float // how deep tip embeds in model (default 0.2mm)
  shaft_diameter: float  // cylindrical shaft diameter (default 1.0mm)
  base_diameter: float   // flared base cone diameter (default 4.0mm)
  base_height: float     // height of the base flare cone (default 2.0mm)
  spacing: float         // Poisson-disc minimum spacing (default 3.0mm)
  enabled_categories: uint8 // bitmask of enabled Category values

struct SupportCollection
  points: vector<SupportPoint>
  params: SupportParams
  mesh_vertices: vector<float>   // generated pillar geometry (flat xyz)
  mesh_indices: vector<uint32>   // triangle indices
  next_id: uint32                // monotonic ID counter

struct AppState (existing, extended)
  + supports: SupportCollection
```

**Invariants:**
- Support IDs are monotonically increasing, never reused within a session
- Support mesh is regenerated from points + params whenever either changes
- `ground_pos.y` is always 0 (build plate)
- Each support point belongs to exactly one category

## Stage 4: Error Handling

**Decision: Production-grade with recovery (auto-applied from _DECISIONS.md)**

Structured JSON errors at the IPC boundary with error codes. If sampling fails on one category, return results from successful categories with a warning. Invalid click positions (off-model, inside mesh) return a structured error. Partial generation is valid — the user sees what succeeded and can retry or adjust.

## Stage 5: Replayability

N/A — existing undo/redo command pattern covers all support operations.

## Stage 6: Persistence

N/A for 7a — support persistence is Phase 8 (Project Persistence).

## Stage 7: Tech Stack

**Decision: Existing stack, no new dependencies (auto-applied from _DECISIONS.md)**

C++17 backend (Zig build), Electron + Vue 3 + Three.js frontend, stdio IPC. Pillar geometry is procedural mesh generation (cylinders, cones, spheres from math). Poisson-disc sampling is a self-contained algorithm. No new vendored libraries needed.

```
build:   make -s build
test:    make -s test
```

## Stage 8: Monitoring & Observability

N/A — desktop application, progress streaming over IPC covers user-facing feedback.

## Stage 9: Networking

N/A — local desktop application, no networking.

## Implementation

### Backend (C++)
- `support_types.h` — SupportPoint, SupportParams, SupportCollection, Category enum with bitmask
- `support_gen.h/cpp` — Poisson-disc sampling (spatial grid), island/reinforcement/overhang/stabilization samplers, pillar mesh generation (base flare + shaft + tip)
- `support_commands.h/cpp` — IPC handlers: generate_supports, place_support, remove_support, clear_supports, cancel_supports, get_support_points. Four undo/redo commands.
- `app_state.h` — Extended with `support::SupportCollection supports` field
- `commands.cpp` — Registered support_commands

### Frontend (Vue 3 + Three.js)
- `SupportPanel.vue` — Category toggles, parameter sliders, Auto Support button, Place/Remove mode buttons, progress, support count
- `App.vue` — Two-sidebar layout (left: overhang → slice → islands → supports; right: mesh info + transforms). Support state, IPC handlers, P/X keybindings
- `Viewport.vue` — Support mesh rendering (orange MeshPhongMaterial), click-to-place via raycast, click-to-remove, support mode indicator

### Documentation
- `designs/support-generation.md` — Full design doc
- `ROADMAP.md` — Phases 7a–7d checked off
- `ARCHITECTURE.md` — Updated directory structure
- `DESIGN.md` — Fixed Z→Y wording, added support-generation design doc link

## Known Issues (to fix next session)

1. **Branches not connecting to model surface** — contact tips exist at island centroid positions but branches from trunks don't visually reach/touch the model. Need to verify branch endpoint coordinates match actual overhangs.
2. **Raft too large** — circular raft extends far beyond model footprint. Should be tighter (convex hull of trunk positions + small margin, not max radius circle).
3. **Trunks/branches too thick** — shaft_diameter 0.8mm is too thick for miniature scale models. Scale geometry relative to model size.
4. **Insufficient branch density** — only 83 contact points after dedup (from 888 islands). Need more branches touching the model at actual unsupported regions.
5. **Contact tip geometry missing** — branches end abruptly rather than tapering to a visible small sphere/cone at the model surface.
6. **Branch angle** — branches are perfectly horizontal from trunk. Should angle slightly upward (30-45°) to approach the underside of overhangs from below, not straight through.
7. **Trunk height** — some trunks extend above the model top. Cap trunk height to model bbox max Y.
