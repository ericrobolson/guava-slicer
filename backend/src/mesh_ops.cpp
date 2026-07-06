/// @file mesh_ops.cpp
/// @brief Pure mesh geometry operations.
#include "mesh_ops.h"

#include <limits>

namespace mesh_ops {

/// @brief Transform a single vertex by a 4x4 matrix, returning homogeneous result.
static inline Vec4 transform_vertex(const float* vertices, uint32_t i, const Mat4& m) {
    Vec4 p = {vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2], 1.0f};
    return linalg::mul(m, p);
}

/// @brief Wrap an operation matrix in a translate sandwich: T(center) * op * T(-center).
static Mat4 transform_around_point(const Mat4& op, const Vec3& center) {
    Mat4 to_origin = linalg::translation_matrix(-center);
    Mat4 from_origin = linalg::translation_matrix(center);
    return linalg::mul(from_origin, linalg::mul(op, to_origin));
}

Vec3 compute_centroid(const float* vertices, uint32_t vertex_count) {
    if (vertex_count == 0) return {0, 0, 0};
    Vec3 sum = {0, 0, 0};
    for (uint32_t i = 0; i < vertex_count; ++i) {
        sum.x += vertices[i * 3];
        sum.y += vertices[i * 3 + 1];
        sum.z += vertices[i * 3 + 2];
    }
    float inv = 1.0f / static_cast<float>(vertex_count);
    return sum * inv;
}

void center_at_centroid(float* vertices, uint32_t vertex_count, mesh::BoundingBox& bbox) {
    Vec3 centroid = compute_centroid(vertices, vertex_count);
    for (uint32_t i = 0; i < vertex_count; ++i) {
        vertices[i * 3]     -= centroid.x;
        vertices[i * 3 + 1] -= centroid.y;
        vertices[i * 3 + 2] -= centroid.z;
    }
    bbox.min = bbox.min - centroid;
    bbox.max = bbox.max - centroid;
}

float min_transformed_y(const float* vertices, uint32_t vertex_count, const Mat4& transform) {
    float min_y = std::numeric_limits<float>::max();
    for (uint32_t i = 0; i < vertex_count; ++i) {
        Vec4 t = transform_vertex(vertices, i, transform);
        if (t.y < min_y) min_y = t.y;
    }
    return min_y;
}

Mat4 rotation_around_point(const Mat4& rotation, const Vec3& center) {
    return transform_around_point(rotation, center);
}

Vec3 center_on_plate_delta(const float* vertices, uint32_t vertex_count, const Mat4& transform) {
    if (vertex_count == 0) return {0, 0, 0};
    float sum_x = 0, sum_z = 0;
    for (uint32_t i = 0; i < vertex_count; ++i) {
        Vec4 t = transform_vertex(vertices, i, transform);
        sum_x += t.x;
        sum_z += t.z;
    }
    float inv = 1.0f / static_cast<float>(vertex_count);
    return {-sum_x * inv, 0.0f, -sum_z * inv};
}

Mat4 scale_around_point(const Vec3& factor, const Vec3& center) {
    Mat4 s = {{factor.x,0,0,0}, {0,factor.y,0,0}, {0,0,factor.z,0}, {0,0,0,1}};
    return transform_around_point(s, center);
}

Vec3 translation_of(const Mat4& m) {
    return {m.w.x, m.w.y, m.w.z};
}

} // namespace mesh_ops
