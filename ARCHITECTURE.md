# Guava Slicer — Architecture

> To be fully populated during Phase 1 spikes. This document captures the high-level split and will be updated with directory structure, data flow, and module details as architecture decisions are made.

## High-Level Split

```
C++ Backend (Zig-built binary, runs as child process)
  ├─> IPC Layer (stdio JSON + length-prefixed binary framing)
  ├─> Command System (execute/undo/redo stack)
  ├─> Mesh I/O (STL binary + ASCII load/save)
  ├─> Slicer (mesh-plane intersection → contour polygons per layer)
  ├─> Island Detector (connected components, layer-to-layer overlap)
  ├─> Support Generator (tree/column supports, FDM-compatible sizing)
  ├─> Raft Generator (raft geometry under model + supports)
  ├─> Project Persistence (save/load project state)
  └─> Thread Pool (async compute for slicing, detection, generation)

Electron Frontend (Vue 3 + Three.js, spawns backend via child_process)
  ├─> IPC Client (stdio JSON + binary frame protocol, UUID request tracking)
  ├─> 3D Viewport (Three.js — model display, support editing, orbit camera)
  ├─> Layer Preview (2D contour display per Z-height, island highlighting)
  ├─> Settings Panels (Vue — slice params, support params, raft settings)
  ├─> Support Tools (place, remove, auto-generate)
  ├─> Project Management (save, load, recent files)
  └─> Undo/Redo UI (Ctrl+Z/Ctrl+Y → IPC undo/redo commands)
```

## IPC Data Flow

```
Frontend                          Backend
   │                                 │
   ├─ { cmd, id, params } ────────> │
   │                                 ├─> parse command
   │                                 ├─> execute (push to undo stack)
   │                                 ├─> compute (thread pool if heavy)
   │  <──── { id, event: progress } ─┤  (streaming, optional)
   │  <──── { id, ok, result } ──────┤  (terminal response)
   │                                 │
   ├─ { cmd: "undo", id } ────────> │
   │                                 ├─> pop undo stack, execute undo
   │  <──── { id, ok, result } ──────┤
   │                                 │
```

## Directory Structure

```
TBD — will be populated during Phase 1 spikes.

Expected shape:
backend/
  src/           # C++ source
  vendor/        # vendored dependencies
  tests/         # doctest tests
  build.zig      # Zig build file
frontend/
  src/           # Vue 3 + Three.js
  package.json   # npm config
  electron/      # Electron main process
```
