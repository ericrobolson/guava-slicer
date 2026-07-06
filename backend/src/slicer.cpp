/// @file slicer.cpp
/// @brief Mesh-plane intersection and contour assembly implementation.
#include "slicer.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace slicer {

namespace {

/// @brief Pack two vertex indices into a canonical edge key (smaller index first).
uint64_t edge_key(uint32_t a, uint32_t b) {
    return a < b ? (static_cast<uint64_t>(a) << 32 | b)
                 : (static_cast<uint64_t>(b) << 32 | a);
}

/// @brief A line segment produced by intersecting a triangle with a Z-plane.
struct Segment {
    Point2D start;
    Point2D end;
    uint64_t start_edge;
    uint64_t end_edge;
};

/// @brief Y-axis index in the vertex stride.
constexpr int Y_AXIS = 1;
/// @brief X-axis index in the vertex stride.
constexpr int X_AXIS = 0;
/// @brief Z-axis index in the vertex stride.
constexpr int Z_AXIS = 2;

/// @brief Intersect a single triangle with a Y-plane.
///
/// Classifies each vertex as above or below the plane (vertices within
/// PLANE_EPSILON are treated as above). If the triangle straddles the plane,
/// computes the two intersection points on the crossing edges, projected
/// onto the XZ plane.
///
/// @return true if a valid segment was produced.
bool intersect_triangle_y(
    const float* verts,
    uint32_t i0, uint32_t i1, uint32_t i2,
    float y,
    Segment& out)
{
    float y0 = verts[i0 * 3 + Y_AXIS];
    float y1 = verts[i1 * 3 + Y_AXIS];
    float y2 = verts[i2 * 3 + Y_AXIS];

    auto classify = [y](float vy) -> int {
        if (vy > y + PLANE_EPSILON) return 1;
        if (vy < y - PLANE_EPSILON) return -1;
        return 1;
    };

    int c0 = classify(y0);
    int c1 = classify(y1);
    int c2 = classify(y2);

    bool has_above = (c0 > 0 || c1 > 0 || c2 > 0);
    bool has_below = (c0 < 0 || c1 < 0 || c2 < 0);
    if (!has_above || !has_below) return false;

    uint32_t vidx[3] = {i0, i1, i2};
    float yv[3] = {y0, y1, y2};
    int cv[3] = {c0, c1, c2};

    Point2D pts[2];
    uint64_t edges[2];
    int count = 0;

    for (int e = 0; e < 3 && count < 2; ++e) {
        int a = e;
        int b = (e + 1) % 3;

        if (cv[a] != cv[b]) {
            float t = (y - yv[a]) / (yv[b] - yv[a]);
            float ax = verts[vidx[a] * 3 + X_AXIS];
            float az = verts[vidx[a] * 3 + Z_AXIS];
            float bx = verts[vidx[b] * 3 + X_AXIS];
            float bz = verts[vidx[b] * 3 + Z_AXIS];

            pts[count] = {ax + t * (bx - ax), az + t * (bz - az)};
            edges[count] = edge_key(vidx[a], vidx[b]);
            count++;
        }
    }

    if (count != 2) return false;

    out.start = pts[0];
    out.end = pts[1];
    out.start_edge = edges[0];
    out.end_edge = edges[1];
    return true;
}

/// @brief Compute signed area of a 2D polygon (positive = CCW, negative = CW).
float signed_area(const std::vector<Point2D>& points) {
    float area = 0.0f;
    size_t n = points.size();
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        area += points[i].x * points[j].y;
        area -= points[j].x * points[i].y;
    }
    return area * 0.5f;
}

/// @brief Minimum number of points for a valid contour.
constexpr size_t MIN_CONTOUR_POINTS = 3;

/// @brief Assemble line segments into closed contour polygons using edge connectivity.
///
/// For manifold meshes, each crossing mesh edge appears in exactly two segments.
/// Segments are chained by matching their edge keys until loops close.
std::vector<Contour> assemble_contours(const std::vector<Segment>& segments) {
    if (segments.empty()) return {};

    std::unordered_map<uint64_t, std::vector<std::pair<size_t, bool>>> edge_map;

    for (size_t i = 0; i < segments.size(); ++i) {
        edge_map[segments[i].start_edge].push_back({i, true});
        edge_map[segments[i].end_edge].push_back({i, false});
    }

    std::vector<bool> visited(segments.size(), false);
    std::vector<Contour> contours;

    for (size_t seed = 0; seed < segments.size(); ++seed) {
        if (visited[seed]) continue;

        Contour contour;
        size_t cur = seed;
        bool cur_at_start = true;

        while (true) {
            visited[cur] = true;

            const Point2D& entry = cur_at_start
                ? segments[cur].start
                : segments[cur].end;
            contour.points.push_back(entry);

            uint64_t exit_edge = cur_at_start
                ? segments[cur].end_edge
                : segments[cur].start_edge;

            size_t next = SIZE_MAX;
            bool next_at_start = false;

            auto it = edge_map.find(exit_edge);
            if (it != edge_map.end()) {
                for (const auto& [idx, is_start] : it->second) {
                    if (idx != cur && !visited[idx]) {
                        next = idx;
                        next_at_start = is_start;
                        break;
                    }
                }
            }

            if (next == SIZE_MAX) break;

            cur = next;
            cur_at_start = next_at_start;
        }

        if (contour.points.size() >= MIN_CONTOUR_POINTS) {
            float area = signed_area(contour.points);
            contour.is_hole = (area > 0.0f);
            contours.push_back(std::move(contour));
        }
    }

    return contours;
}

} // anonymous namespace

SliceResult slice_mesh(
    const float* vertices,
    const uint32_t* indices,
    uint32_t vertex_count,
    uint32_t triangle_count,
    const Mat4& transform,
    float layer_height,
    ProgressCallback progress_cb,
    std::atomic<bool>* cancel)
{
    SliceResult result;
    result.layer_height = layer_height;
    result.warning_count = 0;

    if (vertex_count == 0 || triangle_count == 0) {
        result.layer_count = 0;
        return result;
    }

    std::vector<float> xformed(vertex_count * 3);
    for (uint32_t i = 0; i < vertex_count; ++i) {
        float x = vertices[i * 3 + 0];
        float y = vertices[i * 3 + 1];
        float z = vertices[i * 3 + 2];

        xformed[i * 3 + 0] = transform[0][0] * x + transform[1][0] * y + transform[2][0] * z + transform[3][0];
        xformed[i * 3 + 1] = transform[0][1] * x + transform[1][1] * y + transform[2][1] * z + transform[3][1];
        xformed[i * 3 + 2] = transform[0][2] * x + transform[1][2] * y + transform[2][2] * z + transform[3][2];
    }

    float y_min = xformed[Y_AXIS];
    float y_max = xformed[Y_AXIS];
    for (uint32_t i = 1; i < vertex_count; ++i) {
        float y = xformed[i * 3 + Y_AXIS];
        y_min = std::min(y_min, y);
        y_max = std::max(y_max, y);
    }

    const float y_start = y_min + layer_height * 0.5f;
    uint32_t layer_count = 0;
    if (y_start < y_max) {
        layer_count = static_cast<uint32_t>((y_max - y_start) / layer_height) + 1;
    }

    result.layer_count = layer_count;
    result.layers.resize(layer_count);

    for (uint32_t layer_idx = 0; layer_idx < layer_count; ++layer_idx) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;

        float y = y_start + layer_idx * layer_height;
        result.layers[layer_idx].z_height = y;

        std::vector<Segment> segments;
        for (uint32_t t = 0; t < triangle_count; ++t) {
            uint32_t i0 = indices[t * 3 + 0];
            uint32_t i1 = indices[t * 3 + 1];
            uint32_t i2 = indices[t * 3 + 2];

            Segment seg;
            if (intersect_triangle_y(xformed.data(), i0, i1, i2, y, seg)) {
                segments.push_back(seg);
            }
        }

        result.layers[layer_idx].contours = assemble_contours(segments);

        if (progress_cb && (layer_idx % PROGRESS_INTERVAL == 0 || layer_idx == layer_count - 1)) {
            progress_cb(layer_idx + 1, layer_count);
        }
    }

    return result;
}

} // namespace slicer
