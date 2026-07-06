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

} // namespace stl_writer
