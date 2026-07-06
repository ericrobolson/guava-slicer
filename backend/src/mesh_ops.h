/// @file mesh_ops.h
/// @brief Pure functions for mesh geometry operations — no IO, no state.
#pragma once

#include "linalg_types.h"
#include "mesh.h"

#include <cstdint>

namespace mesh_ops {

using linalg_types::Vec3;
using linalg_types::Vec4;
using linalg_types::Mat4;

/// @brief Compute the vertex centroid (average of all vertex positions).
Vec3 compute_centroid(const float* vertices, uint32_t vertex_count);

/// @brief Translate all vertices so the centroid is at the origin. Updates bounding box.
void center_at_centroid(float* vertices, uint32_t vertex_count, mesh::BoundingBox& bbox);

/// @brief Find the minimum Y coordinate across all vertices after applying a transform matrix.
float min_transformed_y(const float* vertices, uint32_t vertex_count, const Mat4& transform);

/// @brief Build a rotation matrix that rotates around an arbitrary point (translate-sandwich).
Mat4 rotation_around_point(const Mat4& rotation, const Vec3& center);

/// @brief Build a scaling matrix that scales around an arbitrary point (translate-sandwich).
Mat4 scale_around_point(const Vec3& factor, const Vec3& center);

/// @brief Compute the XZ translation needed to center the mesh on the origin (Y unchanged).
Vec3 center_on_plate_delta(const float* vertices, uint32_t vertex_count, const Mat4& transform);

/// @brief Extract the translation component from a 4x4 matrix (the transformed origin).
Vec3 translation_of(const Mat4& m);

} // namespace mesh_ops
