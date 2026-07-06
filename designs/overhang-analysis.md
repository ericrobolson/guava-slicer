# Overhang Analysis & Auto-Orientation

Detect overhanging geometry on a mesh and optionally find the rotation that minimizes total overhang area.

## Overhang Detection

A triangle is an overhang when its surface angle from vertical exceeds a configurable threshold.

**Algorithm:** For each triangle, transform its three vertices by the composite transform matrix, compute the face normal from the cross product of edges in world space, and check:

```
dot(face_normal, UP) < -sin(threshold_rad)
```

Where `UP = [0, 1, 0]` (build direction) and `threshold_rad` is the angle threshold in radians. This flags faces whose surface tilts more than `threshold` degrees away from vertical on the downward side.

**Degenerate handling:** Triangles with near-zero cross product magnitude (area < 1e-10) are skipped silently.

**Output:** A list of overhang triangle indices, plus total overhang area and total surface area in world-space units².

## Auto-Orient

Searches over candidate rotations to find the one minimizing total overhang area.

### Sampling Strategy

**Preferred axis mode** (tilt_back, tilt_forward, tilt_left, tilt_right):
- Primary axis: 0°–360° in 5° steps (72 samples)
- Two secondary axes: ±45° in 10° steps (10 samples each)
- Total coarse: 72 × 10 × 10 = 7,200 candidates

**Any mode:**
- Fibonacci lattice on the unit sphere with 1,000 points
- Each point defines a candidate "up direction"
- Rotation quaternion maps current UP to candidate direction

**Fine refinement** (all modes):
- ±5° around best coarse result in 1° steps on all three axes
- Total: 11 × 11 × 11 = 1,331 candidates

### Multi-Objective Cost Function

Inspired by Tweaker-3 and Bambu Studio's approach, each candidate orientation is scored on four objectives:

```
cost = W_OVERHANG * (overhang_area / total_area)
     - W_BOTTOM   * (bottom_contact_area / total_area)
     + W_HEIGHT   * (print_height / max_dimension)
     - W_TILT     * dot(effective_up, preferred_up)
```

| Term | Weight | Purpose |
|------|--------|---------|
| Overhang area | 1.0 | Minimize faces needing support |
| Bottom contact area | 0.5 | Maximize bed adhesion — prevents upside-down results |
| Print height | 0.15 | Prefer shorter prints (less time, less wobble) |
| Tilt bias | 0.1 | Gentle nudge toward preferred axis (default: 45° backward tilt) |

Bottom contact area: faces within 0.5mm of the lowest point whose normal points mostly downward (dot < -0.7).

### Evaluation

Pre-computes face normals, centroids, and areas once from raw mesh. Each candidate evaluation:
1. Pass 1: rotate centroids to find min/max Y (height bounds)
2. Pass 2: rotate normals for overhang check + rotate centroids for bottom contact check

Coarse phase uses stride-4 subsampling (evaluates every 4th face). Fine phase uses full evaluation.

### Result

The best rotation quaternion, plus the overhang area at that orientation. The `AutoOrientCommand` clears the current transform stack, applies the optimal rotation, and places the mesh on the build plate.

## IPC Commands

### `analyze_overhangs`

```
→ { "cmd": "analyze_overhangs", "id": "...", "params": { "threshold": 45 } }
← [binary frame: uint32[] overhang triangle indices]
← { "id": "...", "ok": true, "result": { "overhang_count": N, "overhang_area": F, "total_area": F, "threshold_deg": F, "binary_follows": true } }
```

Synchronous — runs on the IPC handler thread.

### `auto_orient`

```
→ { "cmd": "auto_orient", "id": "...", "params": { "threshold": 45, "preferred_axis": "tilt_back" } }
← { "id": "...", "event": "progress", "data": { "current": 100, "total": 8531 } }
← { "id": "...", "ok": true, "result": { "transform_matrix": [...], "overhang_area": F, "total_area": F, "samples_evaluated": N, ... } }
```

Threaded — runs on a detached worker thread with progress streaming.

### `cancel_auto_orient`

```
→ { "cmd": "cancel_auto_orient", "id": "...", "params": {} }
← { "id": "...", "ok": true, "result": { "cancelled": true } }
```

Sets an atomic cancellation flag checked by the worker thread.

## Frontend Overlay

Overhang triangles are rendered as a separate `THREE.Mesh` with a procedural checkerboard shader (yellow/dark in object space). The overlay mesh is a child of the main mesh object and uses `polygonOffset` to avoid z-fighting. Overlay visibility is togglable without discarding analysis data.

## Data Flow

```
User loads mesh → auto place_on_plate → analyze_overhangs → overlay renders
User adjusts threshold → debounced analyze_overhangs (300ms) → overlay updates
User transforms model → orient_model → analyze_overhangs → overlay updates
User clicks Auto Orient → auto_orient (threaded) → transform applied → analyze_overhangs → overlay updates
```
