/// @file mesh_raycaster.h
/// @brief BVH-accelerated ray-mesh intersection for support collision testing.
#pragma once

#include "linalg_types.h"

#include <cstdint>
#include <vector>

namespace raycaster {

using linalg_types::Vec3;
using linalg_types::Mat4;

/// @brief Axis-aligned bounding box.
struct AABB {
    Vec3 lo{1e30f, 1e30f, 1e30f};
    Vec3 hi{-1e30f, -1e30f, -1e30f};

    void expand(const Vec3& p);
    void expand(const AABB& other);
    float surface_area() const;
    bool intersects_ray(const Vec3& origin, const Vec3& inv_dir, float t_max) const;
};

/// @brief BVH node — either interior (children) or leaf (triangle range).
struct BVHNode {
    AABB box;
    uint32_t left;
    uint32_t right;
    uint32_t tri_start;
    uint32_t tri_count;
    bool is_leaf() const { return tri_count > 0; }
};

/// @brief Result of a ray-mesh intersection query.
struct HitResult {
    bool hit = false;
    float t = 1e30f;
    uint32_t triangle_index = 0;
    Vec3 point{0, 0, 0};
};

/// @brief BVH-accelerated mesh raycaster.
///
/// Builds a SAH-optimized BVH from triangle mesh data, then supports
/// segment intersection queries (bounded rays).
class MeshRaycaster {
public:
    /// @brief Build BVH from mesh in world space (vertices pre-transformed).
    void build(const float* vertices, const uint32_t* indices,
        uint32_t vertex_count, uint32_t triangle_count,
        const Mat4& transform);

    /// @brief Test if a line segment from `a` to `b` intersects the mesh.
    bool segment_hits(const Vec3& a, const Vec3& b) const;

    /// @brief Find the closest intersection along a segment from `a` to `b`.
    HitResult segment_cast(const Vec3& a, const Vec3& b) const;

    /// @brief Number of triangles in the BVH.
    uint32_t triangle_count() const { return tri_count_; }

    /// @brief Check if the raycaster has been built.
    bool ready() const { return !nodes_.empty(); }

private:
    std::vector<Vec3> verts_;
    std::vector<uint32_t> tri_indices_;
    std::vector<uint32_t> tri_order_;
    std::vector<BVHNode> nodes_;
    uint32_t tri_count_ = 0;

    uint32_t build_recursive(uint32_t start, uint32_t count);
    HitResult intersect_ray(const Vec3& origin, const Vec3& dir, float t_max) const;
};

} // namespace raycaster
