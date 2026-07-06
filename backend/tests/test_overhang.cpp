/// @file test_overhang.cpp
/// @brief Tests for overhang detection and auto-orientation.
#include <doctest.h>
#include "overhang.h"

#include <cmath>

using namespace overhang;

static constexpr float EPS = 1e-4f;

// --- Test mesh builders ---

/// @brief Two triangles forming a unit quad on the XZ plane, normal pointing up (+Y).
static void make_upward_quad(
    float* verts, uint32_t* indices,
    uint32_t& vert_count, uint32_t& tri_count) {
    // v0=(0,0,0) v1=(1,0,0) v2=(1,0,1) v3=(0,0,1)
    // CCW winding from above → cross(e1,e2) points up.
    float v[] = {0,0,0, 1,0,0, 1,0,1, 0,0,1};
    uint32_t idx[] = {0,2,1, 0,3,2};
    for (int i = 0; i < 12; ++i) verts[i] = v[i];
    for (int i = 0; i < 6; ++i) indices[i] = idx[i];
    vert_count = 4;
    tri_count = 2;
}

/// @brief Two triangles forming a unit quad on the XZ plane, normal pointing down (-Y).
static void make_downward_quad(
    float* verts, uint32_t* indices,
    uint32_t& vert_count, uint32_t& tri_count) {
    // Same vertices, CW winding from above → cross(e1,e2) points down.
    float v[] = {0,0,0, 1,0,0, 1,0,1, 0,0,1};
    uint32_t idx[] = {0,1,2, 0,2,3};
    for (int i = 0; i < 12; ++i) verts[i] = v[i];
    for (int i = 0; i < 6; ++i) indices[i] = idx[i];
    vert_count = 4;
    tri_count = 2;
}

/// @brief Two triangles forming a vertical wall on the XY plane (normal = +Z).
static void make_vertical_wall(
    float* verts, uint32_t* indices,
    uint32_t& vert_count, uint32_t& tri_count) {
    float v[] = {0,0,0, 1,0,0, 1,1,0, 0,1,0};
    uint32_t idx[] = {0,1,2, 0,2,3};
    for (int i = 0; i < 12; ++i) verts[i] = v[i];
    for (int i = 0; i < 6; ++i) indices[i] = idx[i];
    vert_count = 4;
    tri_count = 2;
}

// --- analyze ---

TEST_CASE("analyze upward quad has zero overhangs") {
    float verts[12]; uint32_t indices[6];
    uint32_t vc, tc;
    make_upward_quad(verts, indices, vc, tc);

    Mat4 id = linalg::identity;
    auto result = analyze(verts, indices, vc, tc, id, 45.0f);

    CHECK(result.triangle_indices.empty());
    CHECK(result.overhang_area < EPS);
    CHECK(result.total_area > 0.0f);
}

TEST_CASE("analyze downward quad is entirely overhang") {
    float verts[12]; uint32_t indices[6];
    uint32_t vc, tc;
    make_downward_quad(verts, indices, vc, tc);

    Mat4 id = linalg::identity;
    auto result = analyze(verts, indices, vc, tc, id, 45.0f);

    CHECK(result.triangle_indices.size() == 2);
    CHECK(std::abs(result.overhang_area - result.total_area) < EPS);
}

TEST_CASE("analyze vertical wall is not overhang at 45 threshold") {
    float verts[12]; uint32_t indices[6];
    uint32_t vc, tc;
    make_vertical_wall(verts, indices, vc, tc);

    Mat4 id = linalg::identity;
    auto result = analyze(verts, indices, vc, tc, id, 45.0f);

    CHECK(result.triangle_indices.empty());
    CHECK(result.overhang_area < EPS);
}

TEST_CASE("analyze threshold 0 flags all downward faces") {
    float verts[12]; uint32_t indices[6];
    uint32_t vc, tc;
    make_downward_quad(verts, indices, vc, tc);

    Mat4 id = linalg::identity;
    auto result = analyze(verts, indices, vc, tc, id, 0.0f);

    CHECK(result.triangle_indices.size() == 2);
}

TEST_CASE("analyze threshold 0 does not flag vertical wall") {
    float verts[12]; uint32_t indices[6];
    uint32_t vc, tc;
    make_vertical_wall(verts, indices, vc, tc);

    Mat4 id = linalg::identity;
    auto result = analyze(verts, indices, vc, tc, id, 0.0f);

    CHECK(result.triangle_indices.empty());
}

TEST_CASE("analyze threshold 90 flags nothing") {
    float verts[12]; uint32_t indices[6];
    uint32_t vc, tc;
    make_downward_quad(verts, indices, vc, tc);

    Mat4 id = linalg::identity;
    auto result = analyze(verts, indices, vc, tc, id, 90.0f);

    CHECK(result.triangle_indices.empty());
}

TEST_CASE("analyze respects transform rotation") {
    float verts[12]; uint32_t indices[6];
    uint32_t vc, tc;
    make_upward_quad(verts, indices, vc, tc);

    // Rotate 180 degrees around X — flips the quad upside down
    float rad = 3.14159265f;
    Vec4 q = linalg::rotation_quat(Vec3{1, 0, 0}, rad);
    Mat4 rot = linalg::rotation_matrix(q);

    auto result = analyze(verts, indices, vc, tc, rot, 45.0f);

    CHECK(result.triangle_indices.size() == 2);
    CHECK(std::abs(result.overhang_area - result.total_area) < EPS);
}

TEST_CASE("analyze area is correct for unit quad") {
    float verts[12]; uint32_t indices[6];
    uint32_t vc, tc;
    make_upward_quad(verts, indices, vc, tc);

    Mat4 id = linalg::identity;
    auto result = analyze(verts, indices, vc, tc, id, 45.0f);

    // Unit quad = 1.0 area
    CHECK(std::abs(result.total_area - 1.0f) < EPS);
}

// --- find_best_orientation ---

TEST_CASE("find_best_orientation for downward quad finds flip") {
    float verts[12]; uint32_t indices[6];
    uint32_t vc, tc;
    make_downward_quad(verts, indices, vc, tc);

    auto result = find_best_orientation(
        verts, indices, vc, tc, 45.0f, PreferredAxis::TILT_BACK);

    // The optimizer should find a rotation that makes overhang area near zero
    CHECK(result.overhang_area < 0.01f);
    CHECK(result.samples_evaluated > 0);
    CHECK(!result.cancelled);
}

TEST_CASE("find_best_orientation for upward quad keeps identity") {
    float verts[12]; uint32_t indices[6];
    uint32_t vc, tc;
    make_upward_quad(verts, indices, vc, tc);

    auto result = find_best_orientation(
        verts, indices, vc, tc, 45.0f, PreferredAxis::ANY);

    CHECK(result.overhang_area < EPS);
}

TEST_CASE("find_best_orientation respects cancellation") {
    float verts[12]; uint32_t indices[6];
    uint32_t vc, tc;
    make_downward_quad(verts, indices, vc, tc);

    std::atomic<bool> cancel{true};
    auto result = find_best_orientation(
        verts, indices, vc, tc, 45.0f, PreferredAxis::ANY, nullptr, &cancel);

    CHECK(result.cancelled);
}

// --- parse_preferred_axis ---

TEST_CASE("parse_preferred_axis valid values") {
    PreferredAxis a;
    CHECK(parse_preferred_axis("tilt_back", a));
    CHECK(a == PreferredAxis::TILT_BACK);

    CHECK(parse_preferred_axis("tilt_forward", a));
    CHECK(a == PreferredAxis::TILT_FORWARD);

    CHECK(parse_preferred_axis("tilt_left", a));
    CHECK(a == PreferredAxis::TILT_LEFT);

    CHECK(parse_preferred_axis("tilt_right", a));
    CHECK(a == PreferredAxis::TILT_RIGHT);

    CHECK(parse_preferred_axis("any", a));
    CHECK(a == PreferredAxis::ANY);
}

TEST_CASE("parse_preferred_axis invalid value returns false") {
    PreferredAxis a;
    CHECK_FALSE(parse_preferred_axis("invalid", a));
    CHECK_FALSE(parse_preferred_axis("", a));
}
