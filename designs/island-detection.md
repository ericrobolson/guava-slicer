# Island Detection

Detect unsupported contour regions across slice layers. An "island" is a contour polygon in layer N that has zero overlap with the union of layer N-1's polygons — material that would print over empty space.

## Algorithm

1. Build the Clipper2 polygon union of layer 0 (build plate — always supported).
2. For each layer N from 1 to layer_count-1:
   - Build the union of layer N's non-hole contours.
   - For each non-hole contour in layer N, compute the Clipper2 intersection with layer N-1's union.
   - If intersection area < `SUPPORT_AREA_TOLERANCE` (0.001 mm²), flag as island.
3. Compute severity scores: `0.4 * count_norm + 0.6 * area_norm`, normalized across all layers with islands.

## Data Flow

```
SliceResult (cached) → detect() → IslandResult
                                     ├── severity_scores[] (dense, one per layer)
                                     └── layers[] (sparse, only layers with islands)
                                           └── islands[] → {contour_index, area}
```

## IPC Commands

- `detect_islands` — runs detection on background thread, streams progress, returns summary + severity scores
- `get_island_layer` — returns island contour indices and area for a single layer (on-demand)
- `cancel_island_detection` — sets cancellation flag

## Frontend Visualization

- **Red fill overlay**: island contours rendered as `THREE.Mesh` with red semi-transparent fill at the slice plane Y height, renderOrder 1000 (above contour lines at 999).
- **Severity sparkline**: canvas element adjacent to the vertical layer slider. Bars colored from orange (low) to red (high), width proportional to normalized severity.
- **Sidebar stats**: total island count, current layer island count + area, clickable worst-layer link.

## Auto-Trigger

Island detection runs automatically after every slice completes (follows the background auto-calculation decision). Stale island state is cleared at the start of each new slice. Results are invalidated whenever the slice result changes.

## Key Decisions

- **Clipper2 FillRule::NonZero** — handles both CW and CCW winding from the slicer.
- **Sparse layer storage** — only layers with islands are stored in `IslandResult.layers`; severity scores array is dense for O(1) sparkline rendering.
- **No `island_count` field** — use `islands.size()` to avoid sync hazard.
- **Layer 0 always supported** — sits on the build plate by definition.
