/// @file test_island_detection.cpp
/// @brief Tests for island detection — unsupported region identification across layers.
#include <doctest.h>
#include "island_detection.h"

using namespace island_detection;
using namespace slicer;

/// @brief Build a square contour centered at (cx, cy) with given half-size.
static Contour make_square(float cx, float cy, float half, bool is_hole = false) {
    Contour c;
    c.is_hole = is_hole;
    c.points = {
        {cx - half, cy - half},
        {cx + half, cy - half},
        {cx + half, cy + half},
        {cx - half, cy + half},
    };
    return c;
}

/// @brief Build a SliceResult with uniform layers, each containing the given contours.
static SliceResult make_uniform_slice(
    uint32_t layer_count, float layer_height,
    const std::vector<Contour>& contours_per_layer)
{
    SliceResult sr;
    sr.layer_height = layer_height;
    sr.layer_count = layer_count;
    sr.warning_count = 0;
    for (uint32_t i = 0; i < layer_count; ++i) {
        SliceLayer layer;
        layer.z_height = layer_height * (static_cast<float>(i) + 0.5f);
        layer.contours = contours_per_layer;
        sr.layers.push_back(std::move(layer));
    }
    return sr;
}

TEST_CASE("detect: empty slice returns no islands") {
    SliceResult sr;
    sr.layer_height = 0.06f;
    sr.layer_count = 0;
    sr.warning_count = 0;

    auto result = detect(sr);
    CHECK(result.total_island_count == 0);
    CHECK(result.layers.empty());
    CHECK(result.severity_scores.empty());
}

TEST_CASE("detect: single layer returns no islands") {
    SliceResult sr;
    sr.layer_height = 0.06f;
    sr.layer_count = 1;
    sr.warning_count = 0;
    SliceLayer layer;
    layer.z_height = 0.03f;
    layer.contours.push_back(make_square(0, 0, 5));
    sr.layers.push_back(std::move(layer));

    auto result = detect(sr);
    CHECK(result.total_island_count == 0);
    CHECK(result.layers.empty());
    CHECK(result.severity_scores.size() == 1);
    CHECK(result.severity_scores[0] == 0.0f);
}

TEST_CASE("detect: identical layers produce no islands") {
    auto contour = make_square(0, 0, 5);
    auto sr = make_uniform_slice(10, 0.06f, {contour});

    auto result = detect(sr);
    CHECK(result.total_island_count == 0);
    CHECK(result.layers.empty());
}

TEST_CASE("detect: contour with no overlap is flagged as island") {
    SliceResult sr;
    sr.layer_height = 0.06f;
    sr.layer_count = 2;
    sr.warning_count = 0;

    SliceLayer layer0;
    layer0.z_height = 0.03f;
    layer0.contours.push_back(make_square(-10, 0, 2));
    sr.layers.push_back(std::move(layer0));

    SliceLayer layer1;
    layer1.z_height = 0.09f;
    layer1.contours.push_back(make_square(10, 0, 2));
    sr.layers.push_back(std::move(layer1));

    auto result = detect(sr);
    CHECK(result.total_island_count == 1);
    REQUIRE(result.layers.size() == 1);
    CHECK(result.layers[0].layer_index == 1);
    CHECK(result.layers[0].islands.size() == 1);
    CHECK(result.layers[0].islands[0].contour_index == 0);
    CHECK(result.layers[0].islands[0].area > 0.0);
}

TEST_CASE("detect: partial overlap is not an island") {
    SliceResult sr;
    sr.layer_height = 0.06f;
    sr.layer_count = 2;
    sr.warning_count = 0;

    SliceLayer layer0;
    layer0.z_height = 0.03f;
    layer0.contours.push_back(make_square(0, 0, 5));
    sr.layers.push_back(std::move(layer0));

    SliceLayer layer1;
    layer1.z_height = 0.09f;
    layer1.contours.push_back(make_square(3, 0, 5));
    sr.layers.push_back(std::move(layer1));

    auto result = detect(sr);
    CHECK(result.total_island_count == 0);
}

TEST_CASE("detect: mixed supported and unsupported contours") {
    SliceResult sr;
    sr.layer_height = 0.06f;
    sr.layer_count = 2;
    sr.warning_count = 0;

    SliceLayer layer0;
    layer0.z_height = 0.03f;
    layer0.contours.push_back(make_square(0, 0, 5));
    sr.layers.push_back(std::move(layer0));

    SliceLayer layer1;
    layer1.z_height = 0.09f;
    layer1.contours.push_back(make_square(0, 0, 3));
    layer1.contours.push_back(make_square(50, 50, 2));
    sr.layers.push_back(std::move(layer1));

    auto result = detect(sr);
    CHECK(result.total_island_count == 1);
    REQUIRE(result.layers.size() == 1);
    CHECK(result.layers[0].islands[0].contour_index == 1);
}

TEST_CASE("detect: severity scores are populated") {
    SliceResult sr;
    sr.layer_height = 0.06f;
    sr.layer_count = 3;
    sr.warning_count = 0;

    SliceLayer layer0;
    layer0.z_height = 0.03f;
    layer0.contours.push_back(make_square(0, 0, 5));
    sr.layers.push_back(std::move(layer0));

    SliceLayer layer1;
    layer1.z_height = 0.09f;
    layer1.contours.push_back(make_square(0, 0, 5));
    layer1.contours.push_back(make_square(50, 0, 1));
    sr.layers.push_back(std::move(layer1));

    SliceLayer layer2;
    layer2.z_height = 0.15f;
    layer2.contours.push_back(make_square(0, 0, 5));
    layer2.contours.push_back(make_square(50, 0, 3));
    layer2.contours.push_back(make_square(-50, 0, 2));
    sr.layers.push_back(std::move(layer2));

    auto result = detect(sr);
    REQUIRE(result.severity_scores.size() == 3);
    CHECK(result.severity_scores[0] == 0.0f);
    CHECK(result.severity_scores[1] > 0.0f);
    CHECK(result.severity_scores[2] > result.severity_scores[1]);
    CHECK(result.worst_layer_index == 2);
}

TEST_CASE("detect: cancellation stops early") {
    auto contour = make_square(0, 0, 5);
    auto sr = make_uniform_slice(100, 0.06f, {contour});

    std::atomic<bool> cancel{true};
    auto result = detect(sr, nullptr, &cancel);
    CHECK(result.severity_scores.empty());
}

TEST_CASE("detect: progress callback fires") {
    auto contour = make_square(0, 0, 5);
    auto sr = make_uniform_slice(200, 0.06f, {contour});

    uint32_t callback_count = 0;
    auto result = detect(sr, [&](uint32_t, uint32_t) { callback_count++; });
    CHECK(callback_count > 0);
}

TEST_CASE("detect: holes in layer below do not provide support") {
    SliceResult sr;
    sr.layer_height = 0.06f;
    sr.layer_count = 2;
    sr.warning_count = 0;

    SliceLayer layer0;
    layer0.z_height = 0.03f;
    layer0.contours.push_back(make_square(0, 0, 10));
    layer0.contours.push_back(make_square(0, 0, 3, true));
    sr.layers.push_back(std::move(layer0));

    SliceLayer layer1;
    layer1.z_height = 0.09f;
    layer1.contours.push_back(make_square(0, 0, 2));
    sr.layers.push_back(std::move(layer1));

    auto result = detect(sr);
    CHECK(result.total_island_count == 1);
    REQUIRE(result.layers.size() == 1);
    CHECK(result.layers[0].islands[0].contour_index == 0);
}
