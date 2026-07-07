/// @file stl_writer.h
/// @brief Binary STL export from flat vertex/index arrays.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace stl_writer {

/// @brief Write a mesh (flat vertex + index arrays) to a binary STL file.
bool write_stl(const std::string& path,
    const float* vertices, const uint32_t* indices, uint32_t triangle_count);

/// @brief Write two meshes combined into one binary STL file.
bool write_combined_stl(const std::string& path,
    const float* verts_a, const uint32_t* idx_a, uint32_t tris_a,
    const float* verts_b, const uint32_t* idx_b, uint32_t tris_b);

/// @brief Export a model mesh with baked transforms, optionally merged with support mesh.
/// Applies a 4x4 column-major transform matrix to model vertices before writing.
/// Support vertices are written as-is (already in world space).
/// If support data is null/zero, writes model-only.
/// Returns the number of bytes written, or 0 on failure.
uint64_t export_combined_stl(const std::string& path,
    const float* model_verts, const uint32_t* model_idx, uint32_t model_vert_count, uint32_t model_tri_count,
    const float transform[4][4],
    const float* support_verts, const uint32_t* support_idx, uint32_t support_tri_count);

} // namespace stl_writer
