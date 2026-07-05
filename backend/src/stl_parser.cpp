/// @file stl_parser.cpp
/// @brief STL parser implementation — wraps microstl with vertex dedup and progress.
#include "stl_parser.h"
#include "mmap.h"

#include <microstl/microstl.h>

#include <cmath>
#include <cstring>
#include <unordered_map>

namespace stl_parser {

static const uint32_t PROGRESS_INTERVAL = 100000;

/// @brief Hash for vertex deduplication keyed on raw float bits.
struct VertexHash {
    size_t operator()(const mesh::Vec3& v) const {
        uint32_t bx, by, bz;
        std::memcpy(&bx, &v.x, sizeof(uint32_t));
        std::memcpy(&by, &v.y, sizeof(uint32_t));
        std::memcpy(&bz, &v.z, sizeof(uint32_t));
        size_t h = bx;
        h ^= by + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= bz + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct VertexEqual {
    bool operator()(const mesh::Vec3& a, const mesh::Vec3& b) const {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
};

/// @brief microstl handler that builds an indexed mesh with vertex dedup.
class MeshBuilder : public microstl::Reader::Handler {
public:
    mesh::Mesh result;
    ProgressCallback progress_cb;
    uint32_t total_triangles = 0;
    uint32_t triangles_read = 0;
    uint32_t skipped = 0;

    MeshBuilder() {
        result.bounding_box.min = mesh::Vec3{
             std::numeric_limits<float>::max(),
             std::numeric_limits<float>::max(),
             std::numeric_limits<float>::max()};
        result.bounding_box.max = mesh::Vec3{
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max()};
    }

    void onFacetCount(uint32_t count) override {
        total_triangles = count;
        result.vertices.reserve(count * 3 * 3);
        result.normals.reserve(count * 3 * 3);
        result.indices.reserve(count * 3);
        vertex_map.reserve(count * 3);
    }

    void onFacet(const float v1[3], const float v2[3], const float v3[3], const float n[3]) override {
        uint32_t i0 = add_vertex(v1, n);
        uint32_t i1 = add_vertex(v2, n);
        uint32_t i2 = add_vertex(v3, n);

        result.indices.push_back(i0);
        result.indices.push_back(i1);
        result.indices.push_back(i2);

        ++triangles_read;

        if (progress_cb && (triangles_read % PROGRESS_INTERVAL == 0)) {
            progress_cb(triangles_read, total_triangles);
        }
    }

    void onEnd(microstl::Result /*result*/) override {
        result.vertex_count = static_cast<uint32_t>(result.vertices.size() / 3);
        result.triangle_count = triangles_read;
        result.skipped_triangles = skipped;
    }

private:
    std::unordered_map<mesh::Vec3, uint32_t, VertexHash, VertexEqual> vertex_map;

    /// @brief Add a vertex, deduplicating by position. Returns the index.
    uint32_t add_vertex(const float pos[3], const float normal[3]) {
        mesh::Vec3 key{pos[0], pos[1], pos[2]};

        auto it = vertex_map.find(key);
        if (it != vertex_map.end()) {
            return it->second;
        }

        uint32_t idx = static_cast<uint32_t>(result.vertices.size() / 3);
        result.vertices.push_back(pos[0]);
        result.vertices.push_back(pos[1]);
        result.vertices.push_back(pos[2]);
        result.normals.push_back(normal[0]);
        result.normals.push_back(normal[1]);
        result.normals.push_back(normal[2]);

        update_bounds(key);
        vertex_map[key] = idx;
        return idx;
    }

    void update_bounds(const mesh::Vec3& v) {
        result.bounding_box.min = linalg::min(result.bounding_box.min, v);
        result.bounding_box.max = linalg::max(result.bounding_box.max, v);
    }
};

/// @brief Map microstl result codes to our error enum.
static Error map_microstl_result(microstl::Result r) {
    switch (r) {
        case microstl::Result::Success:          return Error::NONE;
        case microstl::Result::FileError:        return Error::FILE_NOT_FOUND;
        case microstl::Result::MissingDataError: return Error::INVALID_STL;
        case microstl::Result::UnexpectedError:  return Error::INVALID_STL;
        case microstl::Result::ParserError:      return Error::PARSE_ERROR;
        case microstl::Result::LineLimitError:   return Error::PARSE_ERROR;
        case microstl::Result::FacetCountError:  return Error::FACET_COUNT_EXCEEDED;
        case microstl::Result::EndianError:      return Error::INVALID_STL;
        default:                                 return Error::PARSE_ERROR;
    }
}

/// @brief Map microstl result codes to a human-readable message.
static const char* microstl_result_message(microstl::Result r) {
    switch (r) {
        case microstl::Result::Success:          return "success";
        case microstl::Result::FileError:        return "unable to read file";
        case microstl::Result::MissingDataError: return "STL data is incomplete or truncated";
        case microstl::Result::UnexpectedError:  return "unexpected token in ASCII STL";
        case microstl::Result::ParserError:      return "failed to parse vertex/normal coordinates";
        case microstl::Result::LineLimitError:   return "ASCII line exceeds safety limit";
        case microstl::Result::FacetCountError:  return "binary facet count exceeds safety limit";
        case microstl::Result::EndianError:      return "unsupported endianness";
        default:                                 return "unknown parse error";
    }
}

ParseResult parse_file(const std::string& path, ProgressCallback progress_cb) {
    ParseResult out;

    mapped_file::MappedFile mf = mapped_file::map_file(path.c_str());
    if (!mf.ok()) {
        if (mf.error == mapped_file::Error::EMPTY_FILE)
            out.error = Error::EMPTY_FILE;
        else if (mf.error == mapped_file::Error::MMAP_FAILED)
            out.error = Error::MMAP_FAILED;
        else
            out.error = Error::FILE_NOT_FOUND;
        out.error_message = mapped_file::error_string(mf.error);
        return out;
    }

    MeshBuilder builder;
    builder.progress_cb = progress_cb;

    microstl::Result mr = microstl::Reader::readStlBuffer(mf.data, mf.size, builder);

    if (mr != microstl::Result::Success) {
        out.error = map_microstl_result(mr);
        out.error_message = microstl_result_message(mr);
        mapped_file::unmap_file(mf);
        return out;
    }

    out.mesh = std::move(builder.result);
    out.mesh.source_path = path;
    out.mesh.file_size_bytes = mf.size;

    mapped_file::unmap_file(mf);

    if (progress_cb) {
        progress_cb(out.mesh.triangle_count, out.mesh.triangle_count);
    }

    return out;
}

const char* error_string(Error err) {
    switch (err) {
        case Error::NONE:                 return "no error";
        case Error::FILE_NOT_FOUND:       return "file not found";
        case Error::EMPTY_FILE:           return "file is empty";
        case Error::MMAP_FAILED:          return "failed to memory-map file";
        case Error::INVALID_STL:          return "invalid STL file";
        case Error::PARSE_ERROR:          return "STL parse error";
        case Error::FACET_COUNT_EXCEEDED: return "facet count exceeds safety limit";
    }
    return "unknown error";
}

} // namespace stl_parser
