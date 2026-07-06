# Undo/Redo & Model Transforms

## Purpose

Command pattern infrastructure for all mutations, combined with model transform tools (rotate, translate, scale). Every transform is an undoable command from creation.

## Architecture

### Command Pattern

Abstract `command::Command` base with `execute()`, `undo()`, `name()`. `command::CommandStack` manages two stacks (undo + redo). Pushing a new command executes it and clears the redo stack. Undo pops from undo, calls undo(), pushes to redo. Redo does the reverse.

### Transform State

`transform::TransformState` stores an ordered list of discrete transform operations (rotate, translate, scale). Each transform command pushes an entry on execute and pops on undo. The composite 4x4 matrix is recomputed from the full list on every mutation (the list is expected to be small — tens of entries, not thousands).

Original mesh vertices are never mutated. The composite matrix is sent to the frontend for rendering and will be applied at export time.

### App State

`app_state::AppState` is a singleton holding the current mesh, command stack, and transform state. IPC handlers access it via `app_state::get()`.

## IPC Commands

### orient_model

Request:
```json
{ "cmd": "orient_model", "id": "...", "params": {
    "type": "rotate|translate|scale|place_on_plate|reset",
    "axis": [1, 0, 0],
    "angle": 90,
    "delta": [0, 0, -5.2],
    "factor": [2, 2, 2]
  }
}
```

Fields depend on type:
- `rotate`: requires `axis` (unit vec3) + `angle` (degrees)
- `translate`: requires `delta` (vec3)
- `scale`: requires `factor` (vec3, all > 0)
- `place_on_plate`: no extra params (auto-computes translation to place bbox.min.z = 0)
- `reset`: no extra params (clears all transforms)

Response:
```json
{ "id": "...", "ok": true, "result": {
    "transform_matrix": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1],
    "bounding_box": { "min": [x,y,z], "max": [x,y,z] },
    "dimensions": [w, h, d],
    "can_undo": true,
    "can_redo": false,
    "undo_name": "Rotate X 90",
    "redo_name": ""
  }
}
```

### undo / redo

Request: `{ "cmd": "undo", "id": "...", "params": {} }`

Response: same shape as orient_model response, plus `action_name` (the name of the undone/redone command) and `has_mesh` (boolean).

Error: `{ "ok": false, "error": { "code": "NOTHING_TO_UNDO", "message": "..." } }`

## Data Flow

1. Frontend sends `orient_model` with transform type + params
2. Backend creates a `TransformCommand`, pushes it (which adds an entry to `TransformState`)
3. Backend recomputes the composite matrix from all active transform entries
4. Backend returns the 16-float column-major matrix + bounding box + undo/redo state
5. Frontend decomposes the matrix and applies to the Three.js mesh object

## Coordinate System

Transforms operate in the original STL coordinate space. The frontend applies a centering offset via a parent group (meshPivot) for display — this is purely visual and not part of the backend transform stack.

The `transform_matrix` is column-major (matching both linalg.h and Three.js conventions). Transforms are left-multiplied: the most recently pushed transform is the outermost in the composition chain.

## Key Decisions

- **Discrete ops over accumulated matrix**: transforms are stored as individual operations, not baked into the vertex data. This makes undo trivial (pop the last entry) and avoids floating-point drift.
- **Session-scoped undo**: the undo stack lives in memory and is cleared on app close. Persistence is deferred to Phase 7.
- **Unbounded stack**: no cap on undo history. The stack size is bounded by user actions per session.
- **load_mesh as undoable command**: `LoadMeshCommand` captures the previous mesh and transform state for undo. Undoing past a load clears the mesh.
