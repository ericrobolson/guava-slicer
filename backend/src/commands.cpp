/// @file commands.cpp
/// @brief IPC command implementations.
#include "commands.h"
#include "app_state.h"
#include "ipc.h"
#include "mesh_ops.h"
#include "stl_parser.h"
#include "transform.h"

#include <cstdint>
#include <thread>
#include <vector>

namespace commands {

static const int DEFAULT_SIMULATE_STEPS = 10;
static const int SIMULATE_STEP_DELAY_MS = 200;
static const uint32_t DEFAULT_BINARY_SIZE = 1024;
static const uint32_t MAX_BINARY_SIZE = 10 * 1024 * 1024;

/// @brief Ping handler — immediate pong response.
static void handle_ping(const std::string& id, const ipc::json& /*params*/) {
    ipc::send_ok(id, {{"message", "pong"}});
}

/// @brief Simulate handler — runs on a worker thread, streams progress events.
static void handle_simulate(const std::string& id, const ipc::json& params) {
    int steps = DEFAULT_SIMULATE_STEPS;
    if (params.contains("steps") && params["steps"].is_number_integer()) {
        steps = params["steps"].get<int>();
        if (steps <= 0) {
            ipc::send_error(id, "INVALID_PARAMS", "'steps' must be a positive integer");
            return;
        }
    }

    std::thread worker([id, steps]() {
        ipc::log_debug("simulate: starting " + std::to_string(steps) + " steps");
        for (int i = 1; i <= steps; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(SIMULATE_STEP_DELAY_MS));
            ipc::send_progress(id, {{"step", i}, {"total", steps}});
        }
        ipc::send_ok(id, {{"completed", true}});
        ipc::log_debug("simulate: finished");
    });
    worker.detach();
}

/// @brief Get binary handler — sends a JSON response followed by a binary frame.
static void handle_get_binary(const std::string& id, const ipc::json& params) {
    uint32_t size = DEFAULT_BINARY_SIZE;
    if (params.contains("size") && params["size"].is_number_unsigned()) {
        size = params["size"].get<uint32_t>();
    }

    if (size > MAX_BINARY_SIZE) {
        ipc::send_error(id, "INVALID_PARAMS",
                        "requested size exceeds maximum of " + std::to_string(MAX_BINARY_SIZE));
        return;
    }

    std::vector<uint8_t> payload(size);
    for (uint32_t i = 0; i < size; ++i) {
        payload[i] = static_cast<uint8_t>(i & 0xFF);
    }

    ipc::send_ok(id, {{"byte_count", size}, {"binary_follows", true}});
    ipc::send_binary(payload.data(), size);
}

/// @brief Map stl_parser error codes to IPC error code strings.
static const char* stl_error_to_ipc_code(stl_parser::Error err) {
    switch (err) {
        case stl_parser::Error::FILE_NOT_FOUND:       return "IO_ERROR";
        case stl_parser::Error::EMPTY_FILE:            return "IO_ERROR";
        case stl_parser::Error::MMAP_FAILED:           return "IO_ERROR";
        case stl_parser::Error::INVALID_STL:           return "INVALID_STL";
        case stl_parser::Error::PARSE_ERROR:           return "PARSE_ERROR";
        case stl_parser::Error::FACET_COUNT_EXCEEDED:  return "OUT_OF_MEMORY";
        default:                                       return "PARSE_ERROR";
    }
}

/// @brief Send mesh geometry as binary frames (vertices, normals, indices).
static void send_mesh_binary(const mesh::Mesh& m) {
    auto send_buffer = [](const auto& vec) {
        ipc::send_binary(
            reinterpret_cast<const uint8_t*>(vec.data()),
            static_cast<uint32_t>(vec.size() * sizeof(vec[0])));
    };
    send_buffer(m.vertices);
    send_buffer(m.normals);
    send_buffer(m.indices);
}

/// @brief Command that loads a mesh into app state. Undoing clears the mesh.
struct LoadMeshCommand : command::Command {
    mesh::Mesh new_mesh;
    mesh::Mesh previous_mesh;
    bool had_previous;
    transform::TransformState::Snapshot previous_transforms;
    app_state::AppState& state;

    LoadMeshCommand(mesh::Mesh m, app_state::AppState& s)
        : new_mesh(std::move(m)), state(s) {}

    void execute() override {
        previous_mesh = std::move(state.mesh);
        had_previous = state.has_mesh;
        previous_transforms = state.transforms.snapshot();
        state.mesh = std::move(new_mesh);
        state.has_mesh = true;
        state.transforms.clear();
    }

    void undo() override {
        new_mesh = std::move(state.mesh);
        state.mesh = std::move(previous_mesh);
        state.has_mesh = had_previous;
        state.transforms.restore(previous_transforms);
    }

    std::string name() const override { return "Load mesh"; }
};

/// @brief Command that pushes a transform entry to the TransformState.
struct TransformCommand : command::Command {
    transform::TransformState::Entry entry;
    std::string label;
    transform::TransformState& state;

    TransformCommand(transform::TransformState::Entry e, std::string l, transform::TransformState& s)
        : entry(e), label(std::move(l)), state(s) {}

    void execute() override { state.push(entry); }
    void undo() override { state.pop(); }
    std::string name() const override { return label; }
};

/// @brief Load mesh handler — parses STL on a worker thread, stores in app state.
static void handle_load_mesh(const std::string& id, const ipc::json& params) {
    if (!params.contains("path") || !params["path"].is_string()) {
        ipc::send_error(id, "INVALID_PARAMS", "missing or invalid 'path' parameter");
        return;
    }

    std::string path = params["path"].get<std::string>();

    std::thread worker([id, path]() {
        ipc::log_debug("load_mesh: parsing " + path);

        auto progress_cb = [&id](uint32_t read, uint32_t total) {
            ipc::send_progress(id, {{"triangles_read", read}, {"total", total}});
        };

        stl_parser::ParseResult result = stl_parser::parse_file(path, progress_cb);

        if (!result.ok()) {
            ipc::send_error(id, stl_error_to_ipc_code(result.error), result.error_message);
            ipc::log_debug("load_mesh: failed — " + result.error_message);
            return;
        }

        ipc::log_debug("load_mesh: parsed " + std::to_string(result.mesh.triangle_count) +
                        " triangles, " + std::to_string(result.mesh.vertex_count) + " unique vertices");

        mesh_ops::center_at_centroid(
            result.mesh.vertices.data(), result.mesh.vertex_count, result.mesh.bounding_box);

        auto& state = app_state::get();
        auto cmd = std::make_unique<LoadMeshCommand>(std::move(result.mesh), state);
        state.commands.push(std::move(cmd));

        const mesh::Mesh& m = state.mesh;
        send_mesh_binary(m);

        ipc::json bbox_min = {m.bounding_box.min.x, m.bounding_box.min.y, m.bounding_box.min.z};
        ipc::json bbox_max = {m.bounding_box.max.x, m.bounding_box.max.y, m.bounding_box.max.z};

        auto resp = app_state::transform_response(state);
        resp["vertex_count"] = m.vertex_count;
        resp["triangle_count"] = m.triangle_count;
        resp["bounding_box"] = {{"min", bbox_min}, {"max", bbox_max}};
        resp["file_size_bytes"] = m.file_size_bytes;
        resp["skipped_triangles"] = m.skipped_triangles;

        ipc::send_ok(id, resp);
        ipc::log_debug("load_mesh: complete");
    });
    worker.detach();
}

/// @brief Orient model handler — applies rotate, translate, or scale transforms.
static void handle_orient_model(const std::string& id, const ipc::json& params) {
    if (!params.contains("type") || !params["type"].is_string()) {
        ipc::send_error(id, "INVALID_PARAMS", "missing or invalid 'type' parameter");
        return;
    }

    auto& state = app_state::get();
    if (!state.has_mesh) {
        ipc::send_error(id, "NO_MESH", "no mesh loaded");
        return;
    }

    std::string type = params["type"].get<std::string>();

    if (type == "rotate") {
        // Build rotation matrix from either quaternion or axis+angle
        transform::Mat4 rot;
        std::string label;

        if (params.contains("quaternion") && params["quaternion"].is_array() && params["quaternion"].size() == 4) {
            transform::Vec4 quat = {
                params["quaternion"][0].get<float>(),
                params["quaternion"][1].get<float>(),
                params["quaternion"][2].get<float>(),
                params["quaternion"][3].get<float>(),
            };
            rot = linalg::rotation_matrix(quat);
            label = "Rotate";

        } else if (params.contains("axis") && params["axis"].is_array() && params["axis"].size() == 3 &&
                   params.contains("angle") && params["angle"].is_number()) {
            transform::Vec3 axis = {
                params["axis"][0].get<float>(),
                params["axis"][1].get<float>(),
                params["axis"][2].get<float>(),
            };
            float angle = params["angle"].get<float>();
            float rad = angle * transform::DEG_TO_RAD;
            transform::Vec4 q = linalg::rotation_quat(axis, rad);
            rot = linalg::rotation_matrix(q);
            label = transform::rotate_label(axis, angle);

        } else {
            ipc::send_error(id, "INVALID_PARAMS",
                "rotate requires 'quaternion' as [x,y,z,w] or 'axis'+'angle'");
            return;
        }

        auto center = mesh_ops::translation_of(state.transforms.composite_matrix());
        auto compound = mesh_ops::rotation_around_point(rot, center);
        auto entry = transform::TransformState::make_matrix(compound);
        auto cmd = std::make_unique<TransformCommand>(entry, label, state.transforms);
        state.commands.push(std::move(cmd));

    } else if (type == "translate") {
        if (!params.contains("delta") || !params["delta"].is_array() || params["delta"].size() != 3) {
            ipc::send_error(id, "INVALID_PARAMS", "translate requires 'delta' as [x,y,z]");
            return;
        }

        transform::Vec3 delta = {
            params["delta"][0].get<float>(),
            params["delta"][1].get<float>(),
            params["delta"][2].get<float>(),
        };

        auto entry = transform::TransformState::make_translate(delta);
        auto cmd = std::make_unique<TransformCommand>(entry, transform::translate_label(delta), state.transforms);
        state.commands.push(std::move(cmd));

    } else if (type == "scale") {
        if (!params.contains("factor") || !params["factor"].is_array() || params["factor"].size() != 3) {
            ipc::send_error(id, "INVALID_PARAMS", "scale requires 'factor' as [x,y,z]");
            return;
        }

        transform::Vec3 factor = {
            params["factor"][0].get<float>(),
            params["factor"][1].get<float>(),
            params["factor"][2].get<float>(),
        };

        if (factor.x <= 0.0f || factor.y <= 0.0f || factor.z <= 0.0f) {
            ipc::send_error(id, "INVALID_PARAMS", "scale factors must be > 0");
            return;
        }

        auto center = mesh_ops::translation_of(state.transforms.composite_matrix());
        auto compound = mesh_ops::scale_around_point(factor, center);
        auto entry = transform::TransformState::make_matrix(compound);
        auto cmd = std::make_unique<TransformCommand>(entry, transform::scale_label(factor), state.transforms);
        state.commands.push(std::move(cmd));

    } else if (type == "place_on_plate") {
        float min_y = mesh_ops::min_transformed_y(
            state.mesh.vertices.data(), state.mesh.vertex_count,
            state.transforms.composite_matrix());
        float dy = -min_y;
        if (std::abs(dy) < 1e-6f) {
            ipc::send_ok(id, app_state::transform_response(state));
            return;
        }

        transform::Vec3 delta = {0.0f, dy, 0.0f};
        auto entry = transform::TransformState::make_translate(delta);
        auto cmd = std::make_unique<TransformCommand>(entry, "Place on build plate", state.transforms);
        state.commands.push(std::move(cmd));

    } else if (type == "center_on_plate") {
        auto delta = mesh_ops::center_on_plate_delta(
            state.mesh.vertices.data(), state.mesh.vertex_count,
            state.transforms.composite_matrix());
        if (std::abs(delta.x) < 1e-6f && std::abs(delta.z) < 1e-6f) {
            ipc::send_ok(id, app_state::transform_response(state));
            return;
        }

        auto entry = transform::TransformState::make_translate(delta);
        auto cmd = std::make_unique<TransformCommand>(entry, "Center on build plate", state.transforms);
        state.commands.push(std::move(cmd));

    } else if (type == "reset") {
        auto cmd = std::make_unique<transform::ResetCommand>(state.transforms);
        state.commands.push(std::move(cmd));

    } else {
        ipc::send_error(id, "INVALID_PARAMS", "unknown transform type: " + type);
        return;
    }

    ipc::send_ok(id, app_state::transform_response(state));
}

/// @brief Undo handler — undoes the most recent command.
static void handle_undo(const std::string& id, const ipc::json& /*params*/) {
    auto& state = app_state::get();

    if (!state.commands.can_undo()) {
        ipc::send_error(id, "NOTHING_TO_UNDO", "nothing to undo");
        return;
    }

    std::string action = state.commands.last_undo_name();
    state.commands.undo();

    ipc::send_ok(id, app_state::undo_redo_response(state, action));
}

/// @brief Redo handler — redoes the most recently undone command.
static void handle_redo(const std::string& id, const ipc::json& /*params*/) {
    auto& state = app_state::get();

    if (!state.commands.can_redo()) {
        ipc::send_error(id, "NOTHING_TO_REDO", "nothing to redo");
        return;
    }

    std::string action = state.commands.last_redo_name();
    state.commands.redo();

    ipc::send_ok(id, app_state::undo_redo_response(state, action));
}

void register_all() {
    ipc::register_command("ping", handle_ping);
    ipc::register_command("simulate", handle_simulate);
    ipc::register_command("get_binary", handle_get_binary);
    ipc::register_command("load_mesh", handle_load_mesh);
    ipc::register_command("orient_model", handle_orient_model);
    ipc::register_command("undo", handle_undo);
    ipc::register_command("redo", handle_redo);
}

} // namespace commands
