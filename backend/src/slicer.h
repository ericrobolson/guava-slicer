/// @file slicer.h
/// @brief Pure functions for mesh-plane intersection and contour assembly.
#pragma once

#include "linalg_types.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace slicer {

using linalg_types::Mat4;

constexpr float DEFAULT_LAYER_HEIGHT = 0.06f;
constexpr float MIN_LAYER_HEIGHT = 0.01f;
constexpr float MAX_LAYER_HEIGHT = 1.0f;

/// @brief Intersection epsilon — vertices within this distance of the slice plane
/// are classified as "above" to avoid degenerate edge cases.
constexpr float PLANE_EPSILON = 1e-6f;

/// @brief Progress is reported every N layers to avoid IPC overhead.
constexpr uint32_t PROGRESS_INTERVAL = 100;

/// @brief 2D point in the XY plane (projected from a Z-plane intersection).
struct Point2D {
    float x;
    float y;
};

/// @brief A closed contour polygon from slicing.
struct Contour {
    std::vector<Point2D> points;
    bool is_hole;
};

/// @brief All contours at a single Z height.
struct SliceLayer {
    float z_height;
    std::vector<Contour> contours;
};

/// @brief Complete result of slicing a mesh.
struct SliceResult {
    std::vector<SliceLayer> layers;
    float layer_height;
    uint32_t layer_count;
    uint32_t warning_count;
};

/// @brief Callback for slice progress: (current_layer, total_layers).
using ProgressCallback = std::function<void(uint32_t, uint32_t)>;

/// @brief Slice a mesh by intersecting with horizontal Y-planes.
///
/// Transforms vertices by the given matrix, then intersects with Y-planes
/// (Y is the vertical axis — build plate is at Y=0).
/// First layer is at y_min + layer_height/2 to avoid boundary degeneracy.
///
/// @param vertices     Flat vertex array [x0,y0,z0, x1,y1,z1, ...].
/// @param indices      Triangle index array [i0,i1,i2, ...].
/// @param vertex_count Number of vertices.
/// @param triangle_count Number of triangles.
/// @param transform    4x4 transform matrix (object → world space).
/// @param layer_height Distance between slice planes in mm.
/// @param progress_cb  Optional progress callback.
/// @param cancel       Optional cancellation flag.
SliceResult slice_mesh(
    const float* vertices,
    const uint32_t* indices,
    uint32_t vertex_count,
    uint32_t triangle_count,
    const Mat4& transform,
    float layer_height,
    ProgressCallback progress_cb = nullptr,
    std::atomic<bool>* cancel = nullptr);

} // namespace slicer
