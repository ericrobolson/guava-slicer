/// @file support_gen.h
/// @brief Pure functions for support point sampling and pillar mesh generation.
#pragma once

#include "island_detection.h"
#include "mesh_raycaster.h"
#include "overhang.h"
#include "support_types.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace support_gen {

/// @brief Callback for generation progress: (current_step, total_steps).
using ProgressCallback = std::function<void(uint32_t, uint32_t)>;

/// @brief Sample island support points — one at each island centroid.
std::vector<support::SupportPoint> sample_island_points(
    const island_detection::IslandResult& islands,
    const slicer::SliceResult& slices,
    const float* vertices,
    const uint32_t* indices,
    uint32_t vertex_count,
    uint32_t triangle_count,
    const linalg_types::Mat4& transform,
    uint32_t& next_id);

/// @brief Sample reinforcement points around island regions.
std::vector<support::SupportPoint> sample_reinforcement_points(
    const island_detection::IslandResult& islands,
    const slicer::SliceResult& slices,
    uint32_t& next_id);

/// @brief Sample overhang support points via Poisson-disc on overhang triangles.
std::vector<support::SupportPoint> sample_overhang_points(
    const overhang::OverhangResult& overhangs,
    const float* vertices,
    const uint32_t* indices,
    uint32_t vertex_count,
    const linalg_types::Mat4& transform,
    float spacing,
    uint32_t& next_id);

/// @brief Sample stabilization points on tall narrow features.
std::vector<support::SupportPoint> sample_stabilization_points(
    const float* vertices,
    const uint32_t* indices,
    uint32_t vertex_count,
    uint32_t triangle_count,
    const linalg_types::Mat4& transform,
    float spacing,
    uint32_t& next_id);

/// @brief Generate pillar mesh geometry for a single support point.
///
/// Produces a pillar consisting of: base flare cone (at Y=0), cylindrical shaft,
/// and contact tip sphere/cone at the model surface.
void generate_pillar(
    const support::SupportPoint& point,
    const support::SupportParams& params,
    std::vector<float>& out_vertices,
    std::vector<float>& out_normals,
    std::vector<uint32_t>& out_indices);

/// @brief Deduplicate support points by XZ proximity, keeping the highest contact per cell.
void deduplicate_points(std::vector<support::SupportPoint>& points, float spacing);

/// @brief Adjust base positions to avoid mesh intersection.
///
/// For supports whose vertical path would pass through the model body,
/// offsets the base outward from the model center so the pillar angles
/// around the mesh. Uses the model's transformed bounding box.
void adjust_bases_for_mesh_avoidance(
    std::vector<support::SupportPoint>& points,
    const float* vertices,
    uint32_t vertex_count,
    const linalg_types::Mat4& transform);

/// @brief Snap contact points to actual model surface via downward ray-cast.
/// Must be called before deduplicate_points and rebuild_mesh.
void snap_contacts_to_surface(
    std::vector<support::SupportPoint>& points,
    const raycaster::MeshRaycaster& rc);

/// @brief Generate a single support pillar and append to the collection's mesh.
/// Uses the same logic as rebuild_mesh (vertical shaft + angled reach + surface-hit tip).
/// Returns false if no valid path was found.
bool append_single_support(support::SupportCollection& collection,
    const support::SupportPoint& point,
    const raycaster::MeshRaycaster* rc = nullptr);

/// @brief Regenerate the complete support mesh from all points.
/// If a raycaster is provided, supports whose shaft or reach path
/// intersects the model mesh are skipped or adjusted.
void rebuild_mesh(support::SupportCollection& collection,
    const raycaster::MeshRaycaster* rc = nullptr);

} // namespace support_gen
