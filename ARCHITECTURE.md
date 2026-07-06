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
│   ├── ipc-protocol.md        # IPC protocol design doc
│   ├── undo-redo.md           # Command pattern and transform commands
│   ├── overhang-analysis.md   # Overhang detection and auto-orientation
│   ├── slicing-engine.md      # Mesh-plane intersection and contour assembly
│   ├── island-detection.md    # Unsupported region detection
│   └── support-generation.md  # Pillar supports, Poisson-disc sampling, categories
├── plans/                 # One-shot plan files
├── backend/
│   ├── build.zig          # Zig build: exe, tests, cross-compilation
│   ├── Makefile           # Wraps zig build targets
│   ├── src/
│   │   ├── main.cpp       # Entry point
│   │   ├── ipc.h          # IPC protocol: framing, send/receive, command dispatch
│   │   ├── ipc.cpp
│   │   ├── commands.h          # Command handler registration
│   │   ├── commands.cpp        # Core handlers: ping, load_mesh, orient, overhang, undo/redo
│   │   ├── slicer.h/cpp        # Mesh-plane intersection, contour assembly
│   │   ├── slicer_commands.h/cpp # IPC: slice, get_layer, cancel
│   │   ├── island_detection.h/cpp # Connected component island detection
│   │   ├── island_commands.h/cpp  # IPC: detect_islands, get_island_layer
│   │   ├── overhang.h/cpp      # Overhang analysis and auto-orient search
│   │   ├── support_types.h     # Support data structures (SupportPoint, SupportParams, etc.)
│   │   ├── support_gen.h/cpp   # Support point sampling and pillar mesh generation
│   │   ├── support_commands.h/cpp # IPC: generate/place/remove/clear supports
│   │   ├── mesh.h              # Core mesh data structure
│   │   ├── mesh_ops.h/cpp      # Mesh utility functions
│   │   ├── transform.h/cpp     # Transform state and commands
│   │   ├── app_state.h/cpp     # Global application state singleton
│   │   └── linalg_types.h      # Shared linalg type aliases
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
        ├── App.vue             # Main app: two-sidebar layout, IPC orchestration
        ├── components/
        │   ├── Viewport.vue    # 3D viewport: Three.js, model + support mesh, orbit, gizmo
        │   ├── Sidebar.vue     # Right sidebar: mesh info, transforms
        │   ├── OverhangPanel.vue  # Left sidebar: overhang analysis, auto-orient
        │   ├── SlicePanel.vue     # Left sidebar: slice controls, island results
        │   ├── SupportPanel.vue   # Left sidebar: support controls, categories, params
        │   └── KeybindingsModal.vue # Keybindings reference modal
        └── utils/
            ├── path.js         # Path utilities
            └── transformDelta.js # Transform delta computation
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
