/// @file transform.h
/// @brief Transform commands (rotate, translate, scale, reset) and composite transform state.
#pragma once

#include "command.h"
#include "linalg_types.h"
#include "mesh.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace transform {

using linalg_types::Vec3;
using linalg_types::Vec4;
using linalg_types::Mat4;

constexpr float PI = 3.14159265358979323846f;
constexpr float DEG_TO_RAD = PI / 180.0f;

/// @brief Build a 4x4 scaling matrix from per-axis factors.
inline Mat4 scaling_matrix(const Vec3& s) {
    return {{s.x,0,0,0}, {0,s.y,0,0}, {0,0,s.z,0}, {0,0,0,1}};
}

/// @brief Discriminator for transform command types in IPC.
enum class TransformType : uint8_t {
    ROTATE,
    TRANSLATE,
    SCALE,
    MATRIX,
};

/// @brief Generate a human-readable label for a rotation (e.g., "Rotate X 90").
std::string rotate_label(Vec3 axis, float angle_degrees);

/// @brief Generate a human-readable label for a translation.
std::string translate_label(Vec3 delta);

/// @brief Generate a human-readable label for a scale operation.
std::string scale_label(Vec3 factor);

/// @brief Tracks the ordered list of transform operations and computes the composite matrix.
///
/// Transforms are stored as discrete operations. The composite matrix is recomputed
/// from the full list on every mutation (the list is expected to be small).
class TransformState {
public:
    /// @brief A single transform operation stored in the state.
    struct Entry {
        TransformType type;
        Vec3 axis_or_delta_or_factor;
        float angle;
        Mat4 mat;
    };

    /// @brief Snapshot of the entry list for undo support.
    struct Snapshot {
        std::vector<Entry> entries;
    };

    /// @brief Build a ROTATE entry.
    static Entry make_rotate(Vec3 axis, float angle_degrees);

    /// @brief Build a TRANSLATE entry.
    static Entry make_translate(Vec3 delta);

    /// @brief Build a SCALE entry.
    static Entry make_scale(Vec3 factor);

    /// @brief Build a MATRIX entry.
    static Entry make_matrix(Mat4 m);

    /// @brief Append a transform entry.
    void push(const Entry& entry);

    /// @brief Remove the last transform entry.
    void pop();

    /// @brief Compute the composite 4x4 matrix from all active transforms.
    Mat4 composite_matrix() const;

    /// @brief Apply the composite matrix to an axis-aligned bounding box.
    mesh::BoundingBox transformed_bounding_box(const mesh::BoundingBox& original) const;

    /// @brief Compute dimensions (width, height, depth) of the transformed bounding box.
    Vec3 dimensions(const mesh::BoundingBox& original) const;

    /// @brief Number of active transform operations.
    uint32_t count() const;

    /// @brief Clear all transforms (reset to identity).
    void clear();

    /// @brief Capture current state as a snapshot.
    Snapshot snapshot() const;

    /// @brief Restore state from a snapshot.
    void restore(const Snapshot& snap);

private:
    std::vector<Entry> entries_;
};

/// @brief Reset all transforms to identity. Captures the prior state for undo.
struct ResetCommand : command::Command {
    TransformState& state;
    TransformState::Snapshot saved;

    explicit ResetCommand(TransformState& s);
    void execute() override;
    void undo() override;
    std::string name() const override;
};

} // namespace transform
