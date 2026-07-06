/// @file support_gen.cpp
/// @brief Tree-style support generation: vertical trunks around model perimeter,
/// horizontal branches to contact points, cross-bracing between trunks, raft base.
#include "support_gen.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace support_gen {

using linalg_types::Vec3;
using linalg_types::Mat4;

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;
constexpr float SUPPORT_OFFSET_DIST = 2.5f;
constexpr float MAX_OFFSET_MULTIPLIER = 4.0f;
constexpr uint32_t OFFSET_ATTEMPTS = 4;
constexpr float CONTACT_TIP_CONE_LENGTH = 1.2f;
constexpr float BRACE_VERTICAL_SPACING = 8.0f;
constexpr float BRACE_DIAMETER_RATIO = 0.5f;
constexpr float RAFT_THICKNESS = 1.5f;
constexpr float RAFT_MARGIN = 2.0f;
constexpr uint32_t RAFT_SEGMENTS = 32;

// --- Helpers ---

static Vec3 transform_point(const Vec3& p, const Mat4& m) {
    return {
        m[0][0] * p.x + m[1][0] * p.y + m[2][0] * p.z + m[3][0],
        m[0][1] * p.x + m[1][1] * p.y + m[2][1] * p.z + m[3][1],
        m[0][2] * p.x + m[1][2] * p.y + m[2][2] * p.z + m[3][2],
    };
}

static Vec3 vertex_at(const float* verts, uint32_t idx) {
    return {verts[idx * 3], verts[idx * 3 + 1], verts[idx * 3 + 2]};
}

static Vec3 face_normal(const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 e1 = b - a;
    Vec3 e2 = c - a;
    Vec3 n = linalg::cross(e1, e2);
    float len = linalg::length(n);
    if (len < 1e-12f) return {0, 1, 0};
    return n / len;
}

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

struct GridKey {
    int32_t cx, cz;
    bool operator==(const GridKey& o) const { return cx == o.cx && cz == o.cz; }
};
struct GridHash {
    size_t operator()(const GridKey& k) const {
        return std::hash<int64_t>()(
            (static_cast<int64_t>(k.cx) << 32) | static_cast<int64_t>(k.cz & 0xFFFFFFFF));
    }
};
static GridKey grid_cell(float x, float z, float inv_spacing) {
    return {
        static_cast<int32_t>(std::floor(x * inv_spacing)),
        static_cast<int32_t>(std::floor(z * inv_spacing)),
    };
}

// --- Sampling (unchanged) ---

std::vector<support::SupportPoint> sample_island_points(
    const island_detection::IslandResult& islands,
    const slicer::SliceResult& slices,
    const float*, const uint32_t*, uint32_t, uint32_t,
    const linalg_types::Mat4&, uint32_t& next_id)
{
    std::vector<support::SupportPoint> result;
    for (const auto& layer : islands.layers) {
        if (layer.layer_index >= slices.layers.size()) continue;
        const auto& sl = slices.layers[layer.layer_index];
        float y = sl.z_height;
        for (const auto& island : layer.islands) {
            if (island.contour_index >= sl.contours.size()) continue;
            const auto& contour = sl.contours[island.contour_index];
            if (contour.points.empty()) continue;
            float cx = 0, cz = 0;
            for (const auto& pt : contour.points) { cx += pt.x; cz += pt.y; }
            cx /= static_cast<float>(contour.points.size());
            cz /= static_cast<float>(contour.points.size());
            support::SupportPoint sp;
            sp.position = {cx, y, cz};
            sp.normal = {0, -1, 0};
            sp.ground_pos = {cx, 0, cz};
            sp.category = support::Category::ISLAND;
            sp.id = next_id++;
            result.push_back(sp);
        }
    }
    return result;
}

std::vector<support::SupportPoint> sample_reinforcement_points(
    const island_detection::IslandResult& islands,
    const slicer::SliceResult& slices, uint32_t& next_id)
{
    std::vector<support::SupportPoint> result;
    for (const auto& layer : islands.layers) {
        if (layer.layer_index >= slices.layers.size()) continue;
        const auto& sl = slices.layers[layer.layer_index];
        float y = sl.z_height;
        for (const auto& island : layer.islands) {
            if (island.contour_index >= sl.contours.size()) continue;
            const auto& contour = sl.contours[island.contour_index];
            if (contour.points.size() < 3) continue;
            float cx = 0, cz = 0;
            for (const auto& pt : contour.points) { cx += pt.x; cz += pt.y; }
            cx /= static_cast<float>(contour.points.size());
            cz /= static_cast<float>(contour.points.size());
            float radius = std::sqrt(static_cast<float>(island.area) / PI) *
                           support::REINFORCEMENT_RING_RADIUS;
            if (radius < 0.5f) radius = 0.5f;
            for (uint32_t i = 0; i < support::REINFORCEMENT_POINTS_PER_ISLAND; ++i) {
                float angle = TWO_PI * static_cast<float>(i) /
                              static_cast<float>(support::REINFORCEMENT_POINTS_PER_ISLAND);
                support::SupportPoint sp;
                sp.position = {cx + radius * std::cos(angle), y, cz + radius * std::sin(angle)};
                sp.normal = {0, -1, 0};
                sp.ground_pos = {sp.position.x, 0, sp.position.z};
                sp.category = support::Category::REINFORCEMENT;
                sp.id = next_id++;
                result.push_back(sp);
            }
        }
    }
    return result;
}

std::vector<support::SupportPoint> sample_overhang_points(
    const overhang::OverhangResult& overhangs,
    const float* vertices, const uint32_t* indices,
    uint32_t vertex_count, const linalg_types::Mat4& transform,
    float spacing, uint32_t& next_id)
{
    std::vector<support::SupportPoint> result;
    if (overhangs.triangle_indices.empty() || vertex_count == 0) return result;
    float inv_spacing = 1.0f / spacing;
    std::unordered_map<GridKey, bool, GridHash> occupied;
    for (uint32_t ti : overhangs.triangle_indices) {
        Vec3 v0 = transform_point(vertex_at(vertices, indices[ti*3+0]), transform);
        Vec3 v1 = transform_point(vertex_at(vertices, indices[ti*3+1]), transform);
        Vec3 v2 = transform_point(vertex_at(vertices, indices[ti*3+2]), transform);
        Vec3 n = face_normal(v0, v1, v2);
        Vec3 centroid = (v0 + v1 + v2) * (1.0f / 3.0f);
        if (centroid.y < support::MIN_PILLAR_HEIGHT) continue;
        GridKey key = grid_cell(centroid.x, centroid.z, inv_spacing);
        if (occupied.count(key)) continue;
        occupied[key] = true;
        support::SupportPoint sp;
        sp.position = centroid;
        sp.normal = n;
        sp.ground_pos = {centroid.x, 0, centroid.z};
        sp.category = support::Category::OVERHANG;
        sp.id = next_id++;
        result.push_back(sp);
    }
    return result;
}

std::vector<support::SupportPoint> sample_stabilization_points(
    const float* vertices, const uint32_t*, uint32_t vertex_count,
    uint32_t, const linalg_types::Mat4& transform,
    float spacing, uint32_t& next_id)
{
    std::vector<support::SupportPoint> result;
    if (vertex_count == 0) return result;
    Vec3 bmin = {1e30f, 1e30f, 1e30f}, bmax = {-1e30f, -1e30f, -1e30f};
    for (uint32_t i = 0; i < vertex_count; ++i) {
        Vec3 p = transform_point(vertex_at(vertices, i), transform);
        bmin = linalg::min(bmin, p); bmax = linalg::max(bmax, p);
    }
    float height = bmax.y - bmin.y;
    float min_w = std::min(bmax.x - bmin.x, bmax.z - bmin.z);
    if (min_w < 1e-6f || height / min_w < support::STABILIZATION_ASPECT_RATIO) return result;
    uint32_t levels = std::max(1u, static_cast<uint32_t>(height / support::STABILIZATION_VERTICAL_SPACING));
    float cx = (bmin.x+bmax.x)*0.5f, cz = (bmin.z+bmax.z)*0.5f;
    float off = min_w*0.5f + spacing*0.5f;
    float ox[4] = {cx+off, cx-off, cx, cx};
    float oz[4] = {cz, cz, cz+off, cz-off};
    for (uint32_t lv = 1; lv <= levels; ++lv) {
        float y = bmin.y + static_cast<float>(lv) * support::STABILIZATION_VERTICAL_SPACING;
        if (y >= bmax.y) break;
        for (int p = 0; p < 4; ++p) {
            support::SupportPoint sp;
            sp.position = {ox[p], y, oz[p]};
            sp.normal = {0,0,0};
            sp.ground_pos = {ox[p], 0, oz[p]};
            sp.category = support::Category::STABILIZATION;
            sp.id = next_id++;
            result.push_back(sp);
        }
    }
    return result;
}

// --- Deduplication ---

void deduplicate_points(std::vector<support::SupportPoint>& points, float spacing) {
    if (points.empty()) return;
    float inv = 1.0f / spacing;
    std::unordered_map<GridKey, size_t, GridHash> grid;
    std::vector<support::SupportPoint> kept;
    kept.reserve(points.size());
    for (auto& pt : points) {
        GridKey key = grid_cell(pt.position.x, pt.position.z, inv);
        auto it = grid.find(key);
        if (it == grid.end()) {
            grid[key] = kept.size();
            kept.push_back(pt);
        } else {
            if (pt.position.y > kept[it->second].position.y)
                kept[it->second] = pt;
        }
    }
    points = std::move(kept);
}

// --- Surface snapping ---

void snap_contacts_to_surface(
    std::vector<support::SupportPoint>& points,
    const raycaster::MeshRaycaster& rc) {
    for (auto& pt : points) {
        // Cast downward from above the contact to find actual model surface
        Vec3 origin = {pt.position.x, pt.position.y + 1.0f, pt.position.z};
        Vec3 target = {pt.position.x, pt.position.y - 3.0f, pt.position.z};
        auto hit = rc.segment_cast(origin, target);
        if (hit.hit) {
            pt.position = hit.point;
        }
    }
}

// --- Mesh avoidance (now a no-op — tree branching handles this) ---

void adjust_bases_for_mesh_avoidance(
    std::vector<support::SupportPoint>&,
    const float*, uint32_t, const linalg_types::Mat4&) {}

// --- Geometry primitives ---

static uint32_t emit_ring(float cx, float y, float cz, float radius,
    std::vector<float>& v, std::vector<float>& n) {
    uint32_t base = static_cast<uint32_t>(v.size() / 3);
    for (uint32_t i = 0; i < support::PILLAR_SEGMENTS; ++i) {
        float a = TWO_PI * static_cast<float>(i) / static_cast<float>(support::PILLAR_SEGMENTS);
        float dx = std::cos(a), dz = std::sin(a);
        v.push_back(cx+dx*radius); v.push_back(y); v.push_back(cz+dz*radius);
        n.push_back(dx); n.push_back(0); n.push_back(dz);
    }
    return base;
}

static void connect_rings(uint32_t bot, uint32_t top, uint32_t segs,
    std::vector<uint32_t>& idx) {
    for (uint32_t i = 0; i < segs; ++i) {
        uint32_t nx = (i+1)%segs;
        idx.push_back(bot+i); idx.push_back(top+i); idx.push_back(top+nx);
        idx.push_back(bot+i); idx.push_back(top+nx); idx.push_back(bot+nx);
    }
}

static void emit_cap(float cx, float y, float cz, float r, float ny,
    std::vector<float>& v, std::vector<float>& n, std::vector<uint32_t>& idx, bool flip) {
    uint32_t c = static_cast<uint32_t>(v.size()/3);
    v.push_back(cx); v.push_back(y); v.push_back(cz);
    n.push_back(0); n.push_back(ny); n.push_back(0);
    uint32_t rb = static_cast<uint32_t>(v.size()/3);
    for (uint32_t i = 0; i < support::PILLAR_SEGMENTS; ++i) {
        float a = TWO_PI * static_cast<float>(i) / static_cast<float>(support::PILLAR_SEGMENTS);
        v.push_back(cx+std::cos(a)*r); v.push_back(y); v.push_back(cz+std::sin(a)*r);
        n.push_back(0); n.push_back(ny); n.push_back(0);
    }
    for (uint32_t i = 0; i < support::PILLAR_SEGMENTS; ++i) {
        uint32_t nx = (i+1)%support::PILLAR_SEGMENTS;
        if (flip) { idx.push_back(c); idx.push_back(rb+nx); idx.push_back(rb+i); }
        else      { idx.push_back(c); idx.push_back(rb+i); idx.push_back(rb+nx); }
    }
}

/// @brief Generate a tapered cylinder (cone frustum) between two 3D points.
static void emit_tapered_cylinder(const Vec3& from, const Vec3& to,
    float radius_bot, float radius_top,
    std::vector<float>& verts, std::vector<float>& norms, std::vector<uint32_t>& indices) {
    Vec3 dir = to - from;
    float len = linalg::length(dir);
    if (len < 0.01f) return;
    Vec3 axis = dir / len;
    Vec3 up = (std::abs(axis.y) < 0.99f) ? Vec3{0,1,0} : Vec3{1,0,0};
    Vec3 u = linalg::normalize(linalg::cross(up, axis));
    Vec3 w = linalg::cross(axis, u);

    uint32_t base_bot = static_cast<uint32_t>(verts.size()/3);
    for (uint32_t i = 0; i < support::PILLAR_SEGMENTS; ++i) {
        float a = TWO_PI * static_cast<float>(i) / static_cast<float>(support::PILLAR_SEGMENTS);
        float ca = std::cos(a), sa = std::sin(a);
        Vec3 off = u * (ca*radius_bot) + w * (sa*radius_bot);
        Vec3 p = from + off;
        Vec3 n = linalg::normalize(u * ca + w * sa);
        verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
        norms.push_back(n.x); norms.push_back(n.y); norms.push_back(n.z);
    }
    uint32_t base_top = static_cast<uint32_t>(verts.size()/3);
    for (uint32_t i = 0; i < support::PILLAR_SEGMENTS; ++i) {
        float a = TWO_PI * static_cast<float>(i) / static_cast<float>(support::PILLAR_SEGMENTS);
        float ca = std::cos(a), sa = std::sin(a);
        Vec3 off = u * (ca*radius_top) + w * (sa*radius_top);
        Vec3 p = to + off;
        Vec3 n = linalg::normalize(u * ca + w * sa);
        verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
        norms.push_back(n.x); norms.push_back(n.y); norms.push_back(n.z);
    }
    connect_rings(base_bot, base_top, support::PILLAR_SEGMENTS, indices);
}

/// @brief Uniform-radius cylinder shorthand.
static void emit_cylinder(const Vec3& from, const Vec3& to, float radius,
    std::vector<float>& verts, std::vector<float>& norms, std::vector<uint32_t>& indices) {
    emit_tapered_cylinder(from, to, radius, radius, verts, norms, indices);
}

/// @brief Emit a capped tapered cylinder (sealed at both ends).
static void emit_capped_tapered(const Vec3& from, const Vec3& to,
    float r_bot, float r_top,
    std::vector<float>& v, std::vector<float>& n, std::vector<uint32_t>& idx) {
    Vec3 dir = to - from;
    float len = linalg::length(dir);
    if (len < 1e-4f) return;
    Vec3 axis = dir / len;
    Vec3 up = (std::abs(axis.y) < 0.99f) ? Vec3{0,1,0} : Vec3{1,0,0};
    Vec3 u = linalg::normalize(linalg::cross(up, axis));
    Vec3 w = linalg::cross(axis, u);

    // Bottom cap
    uint32_t bc = static_cast<uint32_t>(v.size()/3);
    v.push_back(from.x); v.push_back(from.y); v.push_back(from.z);
    n.push_back(-axis.x); n.push_back(-axis.y); n.push_back(-axis.z);
    uint32_t br = static_cast<uint32_t>(v.size()/3);
    for (uint32_t i = 0; i < support::PILLAR_SEGMENTS; ++i) {
        float a = TWO_PI * static_cast<float>(i) / static_cast<float>(support::PILLAR_SEGMENTS);
        Vec3 off = u * (std::cos(a)*r_bot) + w * (std::sin(a)*r_bot);
        Vec3 p = from + off;
        v.push_back(p.x); v.push_back(p.y); v.push_back(p.z);
        n.push_back(-axis.x); n.push_back(-axis.y); n.push_back(-axis.z);
    }
    for (uint32_t i = 0; i < support::PILLAR_SEGMENTS; ++i) {
        uint32_t nx = (i+1) % support::PILLAR_SEGMENTS;
        idx.push_back(bc); idx.push_back(br+nx); idx.push_back(br+i);
    }

    // Side walls
    emit_tapered_cylinder(from, to, r_bot, r_top, v, n, idx);

    // Top cap
    uint32_t tc = static_cast<uint32_t>(v.size()/3);
    v.push_back(to.x); v.push_back(to.y); v.push_back(to.z);
    n.push_back(axis.x); n.push_back(axis.y); n.push_back(axis.z);
    uint32_t tr = static_cast<uint32_t>(v.size()/3);
    for (uint32_t i = 0; i < support::PILLAR_SEGMENTS; ++i) {
        float a = TWO_PI * static_cast<float>(i) / static_cast<float>(support::PILLAR_SEGMENTS);
        Vec3 off = u * (std::cos(a)*r_top) + w * (std::sin(a)*r_top);
        Vec3 p = to + off;
        v.push_back(p.x); v.push_back(p.y); v.push_back(p.z);
        n.push_back(axis.x); n.push_back(axis.y); n.push_back(axis.z);
    }
    for (uint32_t i = 0; i < support::PILLAR_SEGMENTS; ++i) {
        uint32_t nx = (i+1) % support::PILLAR_SEGMENTS;
        idx.push_back(tc); idx.push_back(tr+i); idx.push_back(tr+nx);
    }
}

// --- Mesh generation ---

void generate_pillar(
    const support::SupportPoint&, const support::SupportParams&,
    std::vector<float>&, std::vector<float>&, std::vector<uint32_t>&) {}

void rebuild_mesh(support::SupportCollection& collection,
    const raycaster::MeshRaycaster* rc) {
    collection.mesh_vertices.clear();
    collection.mesh_normals.clear();
    collection.mesh_indices.clear();

    if (collection.points.empty()) return;

    auto& V = collection.mesh_vertices;
    auto& N = collection.mesh_normals;
    auto& I = collection.mesh_indices;
    const auto& params = collection.params;

    float shaft_r = params.shaft_diameter * 0.5f;
    float base_r = params.base_diameter * 0.5f;
    float tip_r = params.tip_diameter * 0.5f;
    float brace_r = shaft_r * BRACE_DIAMETER_RATIO;

    Vec3 bmin = {1e30f, 1e30f, 1e30f}, bmax = {-1e30f, -1e30f, -1e30f};
    for (const auto& pt : collection.points) {
        bmin = linalg::min(bmin, pt.position);
        bmax = linalg::max(bmax, pt.position);
    }
    float model_cx = (bmin.x + bmax.x) * 0.5f;
    float model_cz = (bmin.z + bmax.z) * 0.5f;

    struct PillarInfo { float base_x, base_z, height; };
    std::vector<PillarInfo> pillars;
    pillars.reserve(collection.points.size());

    for (const auto& pt : collection.points) {
        float h = pt.position.y;
        if (h < support::MIN_PILLAR_HEIGHT) continue;

        // Offset direction from model centroid outward
        float dx = pt.position.x - model_cx;
        float dz = pt.position.z - model_cz;
        float dist = std::sqrt(dx * dx + dz * dz);

        float dir_x = 1.0f, dir_z = 0.0f;
        if (dist > 0.1f) {
            dir_x = dx / dist;
            dir_z = dz / dist;
        }

        // Try increasing offsets until shaft + reach are clear of the model
        float bx = 0, bz = 0, shaft_top_y = 0;
        bool found = false;

        for (uint32_t attempt = 0; attempt < OFFSET_ATTEMPTS; ++attempt) {
            float mult = 1.0f + static_cast<float>(attempt) *
                (MAX_OFFSET_MULTIPLIER - 1.0f) / static_cast<float>(OFFSET_ATTEMPTS - 1);
            float off = SUPPORT_OFFSET_DIST * mult;

            bx = pt.position.x + dir_x * off;
            bz = pt.position.z + dir_z * off;

            float base_top_y = std::min(params.base_height, h * 0.15f);
            shaft_top_y = h - off;
            if (shaft_top_y < base_top_y + 1.0f) shaft_top_y = base_top_y + 1.0f;
            if (shaft_top_y > h - 0.5f) shaft_top_y = h - 0.5f;

            if (!rc) { found = true; break; }

            Vec3 shaft_bot = {bx, base_top_y, bz};
            Vec3 shaft_end = {bx, shaft_top_y, bz};
            Vec3 contact = pt.position;

            bool shaft_clear = !rc->segment_hits(shaft_bot, shaft_end);

            // Shorten reach test to exclude the expected hit at the contact surface
            Vec3 reach_dir = contact - shaft_end;
            float reach_len = linalg::length(reach_dir);
            bool reach_clear = true;
            if (reach_len > params.raycast_margin + 0.1f) {
                Vec3 reach_test_end = shaft_end + (reach_dir / reach_len) * (reach_len - params.raycast_margin);
                reach_clear = !rc->segment_hits(shaft_end, reach_test_end);
            }

            if (shaft_clear && reach_clear) { found = true; break; }
        }

        if (!found) continue;

        float base_top_y = std::min(params.base_height, h * 0.15f);

        // 1. Base flare
        emit_cap(bx, 0, bz, base_r, -1.0f, V, N, I, true);
        uint32_t r0 = emit_ring(bx, 0, bz, base_r, V, N);
        uint32_t r1 = emit_ring(bx, base_top_y, bz, shaft_r, V, N);
        connect_rings(r0, r1, support::PILLAR_SEGMENTS, I);

        // 2. Vertical shaft
        uint32_t r2 = emit_ring(bx, shaft_top_y, bz, shaft_r, V, N);
        connect_rings(r1, r2, support::PILLAR_SEGMENTS, I);

        // 3. Reach + cone tip — all rings XZ-aligned at shaft_r for vertex alignment
        Vec3 shaft_top = {bx, shaft_top_y, bz};
        Vec3 contact = pt.position;

        Vec3 reach = contact - shaft_top;
        float reach_len = linalg::length(reach);

        if (reach_len > 0.1f) {
            Vec3 reach_dir = reach / reach_len;
            float cone_len = std::min(CONTACT_TIP_CONE_LENGTH, reach_len * 0.35f);
            Vec3 cone_start = contact - reach_dir * cone_len;

            // Uniform-thickness reach section (XZ ring at cone_start, same radius)
            uint32_t r3 = emit_ring(cone_start.x, cone_start.y, cone_start.z, shaft_r, V, N);
            connect_rings(r2, r3, support::PILLAR_SEGMENTS, I);

            // Cone tip: taper to tip_end_diameter at contact
            float tip_end_r = params.tip_end_diameter * 0.5f;
            uint32_t r4 = emit_ring(contact.x, contact.y, contact.z, tip_end_r, V, N);
            connect_rings(r3, r4, support::PILLAR_SEGMENTS, I);
            emit_cap(contact.x, contact.y, contact.z, tip_end_r, 1.0f, V, N, I, false);
        } else {
            emit_cap(bx, shaft_top_y, bz, shaft_r, 1.0f, V, N, I, false);
        }

        pillars.push_back({bx, bz, h});
    }

    // --- Cross-bracing between nearby pillars ---
    float max_brace_dist = params.spacing * 3.0f;
    float max_brace_dist_sq = max_brace_dist * max_brace_dist;

    for (size_t ai = 0; ai < pillars.size(); ++ai) {
        const auto& pa = pillars[ai];
        for (size_t bi = ai + 1; bi < pillars.size(); ++bi) {
            const auto& pb = pillars[bi];
            float bdx = pa.base_x - pb.base_x;
            float bdz = pa.base_z - pb.base_z;
            if (bdx*bdx + bdz*bdz > max_brace_dist_sq) continue;

            float min_h = std::min(pa.height, pb.height);
            uint32_t brace_count = static_cast<uint32_t>(min_h / BRACE_VERTICAL_SPACING);

            for (uint32_t b = 1; b <= brace_count; ++b) {
                float y_lo = static_cast<float>(b) * BRACE_VERTICAL_SPACING - BRACE_VERTICAL_SPACING * 0.4f;
                float y_hi = static_cast<float>(b) * BRACE_VERTICAL_SPACING + BRACE_VERTICAL_SPACING * 0.4f;
                if (y_hi > min_h) continue;
                if (b % 2 == 0) {
                    emit_cylinder({pa.base_x, y_lo, pa.base_z}, {pb.base_x, y_hi, pb.base_z}, brace_r, V, N, I);
                } else {
                    emit_cylinder({pa.base_x, y_hi, pa.base_z}, {pb.base_x, y_lo, pb.base_z}, brace_r, V, N, I);
                }
            }
        }
    }

    // --- Raft (solid cylinder, bottom at -RAFT_THICKNESS, top at 0) ---
    Vec3 pmin = {1e30f, 0, 1e30f}, pmax = {-1e30f, 0, -1e30f};
    for (const auto& p : pillars) {
        pmin.x = std::min(pmin.x, p.base_x); pmin.z = std::min(pmin.z, p.base_z);
        pmax.x = std::max(pmax.x, p.base_x); pmax.z = std::max(pmax.z, p.base_z);
    }
    float rcx = (pmin.x + pmax.x) * 0.5f, rcz = (pmin.z + pmax.z) * 0.5f;
    float rrx = (pmax.x - pmin.x) * 0.5f + RAFT_MARGIN;
    float rrz = (pmax.z - pmin.z) * 0.5f + RAFT_MARGIN;
    float rr = std::max(rrx, rrz);

    float raft_bot_y = -RAFT_THICKNESS;
    float raft_top_y = 0.0f;

    // Bottom cap (fan)
    {
        uint32_t c = static_cast<uint32_t>(V.size()/3);
        V.push_back(rcx); V.push_back(raft_bot_y); V.push_back(rcz);
        N.push_back(0); N.push_back(-1); N.push_back(0);
        uint32_t br = static_cast<uint32_t>(V.size()/3);
        for (uint32_t i = 0; i < RAFT_SEGMENTS; ++i) {
            float a = TWO_PI * static_cast<float>(i) / static_cast<float>(RAFT_SEGMENTS);
            V.push_back(rcx + std::cos(a)*rr); V.push_back(raft_bot_y); V.push_back(rcz + std::sin(a)*rr);
            N.push_back(0); N.push_back(-1); N.push_back(0);
        }
        for (uint32_t i = 0; i < RAFT_SEGMENTS; ++i) {
            uint32_t nx = (i+1) % RAFT_SEGMENTS;
            I.push_back(c); I.push_back(br+nx); I.push_back(br+i);
        }
    }
    // Side wall
    uint32_t rb_ring = static_cast<uint32_t>(V.size()/3);
    for (uint32_t i = 0; i < RAFT_SEGMENTS; ++i) {
        float a = TWO_PI * static_cast<float>(i) / static_cast<float>(RAFT_SEGMENTS);
        float dx = std::cos(a), dz = std::sin(a);
        V.push_back(rcx + dx*rr); V.push_back(raft_bot_y); V.push_back(rcz + dz*rr);
        N.push_back(dx); N.push_back(0); N.push_back(dz);
    }
    uint32_t rt_ring = static_cast<uint32_t>(V.size()/3);
    for (uint32_t i = 0; i < RAFT_SEGMENTS; ++i) {
        float a = TWO_PI * static_cast<float>(i) / static_cast<float>(RAFT_SEGMENTS);
        float dx = std::cos(a), dz = std::sin(a);
        V.push_back(rcx + dx*rr); V.push_back(raft_top_y); V.push_back(rcz + dz*rr);
        N.push_back(dx); N.push_back(0); N.push_back(dz);
    }
    for (uint32_t i = 0; i < RAFT_SEGMENTS; ++i) {
        uint32_t nx = (i+1) % RAFT_SEGMENTS;
        I.push_back(rb_ring+i); I.push_back(rt_ring+i); I.push_back(rt_ring+nx);
        I.push_back(rb_ring+i); I.push_back(rt_ring+nx); I.push_back(rb_ring+nx);
    }
    // Top cap (fan)
    {
        uint32_t c = static_cast<uint32_t>(V.size()/3);
        V.push_back(rcx); V.push_back(raft_top_y); V.push_back(rcz);
        N.push_back(0); N.push_back(1); N.push_back(0);
        uint32_t tr = static_cast<uint32_t>(V.size()/3);
        for (uint32_t i = 0; i < RAFT_SEGMENTS; ++i) {
            float a = TWO_PI * static_cast<float>(i) / static_cast<float>(RAFT_SEGMENTS);
            V.push_back(rcx + std::cos(a)*rr); V.push_back(raft_top_y); V.push_back(rcz + std::sin(a)*rr);
            N.push_back(0); N.push_back(1); N.push_back(0);
        }
        for (uint32_t i = 0; i < RAFT_SEGMENTS; ++i) {
            uint32_t nx = (i+1) % RAFT_SEGMENTS;
            I.push_back(c); I.push_back(tr+i); I.push_back(tr+nx);
        }
    }
}

} // namespace support_gen
