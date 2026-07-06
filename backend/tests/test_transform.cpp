/// @file test_transform.cpp
/// @brief Tests for TransformState, label generators, and ResetCommand.
#include <doctest.h>
#include "transform.h"

#include <cmath>

using namespace transform;
using TS = TransformState;

static constexpr float EPSILON = 1e-4f;

static void check_vec3_approx(const Vec3& actual, const Vec3& expected, float eps = EPSILON) {
    CHECK(std::abs(actual.x - expected.x) < eps);
    CHECK(std::abs(actual.y - expected.y) < eps);
    CHECK(std::abs(actual.z - expected.z) < eps);
}

static void check_mat4_identity(const Mat4& m, float eps = EPSILON) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float expected = (row == col) ? 1.0f : 0.0f;
            CHECK(std::abs(m[col][row] - expected) < eps);
        }
    }
}

// --- TransformState tests ---

TEST_CASE("TransformState starts with identity matrix") {
    TransformState state;
    check_mat4_identity(state.composite_matrix());
    CHECK(state.count() == 0);
}

TEST_CASE("TransformState translate") {
    TransformState state;
    state.push(TS::make_translate({10.0f, 20.0f, 30.0f}));

    Mat4 m = state.composite_matrix();
    CHECK(std::abs(m.w.x - 10.0f) < EPSILON);
    CHECK(std::abs(m.w.y - 20.0f) < EPSILON);
    CHECK(std::abs(m.w.z - 30.0f) < EPSILON);
    CHECK(state.count() == 1);
}

TEST_CASE("TransformState rotate 90 around Z") {
    TransformState state;
    state.push(TS::make_rotate({0, 0, 1}, 90.0f));

    Mat4 m = state.composite_matrix();
    Vec4 v = linalg::mul(m, Vec4{1, 0, 0, 1});
    CHECK(std::abs(v.x - 0.0f) < EPSILON);
    CHECK(std::abs(v.y - 1.0f) < EPSILON);
    CHECK(std::abs(v.z - 0.0f) < EPSILON);
}

TEST_CASE("TransformState scale uniform") {
    TransformState state;
    state.push(TS::make_scale({2.0f, 2.0f, 2.0f}));

    Mat4 m = state.composite_matrix();
    Vec4 v = linalg::mul(m, Vec4{1, 1, 1, 1});
    CHECK(std::abs(v.x - 2.0f) < EPSILON);
    CHECK(std::abs(v.y - 2.0f) < EPSILON);
    CHECK(std::abs(v.z - 2.0f) < EPSILON);
}

TEST_CASE("TransformState scale non-uniform") {
    TransformState state;
    state.push(TS::make_scale({1.0f, 2.0f, 3.0f}));

    Mat4 m = state.composite_matrix();
    Vec4 v = linalg::mul(m, Vec4{1, 1, 1, 1});
    CHECK(std::abs(v.x - 1.0f) < EPSILON);
    CHECK(std::abs(v.y - 2.0f) < EPSILON);
    CHECK(std::abs(v.z - 3.0f) < EPSILON);
}

TEST_CASE("TransformState pop removes last transform") {
    TransformState state;
    state.push(TS::make_translate({5, 0, 0}));
    state.push(TS::make_translate({0, 10, 0}));
    CHECK(state.count() == 2);

    state.pop();
    CHECK(state.count() == 1);

    Mat4 m = state.composite_matrix();
    CHECK(std::abs(m.w.x - 5.0f) < EPSILON);
    CHECK(std::abs(m.w.y - 0.0f) < EPSILON);
}

TEST_CASE("TransformState clear resets to identity") {
    TransformState state;
    state.push(TS::make_translate({5, 5, 5}));
    state.push(TS::make_rotate({1, 0, 0}, 45.0f));

    state.clear();
    check_mat4_identity(state.composite_matrix());
    CHECK(state.count() == 0);
}

TEST_CASE("TransformState snapshot and restore") {
    TransformState state;
    state.push(TS::make_translate({1, 2, 3}));
    state.push(TS::make_rotate({0, 1, 0}, 45.0f));

    auto snap = state.snapshot();
    Mat4 before = state.composite_matrix();

    state.clear();
    check_mat4_identity(state.composite_matrix());

    state.restore(snap);
    Mat4 after = state.composite_matrix();

    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK(std::abs(before[c][r] - after[c][r]) < EPSILON);
        }
    }
}

TEST_CASE("TransformState transformed_bounding_box with translation") {
    TransformState state;
    state.push(TS::make_translate({10, 0, 0}));

    mesh::BoundingBox original;
    original.min = {0, 0, 0};
    original.max = {1, 1, 1};

    auto bbox = state.transformed_bounding_box(original);
    check_vec3_approx(bbox.min, {10, 0, 0});
    check_vec3_approx(bbox.max, {11, 1, 1});
}

TEST_CASE("TransformState dimensions with scale") {
    TransformState state;
    state.push(TS::make_scale({2, 3, 4}));

    mesh::BoundingBox original;
    original.min = {0, 0, 0};
    original.max = {1, 1, 1};

    Vec3 dims = state.dimensions(original);
    check_vec3_approx(dims, {2, 3, 4});
}

// --- Label generator tests ---

TEST_CASE("rotate_label formatting") {
    CHECK(rotate_label({1, 0, 0}, 90.0f) == "Rotate X 90");
    CHECK(rotate_label({0, 1, 0}, 45.0f) == "Rotate Y 45");
    CHECK(rotate_label({0, 0, 1}, -90.0f) == "Rotate Z -90");
}

TEST_CASE("translate_label formatting") {
    CHECK(translate_label({1.0f, 2.5f, 0.0f}) == "Translate (1.0, 2.5, 0.0)");
}

TEST_CASE("scale_label uniform") {
    CHECK(scale_label({2.0f, 2.0f, 2.0f}) == "Scale 2.00");
}

TEST_CASE("scale_label non-uniform") {
    CHECK(scale_label({1.0f, 2.0f, 3.0f}) == "Scale (1.00, 2.00, 3.00)");
}

// --- ResetCommand tests ---

TEST_CASE("ResetCommand clears state and undo restores") {
    TransformState state;
    state.push(TS::make_translate({5, 5, 5}));
    state.push(TS::make_rotate({1, 0, 0}, 45.0f));

    Mat4 before = state.composite_matrix();

    ResetCommand cmd(state);
    cmd.execute();
    check_mat4_identity(state.composite_matrix());

    cmd.undo();
    Mat4 after = state.composite_matrix();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK(std::abs(before[c][r] - after[c][r]) < EPSILON);
        }
    }
}

// --- Composite transform order tests ---

TEST_CASE("Transforms compose in correct order (translate then rotate)") {
    TransformState state;
    state.push(TS::make_translate({1, 0, 0}));
    state.push(TS::make_rotate({0, 0, 1}, 90.0f));

    Mat4 m = state.composite_matrix();
    Vec4 v = linalg::mul(m, Vec4{0, 0, 0, 1});
    CHECK(std::abs(v.x - 0.0f) < EPSILON);
    CHECK(std::abs(v.y - 1.0f) < EPSILON);
}

TEST_CASE("Transforms compose in correct order (rotate then translate)") {
    TransformState state;
    state.push(TS::make_rotate({0, 0, 1}, 90.0f));
    state.push(TS::make_translate({1, 0, 0}));

    Mat4 m = state.composite_matrix();
    Vec4 v = linalg::mul(m, Vec4{0, 0, 0, 1});
    CHECK(std::abs(v.x - 1.0f) < EPSILON);
    CHECK(std::abs(v.y - 0.0f) < EPSILON);
}
