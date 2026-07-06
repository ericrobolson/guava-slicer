# Slicing Engine

Intersects the mesh with horizontal Y-planes to produce per-layer contour polygons. Y is the vertical axis (build plate at Y=0).

## Algorithm

1. **Transform vertices** — apply the composite transform matrix to all vertices (object → world space).
2. **Compute Y range** — find y_min, y_max of transformed vertices.
3. **Generate layer heights** — first layer at `y_min + layer_height/2`, then evenly spaced by `layer_height`.
4. **Per-layer intersection** — for each triangle, check if it straddles the Y-plane. If so, compute the two intersection points on the crossing edges. Vertices within `PLANE_EPSILON` (1e-6) of the plane are classified as "above" to avoid degenerate edge cases.
5. **Contour assembly** — connect line segments into closed polygons using edge connectivity. Each segment endpoint is tagged with the mesh edge it lies on. For manifold meshes, each crossing edge appears in exactly two segments, enabling chain walking.
6. **Hole classification** — compute signed area of each contour. In the XZ projection (Y-up), positive area = hole, negative area = outer boundary.

## Data Structures

- `Point2D` — (x, y) where x = world X, y = world Z (projected from Y-plane).
- `Contour` — closed polygon (vector of Point2D) + is_hole flag.
- `SliceLayer` — all contours at a single Y height.
- `SliceResult` — all layers + metadata (layer_height, layer_count, warning_count).

## IPC Commands

### `slice`

Request: `{ layer_height: 0.06 }`

Runs on a background thread. Streams progress events `{ layer: N, total: M }`. Returns summary on completion:

```json
{ "layer_count": 1200, "layer_height": 0.06, "warning_count": 0 }
```

Results are cached by mesh hash (vertex/triangle count + transform matrix). A cached result returns immediately with `"cached": true`.

### `get_layer`

Request: `{ layer_index: 42 }`

Returns contour data for a single layer:

```json
{ "z_height": 2.52, "contours": [{ "points": [[x,z], ...], "is_hole": false }], "contour_count": 1 }
```

## Frontend Visualization

- **Layer inspection mode** — toggled by the "Slice" button. Activates a Three.js clipping plane at the current layer Y height, showing all geometry below.
- **Contour overlay** — LineSegments drawn at the clipping plane, highlighting the cross-section outline.
- **Vertical slider** — right edge of the viewport, maps to layer index. Up/Down arrow keys step ±1 layer.
- **Auto-slice** — background slice triggers on mesh load, transform, scale, or orientation change.

## Cancellation

A new slice request sets `s_slice_cancel` (atomic bool) before launching a new worker thread. The running slicer checks this flag between layers and exits early if set.

## Dependencies

- **Clipper2** — vendored but not used by the slicer itself. Available for Phase 6 (island detection) polygon intersection operations.
