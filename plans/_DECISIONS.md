# Saved Decisions

## Stage 4: Error Handling

**Decision: Production-grade with recovery**

Structured JSON errors at the IPC boundary with error codes (`IO_ERROR`, `PARSE_ERROR`, `INVALID_STL`, `OUT_OF_MEMORY`) and human-readable messages. Partial parse recovery (skip malformed triangles with warning count). Memory-mapped file I/O for large files. Detailed error context at the boundary (byte offset, error description).

Justification: Real-world STL files from cheap slicers are frequently slightly malformed. Skipping bad triangles with a warning count is better than failing the entire load. Memory-mapped I/O is essential for 100MB+ files. This is the foundation I/O path for the entire app — worth the complexity.
