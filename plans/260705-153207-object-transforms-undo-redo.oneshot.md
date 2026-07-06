# One-Shot: Object Transforms + Undo/Redo
Model manipulation tools (rotate, translate, scale) and command pattern infrastructure, built simultaneously so every transform is undoable from day one.

**Target dir:** /Users/ericolson/dev/forge/projects/guava-slicer
**Mode:** existing-codebase

## Post-Implementation Summary

**Backend — new files:**
- `command.h/cpp` — Command base class, CommandStack (undo/redo)
- `transform.h/cpp` — TransformState (discrete transform ops, composite matrix), label generators, ResetCommand
- `mesh_ops.h/cpp` — Pure functions: centroid, centering, min_transformed_y, rotation/scale around point, center_on_plate_delta
- `app_state.h/cpp` — Global AppState (mesh + command stack + transform state), JSON response builders
- `linalg_types.h` — Shared Vec3/Vec4/Mat4 type aliases

**Backend — modified files:**
- `commands.cpp` — Added orient_model (rotate/translate/scale/place_on_plate/center_on_plate/reset), undo, redo handlers. Refactored load_mesh to center mesh at centroid and wrap as undoable command.

**Frontend — new files:**
- `KeybindingsModal.vue` — Wide 3-column keybindings overlay
- `transformDelta.js` — Pure functions for rotation/translation/scale delta computation
- `transformDelta.test.js` — 14 Jest tests
- `jest.config.js` + `jest-esm-transform.js` — Jest setup with ESM→CJS transform

**Frontend — modified files:**
- `App.vue` — Undo/redo toolbar buttons, Cmd+Z/Cmd+Shift+Z, orient_model IPC, auto place-on-plate after load, keybindings modal, formatError helper
- `Viewport.vue` — Hold G/R/S + drag for translate/rotate/scale, TransformControls gizmo, backend transform matrix application, improved lighting/contrast
- `Sidebar.vue` — Snap rotation buttons, scale input, place on plate, center on plate, reset buttons

**Backend tests:** 62 (8 command, 17 transform, 18 mesh_ops, 19 existing)
**Frontend tests:** 14 (transform delta computation)

**Design docs:**
- `designs/undo-redo.md` — Command pattern, transform state, IPC protocol, coordinate system docs
- `DESIGN.md` — Updated workflow step 2 + added design doc entry

**What was NOT done:**
- Thread safety (AppState mutex for concurrent IPC access)
- Extract handle_orient_model into per-type handler functions
- Extract Viewport.vue camera/gizmo/axis subsystems into composables
- Transform gizmo numeric input (Blender-style type-a-number while transforming)
- Sidebar numeric fields for direct position/rotation/scale entry
- Debug log: `plans/260705-153207-object-transforms-undo-redo.debug.md` (no debug file created — debugging was done inline)

## Status: finished

## Stage 0: Context & Use Cases

**Who it's for:** Users who've loaded an STL and need to orient/scale it before slicing. Every manipulation must be reversible.

**What it solves:** Currently the app can load and display a mesh but can't transform it. There's no undo infrastructure, so adding any mutation without this foundation would be irreversible.

**Constraints:**
- Transforms stored as discrete ops against original mesh vertices — no in-place vertex mutation. The backend composes a transform matrix from the op stack for rendering/export, but original vertices are preserved.
- Undo stack is unbounded but session-scoped — cleared on app close, not persisted to project files (persistence is Phase 7).
- `load_mesh` is retroactively wrapped as a command (undoing it clears the mesh).
- All transform math happens backend-side (linalg.h). Frontend sends commands and receives updated transform matrix for Three.js.

**Non-goals:**
- Multi-object scenes (one mesh at a time for now).
- Transform gizmo interaction logic in the backend — the frontend handles gizmo UX and sends resulting transform values over IPC.
- Persisting undo history across sessions.

**Project type:** application

**Use Cases:**

1. **Rotate model to print orientation**
   - Actor: User
   - Flow: (1) Load STL → (2) Click rotate gizmo or press hotkey → (3) Frontend sends `orient_model` with rotation params → (4) Backend pushes RotateCommand, computes new composite matrix, returns it → (5) Frontend applies matrix to Three.js mesh
   - Success: Model visually rotated, undo stack has one entry
   - Failure: Invalid rotation params → backend returns error, no state change

2. **Snap rotate 90°**
   - Actor: User
   - Flow: (1) Press axis-snap button (e.g., "Rotate X +90°") → (2) Frontend sends `orient_model` with axis + 90° → (3) Backend pushes command, returns matrix
   - Success: Clean 90° rotation applied
   - Failure: N/A (fixed values)

3. **Translate / place on build plate**
   - Actor: User
   - Flow: (1) Drag translate gizmo or click "Place on build plate" → (2) Frontend sends `orient_model` with translation → (3) Backend pushes TranslateCommand, recomputes bounding box, returns matrix + updated bounds
   - Success: Model repositioned, bottom face at Z=0 for place-on-plate
   - Failure: N/A

4. **Scale model**
   - Actor: User
   - Flow: (1) Enter scale factor (uniform or per-axis) → (2) Frontend sends `orient_model` with scale params → (3) Backend pushes ScaleCommand, returns matrix + updated dimensions
   - Success: Mesh info sidebar reflects new dimensions
   - Failure: Scale factor ≤ 0 → backend returns error

5. **Undo / Redo**
   - Actor: User
   - Flow: (1) Press Ctrl+Z → (2) Frontend sends `undo` → (3) Backend pops command, recomputes composite matrix from remaining stack, returns matrix → (4) Frontend updates viewport
   - Success: Last transform removed, redo stack has one entry
   - Failure: Nothing to undo → backend returns error or no-op

6. **Reset to original**
   - Actor: User
   - Flow: (1) Click "Reset orientation" → (2) Frontend sends a reset command → (3) Backend clears transform stack (or pushes a ResetCommand that undoes to identity), returns identity matrix
   - Success: Model back to original load orientation

7. **Undo past load_mesh**
   - Actor: User
   - Flow: (1) Load a mesh → (2) Apply transforms → (3) Undo all → (4) Undo the load → (5) Backend clears mesh, returns empty state
   - Success: Viewport shows no mesh, as if freshly launched

## Stage 1: User-Facing Interfaces

**Decision: Blender-style gizmo + hotkeys + sidebar**

Interface type: GUI (Electron + Vue 3 + Three.js viewport) with C++ backend over stdio IPC.

**Transform interaction model (Blender UX):**
- **G/R/S hotkeys** activate grab (translate), rotate, scale modes
- **Axis constraint** by pressing X/Y/Z after mode activation (e.g., R → X = rotate around X)
- **Shift+axis** constrains to the remaining two axes (e.g., Shift+Z = constrain to XY plane)
- **Numeric input** while in a mode for precise values (e.g., R → X → 90 → Enter = rotate 90° around X)
- **Mouse drag** for freeform transform in the active mode
- **Enter or left-click** confirms the transform
- **Right-click or Esc** cancels mid-transform (no IPC command sent)
- **Transform gizmo** visible in viewport as visual affordance, clickable/draggable
- Sidebar displays current transform values (rotation, position, scale) — editable for precise numeric entry

**IPC interaction:**
- Frontend handles all modal interaction (key state, mouse drag, axis constraint, visual preview) locally
- On confirm: frontend sends a single IPC command (`orient_model`) with the final transform type + values
- On cancel: nothing sent to backend
- Backend pushes the command onto the undo stack, computes composite matrix, returns it
- Frontend applies the returned matrix to the Three.js mesh

**Additional controls:**
- "Place on build plate" button — translates model so bounding box min Z = 0
- "Reset orientation" button — clears all transforms (or pushes a reset command)
- Snap rotation buttons for quick 90° increments per axis
- Ctrl+Z / Ctrl+Shift+Z for undo/redo (sent as IPC `undo`/`redo` commands)
- Undo/redo buttons in toolbar with enabled/disabled state and action name tooltip

## Stage 2: Behavior

**Actions:**

| Action | Trigger | Inputs | Effect |
|--------|---------|--------|--------|
| Rotate model | R hotkey, gizmo drag, sidebar input | axis (X/Y/Z/free), angle (degrees) | Push RotateCommand, recompute composite matrix, return to frontend |
| Translate model | G hotkey, gizmo drag, sidebar input | axis constraint, delta (vec3) | Push TranslateCommand, recompute composite + bounding box, return |
| Scale model | S hotkey, gizmo drag, sidebar input | uniform factor or per-axis vec3 | Push ScaleCommand, recompute composite + dimensions, return |
| Place on build plate | Button click | none (computed from current bbox) | Push TranslateCommand with delta to make bbox.min.z = 0 |
| Reset orientation | Button click | none | Push ResetCommand that stores current stack, replaces with identity |
| Undo | Ctrl+Z, button | none | Pop from undo stack, push to redo stack, recompute matrix |
| Redo | Ctrl+Shift+Z, button | none | Pop from redo stack, push to undo stack, recompute matrix |
| Load mesh (wrapped) | File open/drag-drop | file path | Push LoadMeshCommand (undo clears mesh + transform stack) |

**Inputs:**
- Keyboard: G, R, S (mode activation); X, Y, Z (axis constraint); Shift+axis (plane constraint); numeric digits + Enter (precise input); Escape/right-click (cancel); Ctrl+Z, Ctrl+Shift+Z (undo/redo)
- Mouse: gizmo drag (translate/rotate/scale depending on active mode), left-click (confirm)
- Sidebar: numeric fields for position X/Y/Z, rotation X/Y/Z, scale X/Y/Z
- Buttons: Place on Build Plate, Reset Orientation, snap-rotate ±90° per axis, Undo, Redo

**Outputs:**
- IPC responses: `orient_model` response contains `{transform_matrix: float[16], bounding_box: {min, max}, dimensions: {x, y, z}}`
- IPC responses: `undo`/`redo` response contains same shape (updated state after undo/redo), plus `{action_name: string}` for UI display
- IPC responses: `undo`/`redo` when stack is empty returns `{ok: false, error: {code: "NOTHING_TO_UNDO"}}` or `"NOTHING_TO_REDO"`
- Frontend state: undo/redo button enabled/disabled state, action name tooltips, sidebar values update

## Stage 3: Data Model

**Entities:**

**Command** (abstract base, backend)
- `name: string` — human-readable action name (e.g., "Rotate X 90°")
- `execute() -> void` — apply the mutation
- `undo() -> void` — reverse the mutation

**RotateCommand : Command**
- `axis: Vec3` — rotation axis (unit vector)
- `angle_degrees: float` — rotation angle

**TranslateCommand : Command**
- `delta: Vec3` — translation offset

**ScaleCommand : Command**
- `factor: Vec3` — per-axis scale factors (uniform = all three equal)

**ResetCommand : Command**
- `saved_stack: vector<unique_ptr<Command>>` — snapshot of the transform stack before reset (undo restores it)

**LoadMeshCommand : Command**
- `mesh: Mesh` — the loaded mesh data
- `path: string` — source file path
- `previous_mesh: optional<Mesh>` — mesh before this load (for undo; null if first load)
- `previous_stack: vector<unique_ptr<Command>>` — transform stack before this load

**CommandStack** (backend, singleton per session)
- `undo_stack: vector<unique_ptr<Command>>` — executed commands
- `redo_stack: vector<unique_ptr<Command>>` — undone commands (cleared on any new command push)
- `push(cmd)` — execute and push to undo_stack, clear redo_stack
- `undo()` — pop from undo_stack, call undo(), push to redo_stack
- `redo()` — pop from redo_stack, call execute(), push to undo_stack
- `can_undo() -> bool`
- `can_redo() -> bool`
- `last_action_name() -> string`

**TransformState** (backend, lives alongside the Mesh)
- `commands: vector<TransformCommand*>` — ordered list of active transform ops (subset of undo_stack, only transform commands)
- `composite_matrix() -> mat4` — compose all active transforms into a single 4x4 matrix (identity if empty)
- `transformed_bounding_box() -> BoundingBox` — apply composite matrix to original mesh bbox

**Invariants:**
- Original mesh vertices are never mutated — transforms compose a matrix applied at render/export time
- Pushing any new command clears the redo stack
- ResetCommand and LoadMeshCommand capture the full transform stack for undo
- The composite matrix is recomputed on every push/undo/redo (not cached — transform count will be small)
- Scale factors must be > 0 (validated at IPC boundary)
- `orient_model` IPC command handles all three transform types via a `type` discriminator field

## Stage 4: Error Handling

**Decision (auto-applied from `_DECISIONS.md`): Production-grade with recovery.**

Structured JSON errors at the IPC boundary with error codes (`INVALID_PARAMS`, `NOTHING_TO_UNDO`, `NOTHING_TO_REDO`) and human-readable messages. Internal code never crashes — invalid transforms (e.g., scale ≤ 0) are caught at the IPC boundary and returned as errors. Undo/redo on empty stacks return structured errors, not crashes.

## Stage 5: Replayability

N/A — undo/redo is the replayability mechanism, already covered in Stage 3.

## Stage 6: Persistence

N/A — undo stack is session-scoped by design. Persistence is deferred to Phase 7.

## Stage 7: Tech Stack

Extending existing stack, no new dependencies.

- C++17 backend (Zig build, `-fno-exceptions -fno-rtti`)
- Electron + Vue 3 + Three.js frontend
- linalg.h for transform math (already vendored)
- doctest for tests

### Build & Test Commands

```
build:   make -s -C /Users/ericolson/dev/forge/projects/guava-slicer build
test:    make -s -C /Users/ericolson/dev/forge/projects/guava-slicer test
```

## Stage 8: Monitoring & Observability

N/A — desktop application, no monitoring infrastructure needed.

## Stage 9: Networking

N/A — all communication is local stdio IPC, no network changes.
