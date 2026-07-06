/// @file app_state.cpp
/// @brief Global application state implementation.
#include "app_state.h"

namespace app_state {

AppState& get() {
    static AppState instance;
    return instance;
}

/// @brief Serialize a 4x4 matrix as a flat 16-element JSON array (column-major).
static json mat4_to_json(const transform::Mat4& m) {
    json arr = json::array();
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            arr.push_back(m[col][row]);
        }
    }
    return arr;
}

json transform_response(const AppState& state) {
    auto mat = state.transforms.composite_matrix();
    auto bbox = state.transforms.transformed_bounding_box(state.mesh.bounding_box);
    auto dims = state.transforms.dimensions(state.mesh.bounding_box);

    return {
        {"transform_matrix", mat4_to_json(mat)},
        {"bounding_box", {
            {"min", {bbox.min.x, bbox.min.y, bbox.min.z}},
            {"max", {bbox.max.x, bbox.max.y, bbox.max.z}},
        }},
        {"dimensions", {dims.x, dims.y, dims.z}},
        {"can_undo", state.commands.can_undo()},
        {"can_redo", state.commands.can_redo()},
        {"undo_name", state.commands.last_undo_name()},
        {"redo_name", state.commands.last_redo_name()},
    };
}

json undo_redo_response(const AppState& state, const std::string& action_name) {
    auto resp = transform_response(state);
    resp["action_name"] = action_name;
    resp["has_mesh"] = state.has_mesh;
    return resp;
}

} // namespace app_state
