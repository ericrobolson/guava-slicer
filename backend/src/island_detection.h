/// @file island_detection.h
/// @brief Pure functions for detecting unsupported regions (islands) across slice layers.
#pragma once

#include "slicer.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace island_detection {

/// @brief Weight for island count in the severity score formula.
constexpr float SEVERITY_WEIGHT_COUNT = 0.4f;

/// @brief Weight for normalized area in the severity score formula.
constexpr float SEVERITY_WEIGHT_AREA = 0.6f;

/// @brief Minimum intersection area (mm²) to consider a contour supported.
/// Below this threshold, the overlap is treated as negligible (e.g. touching at a point).
constexpr double SUPPORT_AREA_TOLERANCE = 0.001;

/// @brief Progress is reported every N layers to avoid IPC overhead.
constexpr uint32_t PROGRESS_INTERVAL = 50;

/// @brief A single unsupported contour within a layer.
struct IslandContour {
    uint32_t contour_index;
    double area;
};

/// @brief Island detection results for a single layer.
struct IslandLayer {
    uint32_t layer_index;
    std::vector<IslandContour> islands;
    double total_island_area;
    float severity;
};

/// @brief Complete result of island detection across all layers.
struct IslandResult {
    std::vector<IslandLayer> layers;
    uint32_t total_island_count;
    uint32_t worst_layer_index;
    float max_severity;
    std::vector<float> severity_scores;
};

/// @brief Callback for detection progress: (current_layer, total_layers).
using ProgressCallback = std::function<void(uint32_t, uint32_t)>;

/// @brief Detect unsupported contour regions across all slice layers.
///
/// For each layer N (starting at layer 1), tests each contour polygon against
/// the union of layer N-1's contour polygons using Clipper2 intersection.
/// A contour with zero (or negligible) intersection area is flagged as an island.
///
/// @param slice_result The cached slice result to analyze.
/// @param progress_cb  Optional progress callback.
/// @param cancel       Optional cancellation flag.
IslandResult detect(
    const slicer::SliceResult& slice_result,
    ProgressCallback progress_cb = nullptr,
    std::atomic<bool>* cancel = nullptr);

} // namespace island_detection
