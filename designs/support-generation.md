# Support Generation Design

Resin-style pillar supports for FDM printing. Supports are a separate mesh rendered in orange/yellow, distinct from the blue model mesh.

## Support Categories

Four categories, each independently toggleable via bitmask:

| Category | Purpose | Sampling Method |
|----------|---------|-----------------|
| Island | Mandatory — unsupported regions that cause print failure | One point at each island contour centroid |
| Reinforcement | Structural — prevents deformation above islands | Ring of 4 points around each island centroid, radius proportional to island area |
| Overhang | Quality — prevents sagging on steep faces | Poisson-disc sampling across overhang triangles |
| Stabilization | Structural — prevents wobble on tall narrow features | Evenly spaced points around the model perimeter, triggered by aspect ratio > 3:1 |

## Pillar Geometry

Each support pillar consists of three sections (bottom to top):

1. **Base flare** — truncated cone from `base_diameter` at Y=0 to `shaft_diameter` over `base_height` mm
2. **Shaft** — cylinder at `shaft_diameter` from base top to tip bottom
3. **Contact tip** — truncated cone from `shaft_diameter` to `tip_diameter`, penetrating `tip_penetration` mm into the model

Default parameters:
- Tip diameter: 0.4 mm
- Tip penetration: 0.2 mm
- Shaft diameter: 1.0 mm
- Base diameter: 4.0 mm
- Base height: 2.0 mm
- Spacing: 3.0 mm (Poisson-disc minimum distance)
- Pillar cross-section: 8 segments

Minimum pillar height: 0.5 mm (shorter supports are skipped).

## Poisson-Disc Sampling

Overhang category uses fast Poisson-disc via spatial grid rejection:
- Grid cell size = spacing
- For each overhang triangle centroid, check 5x5 neighbor cells for conflicts
- If centroid rejected, try one random point on the triangle face
- Deterministic seeding (seed=42) for reproducible results

## Data Flow

```
overhang::analyze() ──→ overhang triangle indices
                                  ↓
island_detection::detect() ──→ island contour centroids
                                  ↓
support_gen::sample_*_points() ──→ SupportPoint[]
                                  ↓
support_gen::generate_pillar() ──→ triangle mesh (vertices, normals, indices)
                                  ↓
IPC: binary frames ──→ Three.js viewport (orange MeshPhongMaterial)
```

## IPC Commands

| Command | Params | Response |
|---------|--------|----------|
| `generate_supports` | `tip_diameter`, `shaft_diameter`, `base_diameter`, `spacing`, `enabled_categories`, `threshold` | Support summary + binary mesh frames |
| `place_support` | `position: [x,y,z]`, `normal: [x,y,z]` | Support summary + binary mesh frames |
| `remove_support` | `support_id: uint32` | Support summary + binary mesh frames |
| `clear_supports` | — | Support summary |
| `cancel_supports` | — | `{cancelled: true}` |
| `get_support_points` | — | Array of `{id, position, category}` |

All mutating commands are undoable via the command pattern.

## Undo/Redo Commands

| Command | Execute | Undo |
|---------|---------|------|
| `GenerateSupportsCommand` | Replace entire support collection | Restore previous collection |
| `PlaceSupportCommand` | Append point, rebuild mesh | Pop point, rebuild mesh |
| `RemoveSupportCommand` | Remove point by ID, rebuild mesh | Re-insert at original index, rebuild mesh |
| `ClearSupportsCommand` | Clear all supports | Restore previous collection |

## Frontend Layout

Two-sidebar layout:
- **Left sidebar** (print-prep workflow): Overhangs → Slice → Islands → Supports
- **Right sidebar** (model info): Mesh info, transform controls

Support panel includes:
- Category toggles with per-category counts
- Parameter sliders (tip, shaft, base diameter, spacing)
- Auto Support button with progress indicator
- Place (P) / Remove (X) mode buttons
- Clear All button

## Future Work (Phase 7b–7d)

- Tree branching (Vanek 2014 algorithm)
- Cross-bracing between pillars
- Raft generation (convex hull of base points)
