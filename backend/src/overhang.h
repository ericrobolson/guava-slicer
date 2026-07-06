/// @file overhang.h
/// @brief Pure functions for overhang detection and auto-orientation.
#pragma once

#include "linalg_types.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace overhang {

using linalg_types::Vec3;
using linalg_types::Vec4;
using linalg_types::Mat4;

constexpr float DEFAULT_THRESHOLD_DEG = 45.0f;
constexpr float MIN_THRESHOLD_DEG = 0.0f;
constexpr float MAX_THRESHOLD_DEG = 90.0f;

/// @brief Result of overhang analysis on a mesh.
struct OverhangResult {
    std::vector<uint32_t> triangle_indices;
    float overhang_area = 0.0f;
    float total_area = 0.0f;
    float threshold_deg = 0.0f;
};

/// @brief Preferred rotation axis for auto-orientation.
enum class PreferredAxis : uint8_t {
    TILT_BACK,
    TILT_FORWARD,
    TILT_LEFT,
    TILT_RIGHT,
    ANY,
};

/// @brief Result of automatic orientation search.
struct AutoOrientResult {
    Vec4 best_rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    float overhang_area = 0.0f;
    float total_area = 0.0f;
    uint32_t samples_evaluated = 0;
    float threshold_deg = 0.0f;
    bool cancelled = false;
};

/// @brief Callback for auto-orient progress: (current_sample, total_samples).
using ProgressCallback = std::function<void(uint32_t, uint32_t)>;

/// @brief Detect overhang triangles on a mesh given a transform and angle threshold.
///
/// Transforms vertices by the given matrix, computes face normals in world space,
/// and flags triangles whose surface angle from vertical exceeds the threshold.
OverhangResult analyze(
    const float* vertices,
    const uint32_t* indices,
    uint32_t vertex_count,
    uint32_t triangle_count,
    const Mat4& transform,
    float threshold_deg);

/// @brief Find the rotation from identity that minimizes total overhang area.
///
/// Searches over candidate orientations biased toward the preferred axis,
/// then refines around the best candidate. Operates on raw (untransformed) mesh.
AutoOrientResult find_best_orientation(
    const float* vertices,
    const uint32_t* indices,
    uint32_t vertex_count,
    uint32_t triangle_count,
    float threshold_deg,
    PreferredAxis axis,
    ProgressCallback progress_cb = nullptr,
    std::atomic<bool>* cancel = nullptr);

/// @brief Parse a preferred axis string from IPC params.
///
/// Valid values: "tilt_back", "tilt_forward", "tilt_left", "tilt_right", "any".
/// Returns false if the string is not recognized.
bool parse_preferred_axis(const char* str, PreferredAxis& out);

} // namespace overhang
