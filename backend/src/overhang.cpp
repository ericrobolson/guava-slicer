/// @file overhang.cpp
/// @brief Overhang detection and auto-orientation implementation.
#include "overhang.h"

#include <linalg/linalg.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace overhang {

constexpr float PI = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI / 180.0f;
constexpr float DEGENERATE_AREA_THRESHOLD = 1e-10f;

constexpr float PRIMARY_STEP_DEG = 10.0f;
constexpr float SECONDARY_STEP_DEG = 15.0f;
constexpr float SECONDARY_RANGE_DEG = 45.0f;
constexpr float FINE_STEP_DEG = 2.0f;
constexpr float FINE_RANGE_DEG = 8.0f;
constexpr uint32_t FIBONACCI_SAMPLES = 500;
constexpr uint32_t COARSE_STRIDE = 4;
constexpr uint32_t PROGRESS_INTERVAL = 200;

constexpr float W_OVERHANG = 1.0f;
constexpr float W_BOTTOM = 0.5f;
constexpr float W_HEIGHT = 0.15f;
constexpr float W_TILT = 0.1f;
constexpr float BOTTOM_CONTACT_THRESHOLD = 0.5f;
constexpr float BOTTOM_NORMAL_LIMIT = -0.7f;
constexpr float PREFERRED_TILT_DEG = 45.0f;

static const Vec3 UP = {0.0f, 1.0f, 0.0f};

/// @brief Pre-computed per-face data for auto-orient evaluation.
struct FaceData {
    Vec3 normal;
    Vec3 centroid;
    float area;
};

/// @brief Compute face data from three vertex positions.
static FaceData compute_face(const float* verts, uint32_t i0, uint32_t i1, uint32_t i2) {
    Vec3 v0 = {verts[i0 * 3], verts[i0 * 3 + 1], verts[i0 * 3 + 2]};
    Vec3 v1 = {verts[i1 * 3], verts[i1 * 3 + 1], verts[i1 * 3 + 2]};
    Vec3 v2 = {verts[i2 * 3], verts[i2 * 3 + 1], verts[i2 * 3 + 2]};
    Vec3 e1 = v1 - v0;
    Vec3 e2 = v2 - v0;
    Vec3 cross = linalg::cross(e1, e2);
    float len = linalg::length(cross);
    FaceData fd;
    fd.area = len * 0.5f;
    fd.normal = (len > DEGENERATE_AREA_THRESHOLD) ? cross / len : Vec3{0, 0, 0};
    fd.centroid = (v0 + v1 + v2) / 3.0f;
    return fd;
}

/// @brief Pre-compute face data for all triangles.
static std::vector<FaceData> precompute_faces(
    const float* vertices, const uint32_t* indices, uint32_t triangle_count) {
    std::vector<FaceData> faces(triangle_count);
    for (uint32_t t = 0; t < triangle_count; ++t) {
        faces[t] = compute_face(
            vertices, indices[t * 3], indices[t * 3 + 1], indices[t * 3 + 2]);
    }
    return faces;
}

/// @brief Compute total surface area from pre-computed faces.
static float total_surface_area(const std::vector<FaceData>& faces) {
    float total = 0.0f;
    for (const auto& f : faces) {
        total += f.area;
    }
    return total;
}

/// @brief Compute maximum bounding box dimension from raw vertices.
static float compute_max_dimension(const float* vertices, uint32_t vertex_count) {
    if (vertex_count == 0) return 1.0f;
    float xmin = vertices[0], xmax = vertices[0];
    float ymin = vertices[1], ymax = vertices[1];
    float zmin = vertices[2], zmax = vertices[2];
    for (uint32_t i = 1; i < vertex_count; ++i) {
        float x = vertices[i * 3], y = vertices[i * 3 + 1], z = vertices[i * 3 + 2];
        if (x < xmin) xmin = x; if (x > xmax) xmax = x;
        if (y < ymin) ymin = y; if (y > ymax) ymax = y;
        if (z < zmin) zmin = z; if (z > zmax) zmax = z;
    }
    return std::max({xmax - xmin, ymax - ymin, zmax - zmin});
}

/// @brief Compute the preferred "up" direction for a given tilt axis.
static Vec3 compute_preferred_up(PreferredAxis axis) {
    const float tilt_rad = PREFERRED_TILT_DEG * DEG_TO_RAD;
    float c = std::cos(tilt_rad);
    float s = std::sin(tilt_rad);
    switch (axis) {
        case PreferredAxis::TILT_BACK:    return {0, c, -s};
        case PreferredAxis::TILT_FORWARD: return {0, c, s};
        case PreferredAxis::TILT_LEFT:    return {-s, c, 0};
        case PreferredAxis::TILT_RIGHT:   return {s, c, 0};
        default:                          return UP;
    }
}

/// @brief Result of multi-objective orientation evaluation.
struct OrientScore {
    float cost;
    float overhang_area;
};

/// @brief Evaluate a candidate orientation using multi-objective cost.
///
/// Cost = w_overhang * overhang - w_bottom * bottom_area + w_height * height - w_tilt * tilt.
/// Lower cost is better. Bottom area and tilt bias prevent upside-down results.
static OrientScore evaluate_orientation(
    const std::vector<FaceData>& faces, const Vec4& rotation,
    float cos_limit, float total_area, float max_dim,
    const Vec3& preferred_up, bool has_preferred, uint32_t stride) {

    const float scale = static_cast<float>(stride);
    const size_t sample_count = (faces.size() + stride - 1) / stride;

    // Pass 1: rotate centroids, cache Y values, find min/max
    std::vector<float> centroid_y(sample_count);
    float min_y = FLT_MAX, max_y = -FLT_MAX;
    for (size_t i = 0, si = 0; i < faces.size(); i += stride, ++si) {
        float cy = linalg::qrot(rotation, faces[i].centroid).y;
        centroid_y[si] = cy;
        if (cy < min_y) min_y = cy;
        if (cy > max_y) max_y = cy;
    }

    float height = max_y - min_y;
    float bottom_y_limit = min_y + BOTTOM_CONTACT_THRESHOLD;

    // Pass 2: compute overhang area and bottom contact area (reuse cached Y)
    float overhang = 0.0f;
    float bottom = 0.0f;
    for (size_t i = 0, si = 0; i < faces.size(); i += stride, ++si) {
        const auto& f = faces[i];
        if (f.area < DEGENERATE_AREA_THRESHOLD) continue;

        float dot_up = linalg::qrot(rotation, f.normal).y;

        if (dot_up < cos_limit) {
            overhang += f.area;
        }

        if (centroid_y[si] < bottom_y_limit && dot_up < BOTTOM_NORMAL_LIMIT) {
            bottom += f.area;
        }
    }

    overhang *= scale;
    bottom *= scale;

    float tilt = 0.0f;
    if (has_preferred) {
        Vec3 effective_up = linalg::qrot(rotation, UP);
        tilt = linalg::dot(effective_up, preferred_up);
    }

    float safe_area = (total_area > DEGENERATE_AREA_THRESHOLD) ? total_area : 1.0f;
    float safe_dim = (max_dim > DEGENERATE_AREA_THRESHOLD) ? max_dim : 1.0f;

    float cost = W_OVERHANG * (overhang / safe_area)
               - W_BOTTOM * (bottom / safe_area)
               + W_HEIGHT * (height / safe_dim)
               - W_TILT * tilt;

    return {cost, overhang};
}

/// @brief Compute quaternion rotating direction `from` to direction `to`.
static Vec4 rotation_between(Vec3 from, Vec3 to) {
    float dot = linalg::dot(from, to);
    if (dot > 0.9999f) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    if (dot < -0.9999f) {
        Vec3 perp = (std::abs(from.x) < 0.9f)
                        ? linalg::normalize(linalg::cross(from, Vec3{1, 0, 0}))
                        : linalg::normalize(linalg::cross(from, Vec3{0, 1, 0}));
        return {perp.x, perp.y, perp.z, 0.0f};
    }
    Vec3 cross = linalg::cross(from, to);
    float w = 1.0f + dot;
    return linalg::normalize(Vec4{cross.x, cross.y, cross.z, w});
}

/// @brief Map preferred axis enum to its primary rotation axis vector.
static Vec3 primary_axis(PreferredAxis axis) {
    switch (axis) {
        case PreferredAxis::TILT_BACK:
        case PreferredAxis::TILT_FORWARD:
            return {1, 0, 0};
        case PreferredAxis::TILT_LEFT:
        case PreferredAxis::TILT_RIGHT:
            return {0, 0, 1};
        default:
            return {0, 1, 0};
    }
}

/// @brief Map preferred axis enum to the two secondary rotation axes.
static void secondary_axes(PreferredAxis axis, Vec3& s1, Vec3& s2) {
    switch (axis) {
        case PreferredAxis::TILT_BACK:
        case PreferredAxis::TILT_FORWARD:
            s1 = {0, 1, 0};
            s2 = {0, 0, 1};
            break;
        case PreferredAxis::TILT_LEFT:
        case PreferredAxis::TILT_RIGHT:
            s1 = {1, 0, 0};
            s2 = {0, 1, 0};
            break;
        default:
            s1 = {1, 0, 0};
            s2 = {0, 0, 1};
            break;
    }
}

/// @brief Count the total candidates for preferred-axis coarse sweep.
static uint32_t count_preferred_candidates() {
    uint32_t primary_steps = static_cast<uint32_t>(360.0f / PRIMARY_STEP_DEG);
    uint32_t secondary_steps = static_cast<uint32_t>(2.0f * SECONDARY_RANGE_DEG / SECONDARY_STEP_DEG) + 1;
    return primary_steps * secondary_steps * secondary_steps;
}

/// @brief Count the total candidates for fine refinement sweep.
static uint32_t count_fine_candidates() {
    uint32_t fine_steps = static_cast<uint32_t>(2.0f * FINE_RANGE_DEG / FINE_STEP_DEG) + 1;
    return fine_steps * fine_steps * fine_steps;
}

// --- Public API ---

OverhangResult analyze(
    const float* vertices,
    const uint32_t* indices,
    uint32_t /*vertex_count*/,
    uint32_t triangle_count,
    const Mat4& transform,
    float threshold_deg) {

    OverhangResult result;
    result.threshold_deg = threshold_deg;

    const float cos_limit = -std::sin(threshold_deg * DEG_TO_RAD);

    for (uint32_t t = 0; t < triangle_count; ++t) {
        uint32_t i0 = indices[t * 3 + 0];
        uint32_t i1 = indices[t * 3 + 1];
        uint32_t i2 = indices[t * 3 + 2];

        Vec4 v0 = linalg::mul(transform, Vec4{vertices[i0 * 3], vertices[i0 * 3 + 1], vertices[i0 * 3 + 2], 1.0f});
        Vec4 v1 = linalg::mul(transform, Vec4{vertices[i1 * 3], vertices[i1 * 3 + 1], vertices[i1 * 3 + 2], 1.0f});
        Vec4 v2 = linalg::mul(transform, Vec4{vertices[i2 * 3], vertices[i2 * 3 + 1], vertices[i2 * 3 + 2], 1.0f});

        Vec3 e1 = {v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
        Vec3 e2 = {v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
        Vec3 cross = linalg::cross(e1, e2);
        float cross_len = linalg::length(cross);
        float area = cross_len * 0.5f;

        if (cross_len < DEGENERATE_AREA_THRESHOLD) continue;

        Vec3 normal = cross / cross_len;
        result.total_area += area;

        if (linalg::dot(normal, UP) < cos_limit) {
            result.triangle_indices.push_back(t);
            result.overhang_area += area;
        }
    }

    return result;
}

AutoOrientResult find_best_orientation(
    const float* vertices,
    const uint32_t* indices,
    uint32_t vertex_count,
    uint32_t triangle_count,
    float threshold_deg,
    PreferredAxis axis,
    ProgressCallback progress_cb,
    std::atomic<bool>* cancel) {

    AutoOrientResult result;
    result.threshold_deg = threshold_deg;

    const float cos_limit = -std::sin(threshold_deg * DEG_TO_RAD);
    auto faces = precompute_faces(vertices, indices, triangle_count);
    float area = total_surface_area(faces);
    float max_dim = compute_max_dimension(vertices, vertex_count);
    result.total_area = area;

    bool has_preferred = (axis != PreferredAxis::ANY);
    Vec3 preferred_up = compute_preferred_up(axis);

    Vec4 best_q = {0.0f, 0.0f, 0.0f, 1.0f};
    auto initial = evaluate_orientation(faces, best_q, cos_limit, area, max_dim,
                                         preferred_up, has_preferred, COARSE_STRIDE);
    float best_cost = initial.cost;
    uint32_t sample_count = 0;

    uint32_t total_coarse = (axis == PreferredAxis::ANY)
                                ? FIBONACCI_SAMPLES
                                : count_preferred_candidates();
    uint32_t total_fine = count_fine_candidates();
    uint32_t total_samples = total_coarse + total_fine;

    auto check_cancel = [&]() -> bool {
        return cancel && cancel->load(std::memory_order_relaxed);
    };

    auto report_progress = [&]() {
        if (progress_cb && (sample_count % PROGRESS_INTERVAL == 0)) {
            progress_cb(sample_count, total_samples);
        }
    };

    uint32_t current_stride = COARSE_STRIDE;

    auto try_candidate = [&](const Vec4& q) {
        auto score = evaluate_orientation(faces, q, cos_limit, area, max_dim,
                                           preferred_up, has_preferred, current_stride);
        ++sample_count;
        if (score.cost < best_cost) {
            best_cost = score.cost;
            best_q = q;
        }
        report_progress();
    };

    // --- Coarse phase ---

    if (axis == PreferredAxis::ANY) {
        const float golden_ratio = (1.0f + std::sqrt(5.0f)) / 2.0f;
        for (uint32_t i = 0; i < FIBONACCI_SAMPLES; ++i) {
            if (check_cancel()) { result.cancelled = true; return result; }
            float theta = std::acos(1.0f - 2.0f * (i + 0.5f) / FIBONACCI_SAMPLES);
            float phi = 2.0f * PI * i / golden_ratio;
            Vec3 dir = {
                std::sin(theta) * std::cos(phi),
                std::cos(theta),
                std::sin(theta) * std::sin(phi)};
            Vec4 q = rotation_between(UP, dir);
            try_candidate(q);
        }
    } else {
        Vec3 prim = primary_axis(axis);
        Vec3 sec1, sec2;
        secondary_axes(axis, sec1, sec2);

        for (float a1 = 0.0f; a1 < 360.0f; a1 += PRIMARY_STEP_DEG) {
            if (check_cancel()) { result.cancelled = true; return result; }
            Vec4 q1 = linalg::rotation_quat(prim, a1 * DEG_TO_RAD);

            for (float a2 = -SECONDARY_RANGE_DEG; a2 <= SECONDARY_RANGE_DEG; a2 += SECONDARY_STEP_DEG) {
                Vec4 q2 = linalg::rotation_quat(sec1, a2 * DEG_TO_RAD);
                Vec4 q12 = linalg::qmul(q2, q1);

                for (float a3 = -SECONDARY_RANGE_DEG; a3 <= SECONDARY_RANGE_DEG; a3 += SECONDARY_STEP_DEG) {
                    Vec4 q3 = linalg::rotation_quat(sec2, a3 * DEG_TO_RAD);
                    Vec4 q123 = linalg::qmul(q3, q12);
                    try_candidate(q123);
                }
            }
        }
    }

    // --- Fine refinement around best ---

    current_stride = 1;
    best_cost = evaluate_orientation(faces, best_q, cos_limit, area, max_dim,
                                      preferred_up, has_preferred, 1).cost;

    Vec4 coarse_best = best_q;
    for (float dx = -FINE_RANGE_DEG; dx <= FINE_RANGE_DEG; dx += FINE_STEP_DEG) {
        if (check_cancel()) { result.cancelled = true; return result; }
        Vec4 qx = linalg::rotation_quat(Vec3{1, 0, 0}, dx * DEG_TO_RAD);

        for (float dy = -FINE_RANGE_DEG; dy <= FINE_RANGE_DEG; dy += FINE_STEP_DEG) {
            Vec4 qy = linalg::rotation_quat(Vec3{0, 1, 0}, dy * DEG_TO_RAD);

            for (float dz = -FINE_RANGE_DEG; dz <= FINE_RANGE_DEG; dz += FINE_STEP_DEG) {
                Vec4 qz = linalg::rotation_quat(Vec3{0, 0, 1}, dz * DEG_TO_RAD);
                Vec4 delta = linalg::qmul(qz, linalg::qmul(qy, qx));
                Vec4 candidate = linalg::qmul(delta, coarse_best);
                try_candidate(candidate);
            }
        }
    }

    // Final full-precision evaluation for the result
    auto final_score = evaluate_orientation(faces, best_q, cos_limit, area, max_dim,
                                             preferred_up, has_preferred, 1);
    result.best_rotation = best_q;
    result.overhang_area = final_score.overhang_area;
    result.samples_evaluated = sample_count;
    return result;
}

bool parse_preferred_axis(const char* str, PreferredAxis& out) {
    if (std::strcmp(str, "tilt_back") == 0) { out = PreferredAxis::TILT_BACK; return true; }
    if (std::strcmp(str, "tilt_forward") == 0) { out = PreferredAxis::TILT_FORWARD; return true; }
    if (std::strcmp(str, "tilt_left") == 0) { out = PreferredAxis::TILT_LEFT; return true; }
    if (std::strcmp(str, "tilt_right") == 0) { out = PreferredAxis::TILT_RIGHT; return true; }
    if (std::strcmp(str, "any") == 0) { out = PreferredAxis::ANY; return true; }
    return false;
}

} // namespace overhang
