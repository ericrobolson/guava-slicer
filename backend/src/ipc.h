/// @file ipc.h
/// @brief Stdio IPC protocol — JSON message framing and binary frame support.
#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace ipc {

using json = nlohmann::json;

/// Frame type prefix bytes written to stdout.
enum FrameType : uint8_t {
    FRAME_JSON   = 0x01,
    FRAME_BINARY = 0x02,
};

/// @brief Write a uint32 as 4 bytes in little-endian order.
inline void write_le32(uint8_t buf[4], uint32_t val) {
    buf[0] = static_cast<uint8_t>(val & 0xFF);
    buf[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    buf[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

/// @brief Send a JSON message to stdout (thread-safe).
void send_json(const json& msg);

/// @brief Send a binary frame to stdout (thread-safe).
void send_binary(const uint8_t* data, uint32_t length);

/// @brief Send an ok response for a given request ID.
void send_ok(const std::string& id, const json& result);

/// @brief Send an error response for a given request ID.
void send_error(const std::string& id, const std::string& code, const std::string& message);

/// @brief Send a progress event for a given request ID.
void send_progress(const std::string& id, const json& data);

/// @brief Command handler signature: receives request ID and params.
using CommandHandler = std::function<void(const std::string& id, const json& params)>;

/// @brief Register a command handler by name.
void register_command(const std::string& name, CommandHandler handler);

/// @brief Run the IPC read loop on stdin. Blocks until EOF or error.
void run_loop();

/// @brief Log a debug message to stderr.
void log_debug(const std::string& msg);

} // namespace ipc
