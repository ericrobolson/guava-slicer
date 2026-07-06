/// @file test_mesh_ops.cpp
/// @brief Tests for pure mesh geometry operations.
#include <doctest.h>
#include "mesh_ops.h"

#include <cmath>
#include <vector>

using namespace mesh_ops;

static constexpr float EPS = 1e-4f;

// --- compute_centroid ---

TEST_CASE("compute_centroid single vertex") {
    float verts[] = {3.0f, 4.0f, 5.0f};
    Vec3 c = compute_centroid(verts, 1);
    CHECK(std::abs(c.x - 3.0f) < EPS);
    CHECK(std::abs(c.y - 4.0f) < EPS);
    CHECK(std::abs(c.z - 5.0f) < EPS);
}

TEST_CASE("compute_centroid two vertices") {
    float verts[] = {0, 0, 0, 10, 20, 30};
    Vec3 c = compute_centroid(verts, 2);
    CHECK(std::abs(c.x - 5.0f) < EPS);
    CHECK(std::abs(c.y - 10.0f) < EPS);
    CHECK(std::abs(c.z - 15.0f) < EPS);
}

TEST_CASE("compute_centroid symmetric cube vertices") {
    float verts[] = {
        -1,-1,-1, 1,-1,-1, -1,1,-1, 1,1,-1,
        -1,-1, 1, 1,-1, 1, -1,1, 1, 1,1, 1,
    };
    Vec3 c = compute_centroid(verts, 8);
    CHECK(std::abs(c.x) < EPS);
    CHECK(std::abs(c.y) < EPS);
    CHECK(std::abs(c.z) < EPS);
}

// --- center_at_centroid ---

TEST_CASE("center_at_centroid shifts vertices to origin") {
    float verts[] = {10, 20, 30, 12, 22, 32};
    mesh::BoundingBox bbox;
    bbox.min = {10, 20, 30};
    bbox.max = {12, 22, 32};

    center_at_centroid(verts, 2, bbox);

    Vec3 c = compute_centroid(verts, 2);
    CHECK(std::abs(c.x) < EPS);
    CHECK(std::abs(c.y) < EPS);
    CHECK(std::abs(c.z) < EPS);

    CHECK(std::abs(bbox.min.x - (-1.0f)) < EPS);
    CHECK(std::abs(bbox.max.x - 1.0f) < EPS);
}

TEST_CASE("center_at_centroid preserves relative positions") {
    float verts[] = {0, 0, 0, 4, 0, 0, 0, 6, 0};
    mesh::BoundingBox bbox;
    bbox.min = {0, 0, 0};
    bbox.max = {4, 6, 0};

    center_at_centroid(verts, 3, bbox);

    float dx = verts[3] - verts[0];
    float dy = verts[7] - verts[1];
    CHECK(std::abs(dx - 4.0f) < EPS);
    CHECK(std::abs(dy - 6.0f) < EPS);
}

// --- min_transformed_y ---

TEST_CASE("min_transformed_y identity matrix") {
    float verts[] = {0, 5, 0, 0, -3, 0, 0, 10, 0};
    Mat4 id = linalg::identity;
    float min_y = min_transformed_y(verts, 3, id);
    CHECK(std::abs(min_y - (-3.0f)) < EPS);
}

TEST_CASE("min_transformed_y with translation") {
    float verts[] = {0, 0, 0, 0, -5, 0};
    Mat4 t = linalg::translation_matrix(Vec3{0, 10, 0});
    float min_y = min_transformed_y(verts, 2, t);
    CHECK(std::abs(min_y - 5.0f) < EPS);
}

TEST_CASE("min_transformed_y with 90 degree X rotation") {
    // Rotating (0, 0, -10) by 90° around X puts it at (0, 10, 0)
    // Rotating (0, 0, 10) by 90° around X puts it at (0, -10, 0)
    float verts[] = {0, 0, -10, 0, 0, 10};
    float rad = 3.14159265f / 2.0f;
    Vec4 q = linalg::rotation_quat(Vec3{1, 0, 0}, rad);
    Mat4 r = linalg::rotation_matrix(q);
    float min_y = min_transformed_y(verts, 2, r);
    CHECK(std::abs(min_y - (-10.0f)) < EPS);
}

// --- rotation_around_point ---

TEST_CASE("rotation_around_point at origin equals plain rotation") {
    float rad = 3.14159265f / 2.0f;
    Vec4 q = linalg::rotation_quat(Vec3{0, 1, 0}, rad);
    Mat4 rot = linalg::rotation_matrix(q);

    Mat4 around = rotation_around_point(rot, {0, 0, 0});

    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK(std::abs(around[c][r] - rot[c][r]) < EPS);
        }
    }
}

TEST_CASE("rotation_around_point preserves the center point") {
    Vec3 center = {5, 10, 15};
    float rad = 3.14159265f / 4.0f;
    Vec4 q = linalg::rotation_quat(Vec3{0, 0, 1}, rad);
    Mat4 rot = linalg::rotation_matrix(q);

    Mat4 around = rotation_around_point(rot, center);

    Vec4 result = linalg::mul(around, Vec4{center.x, center.y, center.z, 1.0f});
    CHECK(std::abs(result.x - center.x) < EPS);
    CHECK(std::abs(result.y - center.y) < EPS);
    CHECK(std::abs(result.z - center.z) < EPS);
}

TEST_CASE("rotation_around_point rotates points relative to center") {
    Vec3 center = {10, 0, 0};
    float rad = 3.14159265f / 2.0f;
    Vec4 q = linalg::rotation_quat(Vec3{0, 1, 0}, rad);
    Mat4 rot = linalg::rotation_matrix(q);

    Mat4 around = rotation_around_point(rot, center);

    // Point at (15, 0, 0) is 5 units from center along X.
    // After 90° Y rotation around center, it should be at (10, 0, -5).
    Vec4 result = linalg::mul(around, Vec4{15, 0, 0, 1});
    CHECK(std::abs(result.x - 10.0f) < EPS);
    CHECK(std::abs(result.y - 0.0f) < EPS);
    CHECK(std::abs(result.z - (-5.0f)) < EPS);
}

// --- center_on_plate_delta ---

TEST_CASE("center_on_plate_delta already centered returns near-zero") {
    float verts[] = {-1, 5, -1, 1, 5, 1};
    Mat4 id = linalg::identity;
    Vec3 d = center_on_plate_delta(verts, 2, id);
    CHECK(std::abs(d.x) < EPS);
    CHECK(std::abs(d.y) < EPS);
    CHECK(std::abs(d.z) < EPS);
}

TEST_CASE("center_on_plate_delta off-center mesh") {
    float verts[] = {10, 0, 20, 12, 0, 22};
    Mat4 id = linalg::identity;
    Vec3 d = center_on_plate_delta(verts, 2, id);
    CHECK(std::abs(d.x - (-11.0f)) < EPS);
    CHECK(std::abs(d.y) < EPS);
    CHECK(std::abs(d.z - (-21.0f)) < EPS);
}

TEST_CASE("center_on_plate_delta with transform") {
    float verts[] = {0, 0, 0, 2, 0, 0};
    Mat4 t = linalg::translation_matrix(Vec3{10, 5, 20});
    Vec3 d = center_on_plate_delta(verts, 2, t);
    // Transformed centroid X = (10+12)/2 = 11, Z = (20+20)/2 = 20
    CHECK(std::abs(d.x - (-11.0f)) < EPS);
    CHECK(std::abs(d.y) < EPS);
    CHECK(std::abs(d.z - (-20.0f)) < EPS);
}

// --- scale_around_point ---

TEST_CASE("scale_around_point at origin equals plain scale") {
    auto m = scale_around_point({2, 2, 2}, {0, 0, 0});
    Vec4 result = linalg::mul(m, Vec4{3, 4, 5, 1});
    CHECK(std::abs(result.x - 6.0f) < EPS);
    CHECK(std::abs(result.y - 8.0f) < EPS);
    CHECK(std::abs(result.z - 10.0f) < EPS);
}

TEST_CASE("scale_around_point preserves the center point") {
    Vec3 center = {10, 20, 30};
    auto m = scale_around_point({3, 3, 3}, center);
    Vec4 result = linalg::mul(m, Vec4{center.x, center.y, center.z, 1});
    CHECK(std::abs(result.x - center.x) < EPS);
    CHECK(std::abs(result.y - center.y) < EPS);
    CHECK(std::abs(result.z - center.z) < EPS);
}

TEST_CASE("scale_around_point scales relative to center") {
    Vec3 center = {10, 0, 0};
    auto m = scale_around_point({2, 2, 2}, center);
    // Point at (15,0,0) is 5 units from center. After 2x scale: 10 units from center -> (20,0,0)
    Vec4 result = linalg::mul(m, Vec4{15, 0, 0, 1});
    CHECK(std::abs(result.x - 20.0f) < EPS);
    CHECK(std::abs(result.y - 0.0f) < EPS);
}

TEST_CASE("scale_around_point non-uniform") {
    Vec3 center = {0, 5, 0};
    auto m = scale_around_point({1, 2, 1}, center);
    // Point at (0,10,0) is 5 above center. After 2x Y scale: 10 above center -> (0,15,0)
    Vec4 result = linalg::mul(m, Vec4{0, 10, 0, 1});
    CHECK(std::abs(result.x - 0.0f) < EPS);
    CHECK(std::abs(result.y - 15.0f) < EPS);
}

// --- translation_of ---

TEST_CASE("translation_of identity is zero") {
    Mat4 id = linalg::identity;
    Vec3 t = translation_of(id);
    CHECK(std::abs(t.x) < EPS);
    CHECK(std::abs(t.y) < EPS);
    CHECK(std::abs(t.z) < EPS);
}

TEST_CASE("translation_of translation matrix") {
    Mat4 m = linalg::translation_matrix(Vec3{7, 8, 9});
    Vec3 t = translation_of(m);
    CHECK(std::abs(t.x - 7.0f) < EPS);
    CHECK(std::abs(t.y - 8.0f) < EPS);
    CHECK(std::abs(t.z - 9.0f) < EPS);
}

TEST_CASE("translation_of composed rotation+translation") {
    float rad = 3.14159265f / 4.0f;
    Vec4 q = linalg::rotation_quat(Vec3{0, 1, 0}, rad);
    Mat4 rot = linalg::rotation_matrix(q);
    Mat4 trans = linalg::translation_matrix(Vec3{3, 4, 5});
    Mat4 composed = linalg::mul(trans, rot);

    Vec3 t = translation_of(composed);
    CHECK(std::abs(t.x - 3.0f) < EPS);
    CHECK(std::abs(t.y - 4.0f) < EPS);
    CHECK(std::abs(t.z - 5.0f) < EPS);
}
