/// @file ipc.cpp
/// @brief Stdio IPC implementation — JSON read loop, framed output, command dispatch.
#include "ipc.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <unordered_map>

namespace ipc {

static std::mutex s_write_mutex;
static std::unordered_map<std::string, CommandHandler> s_commands;

void send_json(const json& msg) {
    std::string payload = msg.dump() + "\n";
    std::lock_guard<std::mutex> lock(s_write_mutex);
    uint8_t prefix = FRAME_JSON;
    std::fwrite(&prefix, 1, 1, stdout);
    std::fwrite(payload.data(), 1, payload.size(), stdout);
    std::fflush(stdout);
}

void send_binary(const uint8_t* data, uint32_t length) {
    std::lock_guard<std::mutex> lock(s_write_mutex);
    uint8_t prefix = FRAME_BINARY;
    std::fwrite(&prefix, 1, 1, stdout);
    uint8_t len_bytes[4];
    write_le32(len_bytes, length);
    std::fwrite(len_bytes, 1, 4, stdout);
    std::fwrite(data, 1, length, stdout);
    std::fflush(stdout);
}

void send_ok(const std::string& id, const json& result) {
    json msg;
    msg["id"] = id;
    msg["ok"] = true;
    msg["result"] = result;
    send_json(msg);
}

void send_error(const std::string& id, const std::string& code, const std::string& message) {
    json msg;
    msg["id"] = id;
    msg["ok"] = false;
    msg["error"] = {{"code", code}, {"message", message}};
    send_json(msg);
}

void send_progress(const std::string& id, const json& data) {
    json msg;
    msg["id"] = id;
    msg["event"] = "progress";
    msg["data"] = data;
    send_json(msg);
}

void register_command(const std::string& name, CommandHandler handler) {
    s_commands[name] = handler;
}

void log_debug(const std::string& msg) {
    std::cerr << "[backend] " << msg << std::endl;
}

static void dispatch(const json& request) {
    std::string id;
    std::string cmd;

    if (!request.contains("id") || !request["id"].is_string()) {
        log_debug("received request without valid 'id' field");
        return;
    }
    id = request["id"].get<std::string>();

    if (!request.contains("cmd") || !request["cmd"].is_string()) {
        send_error(id, "INVALID_REQUEST", "missing or invalid 'cmd' field");
        return;
    }
    cmd = request["cmd"].get<std::string>();

    json params = json::object();
    if (request.contains("params") && request["params"].is_object()) {
        params = request["params"];
    }

    auto it = s_commands.find(cmd);
    if (it == s_commands.end()) {
        send_error(id, "UNKNOWN_COMMAND", "unknown command: " + cmd);
        return;
    }

    it->second(id, params);
}

void run_loop() {
    log_debug("ipc loop started");
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        json request = json::parse(line, nullptr, false);
        if (request.is_discarded()) {
            log_debug("json parse error on input");
            json err_msg;
            err_msg["id"] = nullptr;
            err_msg["ok"] = false;
            err_msg["error"] = {{"code", "PARSE_ERROR"}, {"message", "malformed JSON"}};
            send_json(err_msg);
            continue;
        }

        dispatch(request);
    }
    log_debug("ipc loop ended (stdin closed)");
}

} // namespace ipc
