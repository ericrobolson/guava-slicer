# Debugging Notes — Slicing Engine

## Structural Review Fixes

### C1: Double lock gap in slicer_commands.cpp
**Issue:** Two separate lock scopes with a gap between them when storing the slice result and sending the IPC response. Another thread could overwrite `s_cached_result` between the two locks.
**Fix:** Combined both operations into a single lock scope.

### I5: Unprotected writes to s_slicing_active
**Issue:** `s_slicing_active` was a plain `bool` written from the worker thread without synchronization.
**Fix:** Changed to `std::atomic<bool>`.

### C2: Strict aliasing violation in compute_mesh_hash
**Issue:** `reinterpret_cast<const float*>(&mat)` violates strict aliasing rules.
**Fix:** Replaced with `std::memcpy` into a local `float[16]` array.
