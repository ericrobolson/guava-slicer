/// @file commands.cpp
/// @brief Phase 1 command implementations: ping, simulate, get_binary.
#include "commands.h"
#include "ipc.h"

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

void register_all() {
    ipc::register_command("ping", handle_ping);
    ipc::register_command("simulate", handle_simulate);
    ipc::register_command("get_binary", handle_get_binary);
}

} // namespace commands
