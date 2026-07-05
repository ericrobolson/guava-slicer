/// @file commands.cpp
/// @brief IPC command implementations.
#include "commands.h"
#include "ipc.h"
#include "stl_parser.h"

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

/// @brief Load mesh handler — parses STL on a worker thread, sends geometry as binary frames.
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

        const mesh::Mesh& m = result.mesh;
        ipc::log_debug("load_mesh: parsed " + std::to_string(m.triangle_count) +
                        " triangles, " + std::to_string(m.vertex_count) + " unique vertices");

        auto send_buffer = [](const auto& vec) {
            ipc::send_binary(
                reinterpret_cast<const uint8_t*>(vec.data()),
                static_cast<uint32_t>(vec.size() * sizeof(vec[0])));
        };
        send_buffer(m.vertices);
        send_buffer(m.normals);
        send_buffer(m.indices);

        ipc::json bbox_min = {m.bounding_box.min.x, m.bounding_box.min.y, m.bounding_box.min.z};
        ipc::json bbox_max = {m.bounding_box.max.x, m.bounding_box.max.y, m.bounding_box.max.z};

        ipc::send_ok(id, {
            {"vertex_count", m.vertex_count},
            {"triangle_count", m.triangle_count},
            {"bounding_box", {{"min", bbox_min}, {"max", bbox_max}}},
            {"file_size_bytes", m.file_size_bytes},
            {"skipped_triangles", m.skipped_triangles},
        });

        ipc::log_debug("load_mesh: complete");
    });
    worker.detach();
}

void register_all() {
    ipc::register_command("ping", handle_ping);
    ipc::register_command("simulate", handle_simulate);
    ipc::register_command("get_binary", handle_get_binary);
    ipc::register_command("load_mesh", handle_load_mesh);
}

} // namespace commands
