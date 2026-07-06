# Saved Decisions

## Stage 4: Error Handling

**Decision: Production-grade with recovery**

Structured JSON errors at the IPC boundary with error codes (`IO_ERROR`, `PARSE_ERROR`, `INVALID_STL`, `OUT_OF_MEMORY`) and human-readable messages. Partial parse recovery (skip malformed triangles with warning count). Memory-mapped file I/O for large files. Detailed error context at the boundary (byte offset, error description).

Justification: Real-world STL files from cheap slicers are frequently slightly malformed. Skipping bad triangles with a warning count is better than failing the entire load. Memory-mapped I/O is essential for 100MB+ files. This is the foundation I/O path for the entire app — worth the complexity.

## Stage 1: User-Facing Interfaces

**Decision: In-viewport overlay with sidebar controls**

Vertical layer slider on the right edge of the 3D viewport. "Slice" button and layer height input in the sidebar. Layer inspection mode clips geometry above the current Y height via Three.js clipping plane and draws contour lines at the slice plane. No separate 2D canvas — the 3D viewport with clipping plane keeps spatial context while inspecting layers.

Justification: Matches the Lychee Slicer reference UI. The slider is spatially aligned with the model's vertical axis, making layer scrubbing intuitive. Keeping everything in-viewport avoids context switching between panels and is simpler to implement than a separate 2D rendering surface.

## Stage 7: Tech Stack

**Decision: Compile Clipper2 sources directly in build.zig**

Add Clipper2's 3 .cpp source files as compilation units in the existing build.zig alongside backend sources. Clipper2 uses exceptions internally, so it compiles with `cpp_flags_with_exceptions` (same pattern as stl_parser.cpp). Include path: `vendor/clipper2/include`.

Justification: Zero new build targets — Clipper2 compiles as part of the backend binary with the same optimization level. The 3-file overhead is negligible. Separate static library would add complexity for no benefit at this scale.
