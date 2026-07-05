# Guava Slicer — Roadmap

Phased implementation. Each feature starts with a spike to prove the approach before full implementation. Phases are sequential unless noted otherwise. IPC comes first — everything flows through it.

---

## Phase 1: IPC Spike ← ACTIVE
Prove the stdio IPC protocol between a minimal C++ backend and Electron frontend.

**Deliverables**:
- [ ] Minimal C++ backend: reads JSON from stdin, writes JSON to stdout
- [ ] Length-prefixed binary framing for large payloads
- [ ] UUID-based request/response correlation
- [ ] Standard `{ok, error}` response envelope
- [ ] Progress event streaming (simulated long operation)
- [ ] Electron app spawns backend via `child_process`
- [ ] Vue frontend sends a command and displays the response
- [ ] Zig build for backend, npm for frontend, top-level Makefile wrapping both
- [ ] Verify cross-compilation of backend (macOS ARM/Intel, Windows x86_64)

**Decisions to make**:
- [ ] JSON parsing library for C++ (candidate: nlohmann/json, header-only)
- [ ] Binary framing format (4-byte LE length + payload)
- [ ] Thread pool library or roll minimal pool

---

## Phase 2: STL Loading Spike
Load an STL file in the backend, send geometry to the frontend, display in Three.js.

**Deliverables**:
- [ ] STL parser (binary + ASCII) in C++
- [ ] IPC: `load_mesh` command → backend parses → sends vertex/face data to frontend via binary frame
- [ ] Three.js viewport with orbit camera, grid, build plate visualization
- [ ] Mesh display with basic shading
- [ ] File open dialog (Electron)
- [ ] `make -s run` launches app, opens file, displays mesh

**Decisions to make**:
- [ ] Mesh data structure (indexed triangle soup vs. half-edge — spike both, profile)
- [ ] Linear algebra library (candidate: Eigen or lightweight header-only vec/mat)
- [ ] STL ASCII parser (candidate: fast_float for number parsing)

---

## Phase 3: Project Persistence
Save and load project state so work survives across sessions.

**Deliverables**:
- [ ] Project file format design (JSON or SQLite — decide during spike)
- [ ] Save: mesh file path, model transform, slice parameters, placed supports, raft settings
- [ ] Load: restore full project state from file
- [ ] IPC: `save_project` and `load_project` commands
- [ ] Recent files list in frontend
- [ ] Dirty state tracking (unsaved changes indicator)
- [ ] File association (.guava extension)

---

## Phase 4: Undo/Redo System
Command pattern infrastructure for all mutations.

**Deliverables**:
- [ ] Command base class with `execute()` and `undo()` methods
- [ ] Undo/redo stack in backend
- [ ] IPC: `undo` and `redo` commands
- [ ] Frontend: Ctrl+Z / Ctrl+Shift+Z keybindings
- [ ] Undo/redo state in UI (button enable/disable, action name display)
- [ ] Retroactively wrap `load_mesh` and model transforms as commands
- [ ] Project save captures undo stack state

---

## Phase 5: Model Manipulation
Pre-slice model editing tools.

**Deliverables**:
- [ ] Model orientation (rotate around X/Y/Z, snap to 90°)
- [ ] Model positioning (translate, place on build plate)
- [ ] Model scaling (uniform and per-axis)
- [ ] All transforms are undoable commands
- [ ] Transform gizmo in Three.js viewport
- [ ] Reset to original orientation button

---

## Phase 6: Slicing Engine Spike
Intersect mesh with Z-planes, produce contour polygons per layer.

**Deliverables**:
- [ ] Mesh-plane intersection algorithm (produce contour line segments per layer)
- [ ] Contour assembly (connect line segments into closed polygons)
- [ ] Threaded slicing (slice on worker thread, stream progress to frontend)
- [ ] IPC: `slice` command with layer height param → stream layer contours
- [ ] Layer preview: 2D canvas/SVG showing contour polygons for selected layer
- [ ] Z-slider to step through layers
- [ ] Layer count and current height display
- [ ] Slice result cached until mesh or params change

**Decisions to make**:
- [ ] Polygon library for contour ops (candidate: Clipper2 — vendored, builds under Zig)

---

## Phase 7: Island Detection
Identify unsupported regions per layer.

**Deliverables**:
- [ ] Connected component labeling on each layer's contour polygons
- [ ] Overlap test: check each component in layer N against all components in layer N-1
- [ ] No-overlap components flagged as islands
- [ ] Islands highlighted in layer preview (red fill/outline)
- [ ] Islands highlighted in 3D viewport (colored overlay on mesh)
- [ ] Detection speed tiers: fast (skip layers), normal, full (every layer)
- [ ] Island count summary per layer in UI
- [ ] Threaded detection with progress streaming

---

## Phase 8: Support Generation
Generate FDM-compatible supports and rafts.

**Deliverables**:
- [ ] Support placement: click-to-place in 3D viewport (undoable)
- [ ] Support removal: click to delete individual supports (undoable)
- [ ] Support geometry: tree/column supports sized for FDM (configurable min radius, taper)
- [ ] Auto-generate supports for detected islands (undoable)
- [ ] Support mesh visually distinct from model in viewport (different color/transparency)
- [ ] Raft generation: flat raft geometry under model + supports (undoable)
- [ ] Raft parameters: margin, thickness, density
- [ ] Re-slice with supports + raft included
- [ ] Re-run island detection after support placement to verify fix

---

## Phase 9: STL Export
Export model and supports as separate STLs.

**Deliverables**:
- [ ] Export model mesh as STL (with applied transforms)
- [ ] Export support + raft mesh as separate STL
- [ ] Export combined (model + supports + raft) as single STL
- [ ] Export dialog with format options
- [ ] IPC: `export_stl` command with export mode param
- [ ] Verify exported STLs load correctly in Bambu Studio
