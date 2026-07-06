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
| STL parser | microstl (vendored, MIT, header-only) |
| Linear algebra | linalg.h (vendored, public domain, single header) |
| Float parsing | fast_float (vendored, Apache-2.0/MIT, header-only) |
| State management | TBD during implementation (Pinia or equivalent) |
| Testing (C++) | doctest |
| Build system | Zig (C++ backend), npm (frontend) |
| Dependencies | Vendored, prefer header-only, must build under Zig |

## Workflow

1. **Load STL** — unsupported model, displayed in 3D viewport
2. **Model transforms** — rotate, position on build plate, scale uniform/per-axis
3. **Overhang analysis** — detect and visualize overhangs, auto-orient to minimize overhang area
4. **Set slice parameters** — layer height
5. **Slice** — intersect mesh with Z-planes, produce per-layer contour polygons
6. **Step through layers** — scrub Z-slider, view cross-section at each height
7. **Island detection** — highlight unsupported regions per layer
8. **Place supports** — click to add/remove supports on flagged islands, or auto-generate
9. **Generate raft** — add raft geometry under model and supports
10. **Re-slice and verify** — confirm no remaining islands
11. **Export** — model + supports as separate STLs for Bambu Studio

## Output

The tool exports STL files — it does not drive a printer directly. The exported STLs are loaded into Bambu Studio (or any FDM slicer) for final slicing, G-code generation, and printing. Supports and model are exported as separate meshes so they can be printed at different settings (e.g., model at 0.08mm, supports at 0.16mm).

## Design Documents

Detailed subsystem designs live in `designs/`:

- [IPC Protocol](designs/ipc-protocol.md) — stdio framing format, request/response envelope, binary frames, progress events
- [Undo/Redo & Model Transforms](designs/undo-redo.md) — command pattern, undo/redo stack, transform commands (rotate, translate, scale)
- [Overhang Analysis & Auto-Orientation](designs/overhang-analysis.md) — overhang detection algorithm, auto-orient search, IPC commands, frontend overlay

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for directory structure and dependency flow.
