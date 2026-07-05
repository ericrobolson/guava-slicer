# Guava Slicer — Architecture

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
guava-slicer/
├── ARCHITECTURE.md
├── CLAUDE.md
├── DESIGN.md
├── LICENSE
├── Makefile              # Top-level: wraps backend + frontend builds
├── README.md
├── ROADMAP.md
├── designs/
│   └── ipc-protocol.md   # IPC protocol design doc
├── plans/                 # One-shot plan files
├── backend/
│   ├── build.zig          # Zig build: exe, tests, cross-compilation
│   ├── Makefile           # Wraps zig build targets
│   ├── src/
│   │   ├── main.cpp       # Entry point
│   │   ├── ipc.h          # IPC protocol: framing, send/receive, command dispatch
│   │   ├── ipc.cpp
│   │   ├── commands.h     # Command handler registration
│   │   └── commands.cpp   # ping, simulate, get_binary handlers
│   ├── tests/
│   │   ├── main.cpp       # doctest entry point
│   │   └── test_ipc.cpp   # IPC message shape + framing tests
│   └── vendor/
│       └── nlohmann/
│           └── json.hpp   # nlohmann/json v3.11.3 (single header)
└── frontend/
    ├── package.json
    ├── vite.config.js
    ├── index.html
    ├── electron/
    │   ├── main.cjs       # Electron main process: spawns backend, frame parser
    │   ├── preload.cjs    # Context bridge: exposes IPC to renderer
    │   └── dev.cjs        # Dev launcher: starts Vite then Electron
    └── src/
        ├── main.js        # Vue app entry
        └── App.vue        # IPC Console UI: buttons, log, progress bar
```

## Dependency Flow

```
frontend/src/App.vue
  → window.electronIPC (preload.cjs context bridge)
    → ipcMain/ipcRenderer (Electron IPC)
      → electron/main.cjs (frame parser, child_process)
        → backend/zig-out/bin/guava-slicer-backend (stdio)
          → ipc.cpp (command dispatch)
            → commands.cpp (handler logic)
```
