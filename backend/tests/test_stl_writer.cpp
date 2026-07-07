/// @file test_stl_writer.cpp
/// @brief Tests for stl_writer export functionality.
#include <doctest.h>

#include "stl_writer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const std::string TEST_OUTPUT = "test_export_output.stl";

static void cleanup() {
    std::remove(TEST_OUTPUT.c_str());
}

static uint64_t file_size(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;
    std::fseek(f, 0, SEEK_END);
    uint64_t s = static_cast<uint64_t>(std::ftell(f));
    std::fclose(f);
    return s;
}

static uint32_t read_stl_triangle_count(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;
    std::fseek(f, 80, SEEK_SET);
    uint32_t count = 0;
    std::fread(&count, sizeof(uint32_t), 1, f);
    std::fclose(f);
    return count;
}

static constexpr uint32_t BYTES_PER_TRIANGLE = 50;
static constexpr uint32_t STL_HEADER_SIZE = 84;

TEST_CASE("export_combined_stl — model only, identity transform") {
    float verts[] = {0,0,0, 1,0,0, 0,1,0};
    uint32_t idx[] = {0,1,2};
    float identity[4][4] = {
        {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}
    };

    uint64_t sz = stl_writer::export_combined_stl(
        TEST_OUTPUT, verts, idx, 3, 1, identity, nullptr, nullptr, 0);

    CHECK(sz > 0);
    CHECK(sz == STL_HEADER_SIZE + BYTES_PER_TRIANGLE);
    CHECK(read_stl_triangle_count(TEST_OUTPUT) == 1);
    cleanup();
}

TEST_CASE("export_combined_stl — model with translation transform") {
    float verts[] = {0,0,0, 1,0,0, 0,1,0};
    uint32_t idx[] = {0,1,2};
    float translate_5x[4][4] = {
        {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {5,0,0,1}
    };

    uint64_t sz = stl_writer::export_combined_stl(
        TEST_OUTPUT, verts, idx, 3, 1, translate_5x, nullptr, nullptr, 0);

    CHECK(sz > 0);

    std::FILE* f = std::fopen(TEST_OUTPUT.c_str(), "rb");
    REQUIRE(f != nullptr);
    std::fseek(f, STL_HEADER_SIZE, SEEK_SET);

    float normal[3], v1[3], v2[3], v3[3];
    std::fseek(f, STL_HEADER_SIZE + 12, SEEK_SET);
    std::fread(v1, sizeof(float), 3, f);
    std::fread(v2, sizeof(float), 3, f);
    std::fread(v3, sizeof(float), 3, f);
    std::fclose(f);

    CHECK(v1[0] == doctest::Approx(5.0f));
    CHECK(v2[0] == doctest::Approx(6.0f));
    CHECK(v3[0] == doctest::Approx(5.0f));
    CHECK(v3[1] == doctest::Approx(1.0f));
    cleanup();
}

TEST_CASE("export_combined_stl — model + supports") {
    float model_verts[] = {0,0,0, 1,0,0, 0,1,0};
    uint32_t model_idx[] = {0,1,2};

    float sup_verts[] = {10,10,10, 11,10,10, 10,11,10};
    uint32_t sup_idx[] = {0,1,2};

    float identity[4][4] = {
        {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}
    };

    uint64_t sz = stl_writer::export_combined_stl(
        TEST_OUTPUT, model_verts, model_idx, 3, 1, identity,
        sup_verts, sup_idx, 1);

    CHECK(sz > 0);
    CHECK(sz == STL_HEADER_SIZE + 2 * BYTES_PER_TRIANGLE);
    CHECK(read_stl_triangle_count(TEST_OUTPUT) == 2);
    cleanup();
}

TEST_CASE("export_combined_stl — bad path returns 0") {
    float verts[] = {0,0,0, 1,0,0, 0,1,0};
    uint32_t idx[] = {0,1,2};
    float identity[4][4] = {
        {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}
    };

    uint64_t sz = stl_writer::export_combined_stl(
        "/nonexistent/dir/out.stl", verts, idx, 3, 1, identity, nullptr, nullptr, 0);

    CHECK(sz == 0);
}
