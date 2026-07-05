/// @file test_ipc.cpp
/// @brief Tests for IPC message construction and binary framing.
#include <doctest.h>
#include "ipc.h"
#include <cstdint>
#include <cstring>
#include <vector>

using json = nlohmann::json;

TEST_CASE("json ok response has correct shape") {
    json msg;
    msg["id"] = "test-uuid-1";
    msg["ok"] = true;
    msg["result"] = {{"message", "pong"}};

    CHECK(msg["id"] == "test-uuid-1");
    CHECK(msg["ok"] == true);
    CHECK(msg["result"]["message"] == "pong");
}

TEST_CASE("json error response has correct shape") {
    json msg;
    msg["id"] = "test-uuid-2";
    msg["ok"] = false;
    msg["error"] = {{"code", "UNKNOWN_COMMAND"}, {"message", "unknown command: foo"}};

    CHECK(msg["id"] == "test-uuid-2");
    CHECK(msg["ok"] == false);
    CHECK(msg["error"]["code"] == "UNKNOWN_COMMAND");
    CHECK(msg["error"]["message"] == "unknown command: foo");
}

TEST_CASE("json progress event has correct shape") {
    json msg;
    msg["id"] = "test-uuid-3";
    msg["event"] = "progress";
    msg["data"] = {{"step", 3}, {"total", 10}};

    CHECK(msg["id"] == "test-uuid-3");
    CHECK(msg["event"] == "progress");
    CHECK(msg["data"]["step"] == 3);
    CHECK(msg["data"]["total"] == 10);
}

TEST_CASE("binary frame LE encoding via write_le32") {
    uint32_t length = 0x04030201;
    uint8_t len_bytes[4];
    ipc::write_le32(len_bytes, length);

    CHECK(len_bytes[0] == 0x01);
    CHECK(len_bytes[1] == 0x02);
    CHECK(len_bytes[2] == 0x03);
    CHECK(len_bytes[3] == 0x04);
}

TEST_CASE("request parsing") {
    std::string raw = R"({"cmd":"ping","id":"abc-123","params":{}})";
    json request = json::parse(raw);

    CHECK(request["cmd"] == "ping");
    CHECK(request["id"] == "abc-123");
    CHECK(request["params"].is_object());
    CHECK(request["params"].empty());
}

TEST_CASE("request parsing with params") {
    std::string raw = R"({"cmd":"simulate","id":"def-456","params":{"steps":5}})";
    json request = json::parse(raw);

    CHECK(request["cmd"] == "simulate");
    CHECK(request["params"]["steps"] == 5);
}

TEST_CASE("malformed json returns discarded value with noexcept parse") {
    json result = json::parse("{bad json", nullptr, false);
    CHECK(result.is_discarded());
}
