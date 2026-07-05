/// @file stl_parser.h
/// @brief STL file parser wrapper — builds indexed mesh from binary/ASCII STL via microstl.
#pragma once

#include "mesh.h"

#include <cstdint>
#include <functional>
#include <string>

namespace stl_parser {

/// @brief Error codes for STL parsing operations.
enum class Error : uint8_t {
    NONE = 0,
    FILE_NOT_FOUND,
    EMPTY_FILE,
    MMAP_FAILED,
    INVALID_STL,
    PARSE_ERROR,
    FACET_COUNT_EXCEEDED,
};

/// @brief Parsing result — either a mesh or an error.
struct ParseResult {
    mesh::Mesh mesh;
    Error error = Error::NONE;
    std::string error_message;

    bool ok() const { return error == Error::NONE; }
};

/// @brief Progress callback: (triangles_read, total_triangles).
using ProgressCallback = std::function<void(uint32_t read, uint32_t total)>;

/// @brief Parse an STL file (binary or ASCII) into an indexed mesh.
///
/// Memory-maps the file, auto-detects format, deduplicates vertices,
/// and builds flat GPU-ready arrays. Calls progress_cb periodically
/// during parsing for large files.
ParseResult parse_file(const std::string& path, ProgressCallback progress_cb = nullptr);

/// @brief Return a human-readable string for an error code.
const char* error_string(Error err);

} // namespace stl_parser
