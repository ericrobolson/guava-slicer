/// @file test_slicer.cpp
/// @brief Tests for mesh slicing — plane intersection and contour assembly.
#include <doctest.h>
#include "slicer.h"

#include <cmath>

using namespace slicer;

static constexpr float EPS = 1e-3f;

/// @brief Identity transform matrix.
static linalg_types::Mat4 identity() {
    return {
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 0, 1},
    };
}

/// @brief Build a unit cube (8 vertices, 12 triangles) spanning [0,0,0] to [1,1,1].
static void make_unit_cube(
    float* verts, uint32_t* indices,
    uint32_t& vert_count, uint32_t& tri_count)
{
    float v[] = {
        0,0,0, 1,0,0, 1,1,0, 0,1,0,
        0,0,1, 1,0,1, 1,1,1, 0,1,1,
    };
    uint32_t idx[] = {
        0,1,2, 0,2,3,
        4,6,5, 4,7,6,
        0,4,5, 0,5,1,
        2,6,7, 2,7,3,
        0,3,7, 0,7,4,
        1,5,6, 1,6,2,
    };
    for (int i = 0; i < 24; ++i) verts[i] = v[i];
    for (int i = 0; i < 36; ++i) indices[i] = idx[i];
    vert_count = 8;
    tri_count = 12;
}

/// @brief Build a tall box [0,0,0] to [1,10,1] for multi-layer tests (tall in Y).
static void make_tall_box(
    float* verts, uint32_t* indices,
    uint32_t& vert_count, uint32_t& tri_count)
{
    float v[] = {
        0,0,0,   1,0,0,   1,10,0,  0,10,0,
        0,0,1,   1,0,1,   1,10,1,  0,10,1,
    };
    uint32_t idx[] = {
        0,1,2, 0,2,3,
        4,6,5, 4,7,6,
        0,4,5, 0,5,1,
        2,6,7, 2,7,3,
        0,3,7, 0,7,4,
        1,5,6, 1,6,2,
    };
    for (int i = 0; i < 24; ++i) verts[i] = v[i];
    for (int i = 0; i < 36; ++i) indices[i] = idx[i];
    vert_count = 8;
    tri_count = 12;
}

TEST_CASE("slice_mesh: empty mesh returns zero layers") {
    auto result = slice_mesh(nullptr, nullptr, 0, 0, identity(), DEFAULT_LAYER_HEIGHT);
    CHECK(result.layer_count == 0);
    CHECK(result.layers.empty());
}

TEST_CASE("slice_mesh: unit cube produces layers") {
    float verts[24];
    uint32_t indices[36];
    uint32_t vc, tc;
    make_unit_cube(verts, indices, vc, tc);

    auto result = slice_mesh(verts, indices, vc, tc, identity(), 0.2f);

    CHECK(result.layer_count > 0);
    CHECK(result.layer_height == doctest::Approx(0.2f));

    for (uint32_t i = 0; i < result.layer_count; ++i) {
        const auto& layer = result.layers[i];
        CHECK(layer.z_height > 0.0f);  // z_height stores Y value (vertical axis)
        CHECK(layer.z_height < 1.0f);
        CHECK_FALSE(layer.contours.empty());
    }
}

TEST_CASE("slice_mesh: unit cube contours are closed rectangles") {
    float verts[24];
    uint32_t indices[36];
    uint32_t vc, tc;
    make_unit_cube(verts, indices, vc, tc);

    auto result = slice_mesh(verts, indices, vc, tc, identity(), 0.5f);

    REQUIRE(result.layer_count >= 1);
    const auto& layer = result.layers[0];
    REQUIRE(layer.contours.size() >= 1);

    const auto& contour = layer.contours[0];
    CHECK(contour.points.size() >= 4);
    CHECK_FALSE(contour.is_hole);
}

TEST_CASE("slice_mesh: layer count matches expected for tall box") {
    float verts[24];
    uint32_t indices[36];
    uint32_t vc, tc;
    make_tall_box(verts, indices, vc, tc);

    constexpr float HEIGHT = 0.5f;
    auto result = slice_mesh(verts, indices, vc, tc, identity(), HEIGHT);

    uint32_t expected = static_cast<uint32_t>((10.0f - HEIGHT * 0.5f) / HEIGHT) + 1;
    CHECK(result.layer_count == expected);
}

TEST_CASE("slice_mesh: z_heights are evenly spaced") {
    float verts[24];
    uint32_t indices[36];
    uint32_t vc, tc;
    make_tall_box(verts, indices, vc, tc);

    auto result = slice_mesh(verts, indices, vc, tc, identity(), 1.0f);

    REQUIRE(result.layer_count >= 2);
    for (uint32_t i = 1; i < result.layer_count; ++i) {
        float delta = result.layers[i].z_height - result.layers[i - 1].z_height;
        CHECK(delta == doctest::Approx(1.0f).epsilon(EPS));
    }
}

TEST_CASE("slice_mesh: first layer offset is half layer height") {
    float verts[24];
    uint32_t indices[36];
    uint32_t vc, tc;
    make_unit_cube(verts, indices, vc, tc);

    constexpr float HEIGHT = 0.1f;
    auto result = slice_mesh(verts, indices, vc, tc, identity(), HEIGHT);

    REQUIRE(result.layer_count >= 1);
    CHECK(result.layers[0].z_height == doctest::Approx(HEIGHT * 0.5f).epsilon(EPS));
}

TEST_CASE("slice_mesh: cancellation stops early") {
    float verts[24];
    uint32_t indices[36];
    uint32_t vc, tc;
    make_tall_box(verts, indices, vc, tc);

    std::atomic<bool> cancel{true};
    auto result = slice_mesh(verts, indices, vc, tc, identity(), 0.01f, nullptr, &cancel);

    bool all_empty = true;
    for (uint32_t i = 1; i < result.layer_count; ++i) {
        if (!result.layers[i].contours.empty()) {
            all_empty = false;
            break;
        }
    }
    CHECK(all_empty);
}

TEST_CASE("slice_mesh: progress callback fires") {
    float verts[24];
    uint32_t indices[36];
    uint32_t vc, tc;
    make_tall_box(verts, indices, vc, tc);

    uint32_t last_current = 0;
    uint32_t last_total = 0;
    int call_count = 0;

    auto result = slice_mesh(verts, indices, vc, tc, identity(), 1.0f,
        [&](uint32_t current, uint32_t total) {
            last_current = current;
            last_total = total;
            call_count++;
        });

    CHECK(call_count > 0);
    CHECK(last_total == result.layer_count);
    CHECK(last_current == result.layer_count);
}
