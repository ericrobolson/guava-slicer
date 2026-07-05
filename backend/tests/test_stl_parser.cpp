/// @file test_stl_parser.cpp
/// @brief Tests for STL parser wrapper.
#include <doctest.h>
#include "stl_parser.h"
#include "mmap.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static const char* TEST_DATA_DIR = "tests/data";

/// @brief Create test data directory if it doesn't exist.
static void ensure_test_dir() {
    std::string cmd = std::string("mkdir -p ") + TEST_DATA_DIR;
    (void)std::system(cmd.c_str());
}

/// @brief Write a minimal binary STL with the given triangles.
static std::string write_binary_stl(const char* name, uint32_t num_triangles,
                                     const std::vector<float>& triangle_data) {
    ensure_test_dir();
    std::string path = std::string(TEST_DATA_DIR) + "/" + name;
    std::ofstream f(path, std::ios::binary);

    uint8_t header[80] = {};
    std::memcpy(header, "binary stl test", 15);
    f.write(reinterpret_cast<const char*>(header), 80);
    f.write(reinterpret_cast<const char*>(&num_triangles), 4);

    for (uint32_t i = 0; i < num_triangles; ++i) {
        size_t base = i * 12;
        f.write(reinterpret_cast<const char*>(&triangle_data[base]), 12 * sizeof(float));
        uint16_t attr = 0;
        f.write(reinterpret_cast<const char*>(&attr), 2);
    }

    f.close();
    return path;
}

/// @brief Write a minimal ASCII STL file.
static std::string write_ascii_stl(const char* name, const std::string& content) {
    ensure_test_dir();
    std::string path = std::string(TEST_DATA_DIR) + "/" + name;
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

TEST_CASE("parse_file returns error for nonexistent file") {
    auto result = stl_parser::parse_file("/nonexistent/path/model.stl");
    CHECK_FALSE(result.ok());
    CHECK(result.error == stl_parser::Error::FILE_NOT_FOUND);
}

TEST_CASE("parse_file returns error for empty file") {
    ensure_test_dir();
    std::string path = std::string(TEST_DATA_DIR) + "/empty.stl";
    std::ofstream f(path);
    f.close();

    auto result = stl_parser::parse_file(path);
    CHECK_FALSE(result.ok());
    CHECK(result.error == stl_parser::Error::EMPTY_FILE);
}

TEST_CASE("parse_file handles binary STL with one triangle") {
    // normal(0,0,1), v0(0,0,0), v1(1,0,0), v2(0,1,0)
    std::vector<float> data = {
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };

    auto path = write_binary_stl("one_tri.stl", 1, data);
    auto result = stl_parser::parse_file(path);

    REQUIRE(result.ok());
    CHECK(result.mesh.triangle_count == 1);
    CHECK(result.mesh.vertex_count == 3);
    CHECK(result.mesh.indices.size() == 3);
    CHECK(result.mesh.vertices.size() == 9);
    CHECK(result.mesh.normals.size() == 9);
}

TEST_CASE("parse_file deduplicates shared vertices") {
    // Two triangles sharing an edge: (0,0,0)-(1,0,0)
    // normal(0,0,1) for both
    std::vector<float> data = {
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,

        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 0.0f,
    };

    auto path = write_binary_stl("two_tri_shared.stl", 2, data);
    auto result = stl_parser::parse_file(path);

    REQUIRE(result.ok());
    CHECK(result.mesh.triangle_count == 2);
    CHECK(result.mesh.vertex_count == 4);
    CHECK(result.mesh.indices.size() == 6);
}

TEST_CASE("parse_file computes correct bounding box") {
    std::vector<float> data = {
        0.0f, 0.0f, 1.0f,
        -1.0f, -2.0f, -3.0f,
         5.0f,  0.0f,  0.0f,
         0.0f,  4.0f,  7.0f,
    };

    auto path = write_binary_stl("bbox.stl", 1, data);
    auto result = stl_parser::parse_file(path);

    REQUIRE(result.ok());
    CHECK(result.mesh.bounding_box.min.x == doctest::Approx(-1.0f));
    CHECK(result.mesh.bounding_box.min.y == doctest::Approx(-2.0f));
    CHECK(result.mesh.bounding_box.min.z == doctest::Approx(-3.0f));
    CHECK(result.mesh.bounding_box.max.x == doctest::Approx(5.0f));
    CHECK(result.mesh.bounding_box.max.y == doctest::Approx(4.0f));
    CHECK(result.mesh.bounding_box.max.z == doctest::Approx(7.0f));
}

TEST_CASE("parse_file handles ASCII STL") {
    std::string content = R"(solid test
facet normal 0 0 1
  outer loop
    vertex 0 0 0
    vertex 1 0 0
    vertex 0 1 0
  endloop
endfacet
endsolid test
)";

    auto path = write_ascii_stl("ascii.stl", content);
    auto result = stl_parser::parse_file(path);

    REQUIRE(result.ok());
    CHECK(result.mesh.triangle_count == 1);
    CHECK(result.mesh.vertex_count == 3);
}

TEST_CASE("parse_file calls progress callback") {
    std::vector<float> data;
    static const uint32_t TRI_COUNT = 15000;
    for (uint32_t i = 0; i < TRI_COUNT; ++i) {
        float fi = static_cast<float>(i);
        // normal
        data.push_back(0.0f); data.push_back(0.0f); data.push_back(1.0f);
        // v0
        data.push_back(fi); data.push_back(0.0f); data.push_back(0.0f);
        // v1
        data.push_back(fi + 1.0f); data.push_back(0.0f); data.push_back(0.0f);
        // v2
        data.push_back(fi); data.push_back(1.0f); data.push_back(0.0f);
    }

    auto path = write_binary_stl("progress.stl", TRI_COUNT, data);

    uint32_t callback_count = 0;
    auto result = stl_parser::parse_file(path, [&](uint32_t /*read*/, uint32_t /*total*/) {
        ++callback_count;
    });

    REQUIRE(result.ok());
    CHECK(callback_count > 0);
    CHECK(result.mesh.triangle_count == TRI_COUNT);
}

TEST_CASE("parse_file records file size") {
    std::vector<float> data = {
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };

    auto path = write_binary_stl("filesize.stl", 1, data);
    auto result = stl_parser::parse_file(path);

    REQUIRE(result.ok());
    // 80-byte header + 4-byte count + 1 * (48 bytes + 2 byte attr) = 134
    CHECK(result.mesh.file_size_bytes == 134);
}

TEST_CASE("mmap returns error for nonexistent file") {
    auto mf = mapped_file::map_file("/nonexistent/file.stl");
    CHECK_FALSE(mf.ok());
    CHECK(mf.error == mapped_file::Error::FILE_NOT_FOUND);
}
