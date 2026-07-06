/// @file app_state.h
/// @brief Global application state — mesh, command stack, transform state.
#pragma once

#include "command.h"
#include "mesh.h"
#include "transform.h"

#include <nlohmann/json.hpp>
#include <string>

namespace app_state {

using json = nlohmann::json;

/// @brief Singleton application state.
struct AppState {
    mesh::Mesh mesh;
    bool has_mesh = false;
    command::CommandStack commands;
    transform::TransformState transforms;
};

/// @brief Get the global application state.
AppState& get();

/// @brief Build the standard transform response JSON (matrix, bbox, dimensions, undo/redo state).
json transform_response(const AppState& state);

/// @brief Build the undo/redo response JSON (same as transform_response + action name).
json undo_redo_response(const AppState& state, const std::string& action_name);

} // namespace app_state
