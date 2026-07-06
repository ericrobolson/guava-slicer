/// @file island_detection.cpp
/// @brief Island detection algorithm using Clipper2 polygon intersection.
#include "island_detection.h"

#include <clipper2/clipper.h>

#include <algorithm>
#include <cmath>

namespace island_detection {

/// @brief Convert a slicer contour to a Clipper2 PathD.
static Clipper2Lib::PathD contour_to_path(const slicer::Contour& contour) {
    Clipper2Lib::PathD path;
    path.reserve(contour.points.size());
    for (const auto& pt : contour.points) {
        path.emplace_back(static_cast<double>(pt.x), static_cast<double>(pt.y));
    }
    return path;
}

/// @brief Build the union of all non-hole contours in a layer as Clipper2 PathsD.
static Clipper2Lib::PathsD build_layer_union(const slicer::SliceLayer& layer) {
    Clipper2Lib::PathsD subjects;
    Clipper2Lib::PathsD holes;

    for (const auto& contour : layer.contours) {
        auto path = contour_to_path(contour);
        if (contour.is_hole) {
            holes.push_back(std::move(path));
        } else {
            subjects.push_back(std::move(path));
        }
    }

    auto result = Clipper2Lib::Union(subjects, Clipper2Lib::FillRule::NonZero);

    if (!holes.empty()) {
        result = Clipper2Lib::Difference(result, holes, Clipper2Lib::FillRule::NonZero);
    }

    return result;
}

/// @brief Compute the absolute area of a Clipper2 PathD polygon.
static double path_area(const Clipper2Lib::PathD& path) {
    return std::abs(Clipper2Lib::Area(path));
}

/// @brief Compute the total area of a Clipper2 PathsD (sum of absolute areas).
static double paths_area(const Clipper2Lib::PathsD& paths) {
    double total = 0.0;
    for (const auto& p : paths) {
        total += path_area(p);
    }
    return total;
}

IslandResult detect(
    const slicer::SliceResult& slice_result,
    ProgressCallback progress_cb,
    std::atomic<bool>* cancel)
{
    IslandResult result;
    result.total_island_count = 0;
    result.worst_layer_index = 0;
    result.max_severity = 0.0f;

    uint32_t layer_count = slice_result.layer_count;
    result.severity_scores.resize(layer_count, 0.0f);

    if (layer_count <= 1) {
        return result;
    }

    double max_island_area = 0.0;
    uint32_t max_island_count = 0;

    Clipper2Lib::PathsD prev_union = build_layer_union(slice_result.layers[0]);

    for (uint32_t i = 1; i < layer_count; ++i) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            result.severity_scores.clear();
            return result;
        }

        if (progress_cb && (i % PROGRESS_INTERVAL == 0 || i == layer_count - 1)) {
            progress_cb(i, layer_count);
        }

        const auto& layer = slice_result.layers[i];
        Clipper2Lib::PathsD current_union = build_layer_union(layer);

        IslandLayer il;
        il.layer_index = i;
        il.total_island_area = 0.0;
        il.severity = 0.0f;

        for (uint32_t c = 0; c < layer.contours.size(); ++c) {
            if (layer.contours[c].is_hole) continue;

            Clipper2Lib::PathsD subject;
            subject.push_back(contour_to_path(layer.contours[c]));

            auto intersection = Clipper2Lib::Intersect(
                subject, prev_union, Clipper2Lib::FillRule::NonZero);

            double intersect_area = paths_area(intersection);
            double contour_area = path_area(subject[0]);

            if (intersect_area < SUPPORT_AREA_TOLERANCE) {
                il.islands.push_back({c, contour_area});
                il.total_island_area += contour_area;
            }
        }

        if (!il.islands.empty()) {
            uint32_t count = static_cast<uint32_t>(il.islands.size());
            if (il.total_island_area > max_island_area) {
                max_island_area = il.total_island_area;
            }
            if (count > max_island_count) {
                max_island_count = count;
            }
            result.total_island_count += count;
            result.layers.push_back(std::move(il));
        }

        prev_union = std::move(current_union);
    }

    if (max_island_count == 0) {
        return result;
    }

    float worst_severity = 0.0f;
    for (auto& il : result.layers) {
        float count_norm = static_cast<float>(il.islands.size()) / static_cast<float>(max_island_count);
        float area_norm = static_cast<float>(il.total_island_area / max_island_area);
        float severity = SEVERITY_WEIGHT_COUNT * count_norm + SEVERITY_WEIGHT_AREA * area_norm;
        il.severity = severity;

        result.severity_scores[il.layer_index] = severity;

        if (severity > worst_severity) {
            worst_severity = severity;
            result.worst_layer_index = il.layer_index;
        }
    }

    result.max_severity = worst_severity;

    return result;
}

} // namespace island_detection
