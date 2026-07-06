/// @file transform.cpp
/// @brief Transform command and TransformState implementation.
#include "transform.h"

#include <cmath>
#include <limits>

namespace transform {

// --- Label generators ---

std::string rotate_label(Vec3 axis, float angle_degrees) {
    const char* axis_name = "XYZ";
    int axis_idx = 0;
    if (axis.y > 0.5f) axis_idx = 1;
    else if (axis.z > 0.5f) axis_idx = 2;
    char buf[64];
    snprintf(buf, sizeof(buf), "Rotate %c %.0f", axis_name[axis_idx], angle_degrees);
    return buf;
}

std::string translate_label(Vec3 delta) {
    char buf[96];
    snprintf(buf, sizeof(buf), "Translate (%.1f, %.1f, %.1f)", delta.x, delta.y, delta.z);
    return buf;
}

std::string scale_label(Vec3 factor) {
    if (factor.x == factor.y && factor.y == factor.z) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Scale %.2f", factor.x);
        return buf;
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "Scale (%.2f, %.2f, %.2f)", factor.x, factor.y, factor.z);
    return buf;
}

// --- TransformState ---

TransformState::Entry TransformState::make_rotate(Vec3 axis, float angle_degrees) {
    return {TransformType::ROTATE, axis, angle_degrees, linalg::identity};
}

TransformState::Entry TransformState::make_translate(Vec3 delta) {
    return {TransformType::TRANSLATE, delta, 0.0f, linalg::identity};
}

TransformState::Entry TransformState::make_scale(Vec3 factor) {
    return {TransformType::SCALE, factor, 0.0f, linalg::identity};
}

TransformState::Entry TransformState::make_matrix(Mat4 m) {
    return {TransformType::MATRIX, {0,0,0}, 0.0f, m};
}

void TransformState::push(const Entry& entry) {
    entries_.push_back(entry);
}

void TransformState::pop() {
    if (!entries_.empty()) entries_.pop_back();
}

Mat4 TransformState::composite_matrix() const {
    Mat4 result = linalg::identity;
    for (const auto& e : entries_) {
        Mat4 m = linalg::identity;
        switch (e.type) {
            case TransformType::ROTATE: {
                float rad = e.angle * DEG_TO_RAD;
                Vec4 q = linalg::rotation_quat(e.axis_or_delta_or_factor, rad);
                m = linalg::rotation_matrix(q);
                break;
            }
            case TransformType::TRANSLATE:
                m = linalg::translation_matrix(e.axis_or_delta_or_factor);
                break;
            case TransformType::SCALE:
                m = scaling_matrix(e.axis_or_delta_or_factor);
                break;
            case TransformType::MATRIX:
                m = e.mat;
                break;
            default:
                break;
        }
        result = linalg::mul(m, result);
    }
    return result;
}

mesh::BoundingBox TransformState::transformed_bounding_box(const mesh::BoundingBox& original) const {
    Mat4 mat = composite_matrix();

    // Transform all 8 corners of the AABB and find the new AABB
    Vec3 corners[8] = {
        {original.min.x, original.min.y, original.min.z},
        {original.max.x, original.min.y, original.min.z},
        {original.min.x, original.max.y, original.min.z},
        {original.max.x, original.max.y, original.min.z},
        {original.min.x, original.min.y, original.max.z},
        {original.max.x, original.min.y, original.max.z},
        {original.min.x, original.max.y, original.max.z},
        {original.max.x, original.max.y, original.max.z},
    };

    constexpr float F_MAX = std::numeric_limits<float>::max();
    mesh::BoundingBox result;
    result.min = {F_MAX, F_MAX, F_MAX};
    result.max = {-F_MAX, -F_MAX, -F_MAX};

    for (const auto& c : corners) {
        Vec4 h = {c.x, c.y, c.z, 1.0f};
        Vec4 t = linalg::mul(mat, h);
        Vec3 p = {t.x, t.y, t.z};

        result.min.x = std::min(result.min.x, p.x);
        result.min.y = std::min(result.min.y, p.y);
        result.min.z = std::min(result.min.z, p.z);
        result.max.x = std::max(result.max.x, p.x);
        result.max.y = std::max(result.max.y, p.y);
        result.max.z = std::max(result.max.z, p.z);
    }

    return result;
}

Vec3 TransformState::dimensions(const mesh::BoundingBox& original) const {
    auto bbox = transformed_bounding_box(original);
    return {bbox.max.x - bbox.min.x, bbox.max.y - bbox.min.y, bbox.max.z - bbox.min.z};
}

uint32_t TransformState::count() const {
    return static_cast<uint32_t>(entries_.size());
}

void TransformState::clear() {
    entries_.clear();
}

TransformState::Snapshot TransformState::snapshot() const {
    return {entries_};
}

void TransformState::restore(const Snapshot& snap) {
    entries_ = snap.entries;
}

// --- ResetCommand ---

ResetCommand::ResetCommand(TransformState& s)
    : state(s) {}

void ResetCommand::execute() {
    saved = state.snapshot();
    state.clear();
}

void ResetCommand::undo() {
    state.restore(saved);
}

std::string ResetCommand::name() const {
    return "Reset orientation";
}

} // namespace transform
