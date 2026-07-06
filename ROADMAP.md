# Guava Slicer — Roadmap

Phased implementation. Each feature starts with a spike to prove the approach before full implementation. Phases are sequential unless noted otherwise. IPC comes first — everything flows through it.

---

## Phase 1: IPC Spike ✓ DONE
Prove the stdio IPC protocol between a minimal C++ backend and Electron frontend.

**Deliverables**:
- [x] Minimal C++ backend: reads JSON from stdin, writes JSON to stdout
- [x] Length-prefixed binary framing for large payloads
- [x] UUID-based request/response correlation
- [x] Standard `{ok, error}` response envelope
- [x] Progress event streaming (simulated long operation)
- [x] Electron app spawns backend via `child_process`
- [x] Vue frontend sends a command and displays the response
- [x] Zig build for backend, npm for frontend, top-level Makefile wrapping both
- [x] Verify cross-compilation of backend (macOS ARM/Intel, Windows x86_64)

**Decisions made**:
- [x] JSON parsing library: nlohmann/json v3.11.3, header-only, vendored
- [x] Binary framing format: 1-byte type prefix (0x01 JSON, 0x02 binary) + 4-byte LE length + payload
- [x] Threading: std::thread per long-running command (no pool yet — revisit when needed)

---

## Phase 2: STL Loading Spike ✓ DONE
Load an STL file in the backend, send geometry to the frontend, display in Three.js.

**Deliverables**:
- [x] STL parser (binary + ASCII) in C++ via microstl wrapper with result types
- [x] IPC: `load_mesh` command → backend parses → sends vertex/face data to frontend via binary frame
- [x] Three.js viewport with orbit camera, grid, build plate visualization
- [x] Mesh display with basic shading (MeshPhongMaterial, flat shading)
- [x] File open dialog (Electron) + drag-and-drop STL files onto viewport
- [x] `make -s run` launches app, opens file, displays mesh
- [x] Blender-style numpad controls (1/3/7 views, 5 ortho toggle, 2/4/6/8 orbit, 9 back, 0 reset, +/- zoom)
- [x] 3D axis gizmo in viewport corner
- [x] Mesh info sidebar (vertex/triangle count, bounding box, dimensions, file size)
- [x] Two-phase progress bar (loading + processing mesh)
- [x] Memory-mapped file I/O for 100MB+ STL files
- [x] Vertex deduplication with hash map during parse

**Decisions made**:
- [x] Mesh data structure: indexed triangle soup (flat float/uint32 arrays, cache-friendly, trivial IPC serialization)
- [x] Linear algebra library: linalg.h (public domain, single header, carries forward to Phase 5+)
- [x] STL parser: microstl (MIT, header-only) with result-type wrapper, fast_float for ASCII number parsing
- [x] Error handling: production-grade with structured errors at IPC boundary, partial recovery, mmap for large files

---

## Phase 3: Object Transforms + Undo/Redo
Model manipulation tools and the command pattern infrastructure to make them undoable. Built simultaneously — every transform is an undoable command from day one.

**Deliverables — Undo/Redo**:
- [x] Command base class with `execute()` and `undo()` methods
- [x] Undo/redo stack in backend
- [x] IPC: `undo` and `redo` commands
- [x] Frontend: Cmd+Z / Cmd+Shift+Z keybindings
- [x] Undo/redo state in UI (button enable/disable, action name display)
- [x] Retroactively wrap `load_mesh` as a command

**Deliverables — Model Manipulation**:
- [x] Model orientation (rotate around X/Y/Z, snap to 90°)
- [x] Model positioning (translate, place on build plate, center on build plate)
- [x] Model scaling (uniform and per-axis)
- [x] All transforms are undoable commands
- [x] Transform gizmo in Three.js viewport + hold-and-drag (G/R/S)
- [x] Reset to original orientation button

---

## Phase 4: Overhang Analysis & Visualization ✓ DONE
Detect and visualize overhanging geometry that exceeds a configurable angle threshold. Overhangs are areas where printed material extends outward over empty space without solid material directly beneath it — gravity pulls unsupported molten plastic downward, causing sagging or deformed prints. The 45-degree rule: most FDM printers handle overhangs up to ~45° from vertical, where each new layer has enough of the previous layer underneath to anchor to.

**Deliverables**:
- [x] Overhang detection algorithm: compute face normals, flag triangles whose angle from vertical exceeds the threshold
- [x] Configurable overhang angle threshold (default 45°, adjustable in UI)
- [x] Overhang visualization in 3D viewport: checkerboard pattern overlay in yellow/dark on overhang faces
- [x] Overhang area metric and % of surface displayed in sidebar
- [x] Real-time overhang overlay updates on every transform (debounced 300ms)
- [x] Manual rotation to minimize overhangs — user rotates model while watching overhang overlay update live
- [x] Auto-orient with multi-objective cost function (overhang area, bottom contact area, print height, tilt bias)
- [x] Precompute orientations for all 5 axes in background after mesh load — each axis shows resulting overhang area
- [x] Click axis row to instantly apply pre-computed orientation
- [x] Toggle overhang visualization on/off
- [x] IPC: `analyze_overhangs`, `auto_orient`, `precompute_orientations`, `apply_orientation`, `cancel_auto_orient`
- [x] Threaded analysis with progress streaming

**Decisions made**:
- [x] Overhang threshold formula: `dot(face_normal, UP) < -sin(threshold_rad)` — angle measured from vertical
- [x] Auto-orient scoring: weighted multi-objective (overhang 1.0, bottom contact 0.5, height 0.15, tilt bias 0.1)
- [x] Sampling: Euler sweep for preferred axis, Fibonacci lattice for "any", stride-4 subsampling during coarse, ±8° fine refinement
- [x] Build with `-Doptimize=ReleaseFast` for the main binary (linalg template code needs optimization)

---

## Phase 5: Slicing Engine Spike
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

## Phase 6: Island Detection
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

## Phase 7: Support Generation
Generate FDM-compatible supports and rafts.

**Reference**: `/Users/ericolson/Library/CloudStorage/Dropbox/3d printing/software/Resin2FDM-Lite-v1` — Resin2FDM codebase for support generation / mesh processing prior art.

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

## Phase 8: Project Persistence
Save and load project state so work survives across sessions.

**Deliverables**:
- [ ] Project file format design (JSON or SQLite — decide during spike)
- [ ] Save: mesh file path, model transform, slice parameters, placed supports, raft settings
- [ ] Load: restore full project state from file
- [ ] IPC: `save_project` and `load_project` commands
- [ ] Recent files list in frontend
- [ ] Dirty state tracking (unsaved changes indicator)
- [ ] File association (.guava extension)
- [ ] Project save captures undo stack state
- [ ] Cache precomputed auto-orient rotations per mesh (keyed by mesh hash + threshold) — skip recomputation on reload

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

---

## Phase 10: Model Sectioning
Cut a model into multiple pieces along user-defined planes, with connectors (pins, dovetails, etc.) for reassembly after printing. Enables printing large models that exceed build volume.

**Deliverables**:
- [ ] Spike: mesh-plane boolean cut (split mesh into two pieces along an arbitrary plane)
- [ ] Spike: connector geometry generation (pin/socket, dovetail — at least two styles)
- [ ] Cutting plane placement UI in 3D viewport
- [ ] Preview of cut pieces before committing
- [ ] Connector placement at cut interfaces (automatic or manual)
- [ ] Export cut pieces as separate STLs
- [ ] Undo/redo for all sectioning operations

**Decisions to make**:
- [ ] Boolean library (Clipper2? CGAL? manifold? — must vendor and build under Zig)
- [ ] Connector attachment strategy (geometry union vs. separate mesh)
- [ ] Multi-cut ordering and interaction (can cuts intersect?)
