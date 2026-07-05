# Guava Slicer — Design Overview

Open-source support preparation tool for FDM printing. Load STL models, slice layer-by-layer to find islands, place supports (manual or auto-generated), generate rafts, and export model + supports as separate STLs for use in Bambu Studio or other FDM slicers.

## Core Pillars

1. **Layer-by-layer inspection** — step through sliced layers, visualize cross-section contours, identify unsupported regions before printing
2. **Island detection & repair** — automatic detection of unsupported regions (connected components with no overlap to layer below), manual and auto support placement to fix them
3. **FDM-compatible support generation** — generate resin-style tree/column supports sized for FDM printing, with raft generation
4. **Non-blocking UI** — async backend with threading for compute-heavy operations, frontend never stalls
5. **Undo/redo everything** — command pattern on all mutations, full undo/redo stack

## Tech Stack

| Component | Choice |
|-----------|--------|
| Backend | C++17 (Zig build) |
| Frontend | Electron + Vue 3 + Three.js |
| IPC | child_process stdio (JSON + length-prefixed binary) |
| Mesh I/O | STL (binary + ASCII) |
| 3D viewport | Three.js |
| UI framework | Vue 3 |
| State management | TBD during implementation (Pinia or equivalent) |
| Testing (C++) | doctest |
| Build system | Zig (C++ backend), npm (frontend) |
| Dependencies | Vendored, prefer header-only, must build under Zig |

## Workflow

1. **Load STL** — unsupported model, displayed in 3D viewport
2. **Orient model** — rotate and position on build plate
3. **Set slice parameters** — layer height
4. **Slice** — intersect mesh with Z-planes, produce per-layer contour polygons
5. **Step through layers** — scrub Z-slider, view cross-section at each height
6. **Island detection** — highlight unsupported regions per layer
7. **Place supports** — click to add/remove supports on flagged islands, or auto-generate
8. **Generate raft** — add raft geometry under model and supports
9. **Re-slice and verify** — confirm no remaining islands
10. **Export** — model + supports as separate STLs for Bambu Studio

## Output

The tool exports STL files — it does not drive a printer directly. The exported STLs are loaded into Bambu Studio (or any FDM slicer) for final slicing, G-code generation, and printing. Supports and model are exported as separate meshes so they can be printed at different settings (e.g., model at 0.08mm, supports at 0.16mm).

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for directory structure and dependency flow.
