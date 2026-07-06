/// @file mesh_raycaster.cpp
/// @brief SAH-based BVH construction and Moller-Trumbore ray-triangle intersection.
#include "mesh_raycaster.h"

#include <algorithm>
#include <cmath>

namespace raycaster {

static constexpr uint32_t MAX_LEAF_TRIS = 4;
static constexpr uint32_t SAH_BINS = 12;
static constexpr float TRAVERSAL_COST = 1.0f;
static constexpr float INTERSECT_COST = 1.0f;
static constexpr float RAY_EPSILON = 1e-6f;

// --- AABB ---

void AABB::expand(const Vec3& p) {
    lo = linalg::min(lo, p);
    hi = linalg::max(hi, p);
}

void AABB::expand(const AABB& other) {
    lo = linalg::min(lo, other.lo);
    hi = linalg::max(hi, other.hi);
}

float AABB::surface_area() const {
    Vec3 d = hi - lo;
    return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
}

bool AABB::intersects_ray(const Vec3& origin, const Vec3& inv_dir, float t_max) const {
    float t1 = (lo.x - origin.x) * inv_dir.x;
    float t2 = (hi.x - origin.x) * inv_dir.x;
    float t3 = (lo.y - origin.y) * inv_dir.y;
    float t4 = (hi.y - origin.y) * inv_dir.y;
    float t5 = (lo.z - origin.z) * inv_dir.z;
    float t6 = (hi.z - origin.z) * inv_dir.z;

    float tmin = std::max(std::max(std::min(t1, t2), std::min(t3, t4)), std::min(t5, t6));
    float tmax = std::min(std::min(std::max(t1, t2), std::max(t3, t4)), std::max(t5, t6));

    return tmax >= std::max(tmin, 0.0f) && tmin < t_max;
}

// --- Moller-Trumbore ray-triangle intersection ---

static bool ray_triangle(const Vec3& origin, const Vec3& dir,
    const Vec3& v0, const Vec3& v1, const Vec3& v2, float& t_out) {
    Vec3 e1 = v1 - v0;
    Vec3 e2 = v2 - v0;
    Vec3 h = linalg::cross(dir, e2);
    float det = linalg::dot(e1, h);

    if (std::abs(det) < RAY_EPSILON) return false;

    float inv_det = 1.0f / det;
    Vec3 s = origin - v0;
    float u = linalg::dot(s, h) * inv_det;
    if (u < 0.0f || u > 1.0f) return false;

    Vec3 q = linalg::cross(s, e1);
    float v = linalg::dot(dir, q) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = linalg::dot(e2, q) * inv_det;
    if (t < RAY_EPSILON) return false;

    t_out = t;
    return true;
}

// --- Transform helper ---

static Vec3 transform_point(const Vec3& p, const Mat4& m) {
    return {
        m[0][0] * p.x + m[1][0] * p.y + m[2][0] * p.z + m[3][0],
        m[0][1] * p.x + m[1][1] * p.y + m[2][1] * p.z + m[3][1],
        m[0][2] * p.x + m[1][2] * p.y + m[2][2] * p.z + m[3][2],
    };
}

// --- BVH build ---

void MeshRaycaster::build(const float* vertices, const uint32_t* indices,
    uint32_t vertex_count, uint32_t triangle_count, const Mat4& transform) {

    tri_count_ = triangle_count;

    verts_.resize(vertex_count);
    for (uint32_t i = 0; i < vertex_count; ++i) {
        Vec3 raw = {vertices[i*3], vertices[i*3+1], vertices[i*3+2]};
        verts_[i] = transform_point(raw, transform);
    }

    tri_indices_.resize(triangle_count * 3);
    for (uint32_t i = 0; i < triangle_count * 3; ++i)
        tri_indices_[i] = indices[i];

    tri_order_.resize(triangle_count);
    for (uint32_t i = 0; i < triangle_count; ++i)
        tri_order_[i] = i;

    nodes_.clear();
    nodes_.reserve(2 * triangle_count);

    build_recursive(0, triangle_count);
}

uint32_t MeshRaycaster::build_recursive(uint32_t start, uint32_t count) {
    uint32_t node_idx = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back({});
    auto& node = nodes_[node_idx];

    AABB bounds;
    for (uint32_t i = start; i < start + count; ++i) {
        uint32_t ti = tri_order_[i];
        bounds.expand(verts_[tri_indices_[ti*3+0]]);
        bounds.expand(verts_[tri_indices_[ti*3+1]]);
        bounds.expand(verts_[tri_indices_[ti*3+2]]);
    }
    node.box = bounds;

    if (count <= MAX_LEAF_TRIS) {
        node.tri_start = start;
        node.tri_count = count;
        node.left = node.right = 0;
        return node_idx;
    }

    // SAH binned split
    float best_cost = INTERSECT_COST * static_cast<float>(count);
    int best_axis = -1;
    uint32_t best_split = start;

    Vec3 extent = bounds.hi - bounds.lo;

    for (int axis = 0; axis < 3; ++axis) {
        float axis_extent = (axis == 0) ? extent.x : (axis == 1) ? extent.y : extent.z;
        if (axis_extent < RAY_EPSILON) continue;

        float axis_lo = (axis == 0) ? bounds.lo.x : (axis == 1) ? bounds.lo.y : bounds.lo.z;
        float inv_extent = 1.0f / axis_extent;

        struct Bin { AABB box; uint32_t count = 0; };
        Bin bins[SAH_BINS];

        for (uint32_t i = start; i < start + count; ++i) {
            uint32_t ti = tri_order_[i];
            Vec3 c = (verts_[tri_indices_[ti*3+0]] +
                      verts_[tri_indices_[ti*3+1]] +
                      verts_[tri_indices_[ti*3+2]]) * (1.0f / 3.0f);
            float cv = (axis == 0) ? c.x : (axis == 1) ? c.y : c.z;
            uint32_t b = static_cast<uint32_t>((cv - axis_lo) * inv_extent * static_cast<float>(SAH_BINS));
            if (b >= SAH_BINS) b = SAH_BINS - 1;
            bins[b].box.expand(verts_[tri_indices_[ti*3+0]]);
            bins[b].box.expand(verts_[tri_indices_[ti*3+1]]);
            bins[b].box.expand(verts_[tri_indices_[ti*3+2]]);
            bins[b].count++;
        }

        // Sweep to find best split
        AABB left_box;
        uint32_t left_count = 0;
        float right_area[SAH_BINS];
        uint32_t right_count_arr[SAH_BINS];

        AABB right_box;
        uint32_t rc = 0;
        for (int i = SAH_BINS - 1; i >= 0; --i) {
            right_box.expand(bins[i].box);
            rc += bins[i].count;
            right_area[i] = right_box.surface_area();
            right_count_arr[i] = rc;
        }

        for (uint32_t i = 0; i < SAH_BINS - 1; ++i) {
            left_box.expand(bins[i].box);
            left_count += bins[i].count;
            if (left_count == 0 || left_count == count) continue;

            float cost = TRAVERSAL_COST +
                INTERSECT_COST * (left_box.surface_area() * static_cast<float>(left_count) +
                                  right_area[i + 1] * static_cast<float>(right_count_arr[i + 1])) /
                bounds.surface_area();

            if (cost < best_cost) {
                best_cost = cost;
                best_axis = axis;
                float split_pos = axis_lo + axis_extent * static_cast<float>(i + 1) / static_cast<float>(SAH_BINS);
                (void)split_pos;

                // Partition tri_order
                uint32_t mid = start;
                for (uint32_t j = start; j < start + count; ++j) {
                    uint32_t tj = tri_order_[j];
                    Vec3 cj = (verts_[tri_indices_[tj*3+0]] +
                               verts_[tri_indices_[tj*3+1]] +
                               verts_[tri_indices_[tj*3+2]]) * (1.0f / 3.0f);
                    float cjv = (axis == 0) ? cj.x : (axis == 1) ? cj.y : cj.z;
                    uint32_t bj = static_cast<uint32_t>((cjv - axis_lo) * inv_extent * static_cast<float>(SAH_BINS));
                    if (bj >= SAH_BINS) bj = SAH_BINS - 1;
                    if (bj <= i) {
                        std::swap(tri_order_[j], tri_order_[mid]);
                        ++mid;
                    }
                }
                best_split = mid;
            }
        }
    }

    if (best_axis == -1 || best_split == start || best_split == start + count) {
        node.tri_start = start;
        node.tri_count = count;
        node.left = node.right = 0;
        return node_idx;
    }

    // Need to re-partition for the best axis/split found
    // The partition was done in-place during the sweep above for the last improvement,
    // but we need to redo it cleanly since the sweep may have tested multiple axes.
    {
        Vec3 ext = bounds.hi - bounds.lo;
        float axis_lo = (best_axis == 0) ? bounds.lo.x : (best_axis == 1) ? bounds.lo.y : bounds.lo.z;
        float axis_extent = (best_axis == 0) ? ext.x : (best_axis == 1) ? ext.y : ext.z;
        float inv_ext = 1.0f / axis_extent;

        // Find the bin threshold that produces best_split
        // Re-partition cleanly
        struct Bin { uint32_t count = 0; };
        Bin bins2[SAH_BINS];
        for (uint32_t i = start; i < start + count; ++i) {
            uint32_t ti = tri_order_[i];
            Vec3 c = (verts_[tri_indices_[ti*3+0]] +
                      verts_[tri_indices_[ti*3+1]] +
                      verts_[tri_indices_[ti*3+2]]) * (1.0f / 3.0f);
            float cv = (best_axis == 0) ? c.x : (best_axis == 1) ? c.y : c.z;
            uint32_t b = static_cast<uint32_t>((cv - axis_lo) * inv_ext * static_cast<float>(SAH_BINS));
            if (b >= SAH_BINS) b = SAH_BINS - 1;
            bins2[b].count++;
        }

        // Find the split bin that yields best_split left_count
        uint32_t left_total = 0;
        uint32_t split_bin = 0;
        for (uint32_t i = 0; i < SAH_BINS - 1; ++i) {
            left_total += bins2[i].count;
            if (left_total > 0 && left_total < count) {
                split_bin = i;
                if (left_total + start == best_split) break;
            }
        }

        uint32_t mid = start;
        for (uint32_t j = start; j < start + count; ++j) {
            uint32_t tj = tri_order_[j];
            Vec3 cj = (verts_[tri_indices_[tj*3+0]] +
                       verts_[tri_indices_[tj*3+1]] +
                       verts_[tri_indices_[tj*3+2]]) * (1.0f / 3.0f);
            float cjv = (best_axis == 0) ? cj.x : (best_axis == 1) ? cj.y : cj.z;
            uint32_t bj = static_cast<uint32_t>((cjv - axis_lo) * inv_ext * static_cast<float>(SAH_BINS));
            if (bj >= SAH_BINS) bj = SAH_BINS - 1;
            if (bj <= split_bin) {
                std::swap(tri_order_[j], tri_order_[mid]);
                ++mid;
            }
        }
        best_split = mid;
    }

    if (best_split == start || best_split == start + count) {
        node.tri_start = start;
        node.tri_count = count;
        node.left = node.right = 0;
        return node_idx;
    }

    node.tri_start = 0;
    node.tri_count = 0;

    uint32_t left_count = best_split - start;
    uint32_t right_count = count - left_count;

    uint32_t left_idx = build_recursive(start, left_count);
    uint32_t right_idx = build_recursive(best_split, right_count);

    nodes_[node_idx].left = left_idx;
    nodes_[node_idx].right = right_idx;

    return node_idx;
}

// --- Queries ---

HitResult MeshRaycaster::intersect_ray(const Vec3& origin, const Vec3& dir, float t_max) const {
    HitResult result;

    Vec3 inv_dir = {
        std::abs(dir.x) > RAY_EPSILON ? 1.0f / dir.x : 1e30f * (dir.x >= 0 ? 1.0f : -1.0f),
        std::abs(dir.y) > RAY_EPSILON ? 1.0f / dir.y : 1e30f * (dir.y >= 0 ? 1.0f : -1.0f),
        std::abs(dir.z) > RAY_EPSILON ? 1.0f / dir.z : 1e30f * (dir.z >= 0 ? 1.0f : -1.0f),
    };

    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        uint32_t ni = stack[--sp];
        const auto& node = nodes_[ni];

        if (!node.box.intersects_ray(origin, inv_dir, result.hit ? result.t : t_max))
            continue;

        if (node.is_leaf()) {
            for (uint32_t i = node.tri_start; i < node.tri_start + node.tri_count; ++i) {
                uint32_t ti = tri_order_[i];
                const Vec3& v0 = verts_[tri_indices_[ti*3+0]];
                const Vec3& v1 = verts_[tri_indices_[ti*3+1]];
                const Vec3& v2 = verts_[tri_indices_[ti*3+2]];

                float t;
                if (ray_triangle(origin, dir, v0, v1, v2, t)) {
                    if (t < (result.hit ? result.t : t_max)) {
                        result.hit = true;
                        result.t = t;
                        result.triangle_index = ti;
                        result.point = origin + dir * t;
                    }
                }
            }
        } else {
            stack[sp++] = node.left;
            stack[sp++] = node.right;
        }
    }

    return result;
}

bool MeshRaycaster::segment_hits(const Vec3& a, const Vec3& b) const {
    Vec3 dir = b - a;
    float len = linalg::length(dir);
    if (len < RAY_EPSILON) return false;
    dir = dir / len;

    auto hit = intersect_ray(a, dir, len);
    return hit.hit;
}

HitResult MeshRaycaster::segment_cast(const Vec3& a, const Vec3& b) const {
    Vec3 dir = b - a;
    float len = linalg::length(dir);
    if (len < RAY_EPSILON) return {};
    dir = dir / len;

    return intersect_ray(a, dir, len);
}

} // namespace raycaster
