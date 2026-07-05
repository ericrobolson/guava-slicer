/// @file mesh.h
/// @brief Core mesh data structures for indexed triangle geometry.
#pragma once

#include <linalg/linalg.h>
#include <cstdint>
#include <string>
#include <vector>

namespace mesh {

using Vec3 = linalg::vec<float, 3>;

/// @brief Axis-aligned bounding box.
struct BoundingBox {
    Vec3 min{0.0f, 0.0f, 0.0f};
    Vec3 max{0.0f, 0.0f, 0.0f};
};

/// @brief Indexed triangle mesh stored as flat GPU-ready arrays.
struct Mesh {
    /// Flat vertex positions: [x0,y0,z0, x1,y1,z1, ...]. Size = 3 * vertex_count.
    std::vector<float> vertices;
    /// Flat vertex normals: [nx0,ny0,nz0, ...]. Size = 3 * vertex_count.
    std::vector<float> normals;
    /// Triangle indices into the vertex array. Size = 3 * triangle_count.
    std::vector<uint32_t> indices;

    uint32_t vertex_count = 0;
    uint32_t triangle_count = 0;
    BoundingBox bounding_box;
    std::string source_path;
    uint64_t file_size_bytes = 0;
    uint32_t skipped_triangles = 0;
};

} // namespace mesh
