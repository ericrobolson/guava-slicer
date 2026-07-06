/// @file slicer_commands.cpp
/// @brief IPC command handlers for slicing — slice and get_layer.
#include "slicer_commands.h"
#include "app_state.h"
#include "ipc.h"
#include "slicer.h"
#include "transform.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

namespace slicer_commands {

static std::atomic<bool> s_slice_cancel{false};
static std::mutex s_result_mutex;
static slicer::SliceResult s_cached_result;
static uint64_t s_cached_mesh_hash = 0;
static float s_cached_layer_height = 0.0f;
static bool s_has_result = false;
static std::atomic<bool> s_slicing_active{false};

/// @brief Compute a simple hash of the mesh state for cache invalidation.
///
/// Combines vertex/triangle counts with all 16 floats of the composite
/// transform matrix using FNV-1a.
static uint64_t compute_mesh_hash(const app_state::AppState& state) {
    constexpr uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
    constexpr uint64_t FNV_PRIME = 0x100000001b3ULL;

    uint64_t h = FNV_OFFSET;
    const auto& m = state.mesh;
    h ^= static_cast<uint64_t>(m.vertex_count);
    h *= FNV_PRIME;
    h ^= static_cast<uint64_t>(m.triangle_count);
    h *= FNV_PRIME;

    linalg_types::Mat4 mat = state.transforms.composite_matrix();
    constexpr int MAT4_FLOATS = 16;
    float floats[MAT4_FLOATS];
    std::memcpy(floats, &mat, sizeof(floats));
    for (int i = 0; i < MAT4_FLOATS; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &floats[i], sizeof(bits));
        h ^= bits;
        h *= FNV_PRIME;
    }
    return h;
}

/// @brief Handle the "slice" command — run slicing on a background thread.
static void handle_slice(const std::string& id, const ipc::json& params) {
    auto& state = app_state::get();
    if (!state.has_mesh) {
        ipc::send_error(id, "NO_MESH", "no mesh loaded");
        return;
    }

    float layer_height = slicer::DEFAULT_LAYER_HEIGHT;
    if (params.contains("layer_height") && params["layer_height"].is_number()) {
        layer_height = params["layer_height"].get<float>();
        if (layer_height < slicer::MIN_LAYER_HEIGHT || layer_height > slicer::MAX_LAYER_HEIGHT) {
            ipc::send_error(id, "INVALID_PARAMS",
                "layer_height must be between " +
                std::to_string(slicer::MIN_LAYER_HEIGHT) + " and " +
                std::to_string(slicer::MAX_LAYER_HEIGHT));
            return;
        }
    }

    uint64_t mesh_hash = compute_mesh_hash(state);

    {
        std::lock_guard<std::mutex> lock(s_result_mutex);
        if (s_has_result && s_cached_mesh_hash == mesh_hash && s_cached_layer_height == layer_height) {
            ipc::send_ok(id, {
                {"layer_count", s_cached_result.layer_count},
                {"layer_height", s_cached_result.layer_height},
                {"warning_count", s_cached_result.warning_count},
                {"cached", true},
            });
            return;
        }
    }

    s_slice_cancel.store(true, std::memory_order_relaxed);

    const auto& mesh = state.mesh;
    std::vector<float> verts_copy(mesh.vertices);
    std::vector<uint32_t> idx_copy(mesh.indices);
    uint32_t vert_count = mesh.vertex_count;
    uint32_t tri_count = mesh.triangle_count;
    linalg_types::Mat4 xform = state.transforms.composite_matrix();

    std::thread worker([id, layer_height, mesh_hash, verts_copy = std::move(verts_copy),
                        idx_copy = std::move(idx_copy), vert_count, tri_count, xform]() {
        s_slice_cancel.store(false, std::memory_order_relaxed);
        s_slicing_active = true;

        auto result = slicer::slice_mesh(
            verts_copy.data(),
            idx_copy.data(),
            vert_count,
            tri_count,
            xform,
            layer_height,
            [&id](uint32_t current, uint32_t total) {
                ipc::send_progress(id, {{"layer", current}, {"total", total}});
            },
            &s_slice_cancel);

        s_slicing_active = false;

        if (s_slice_cancel.load(std::memory_order_relaxed)) {
            ipc::send_error(id, "CANCELLED", "slice cancelled");
            return;
        }

        std::lock_guard<std::mutex> lock(s_result_mutex);
        s_cached_result = std::move(result);
        s_cached_mesh_hash = mesh_hash;
        s_cached_layer_height = layer_height;
        s_has_result = true;

        ipc::send_ok(id, {
            {"layer_count", s_cached_result.layer_count},
            {"layer_height", s_cached_result.layer_height},
            {"warning_count", s_cached_result.warning_count},
        });
    });
    worker.detach();
}

/// @brief Handle the "get_layer" command — return contour data for a single layer.
static void handle_get_layer(const std::string& id, const ipc::json& params) {
    if (!params.contains("layer_index") || !params["layer_index"].is_number_integer()) {
        ipc::send_error(id, "INVALID_PARAMS", "missing or invalid 'layer_index' parameter");
        return;
    }

    int layer_index = params["layer_index"].get<int>();

    std::lock_guard<std::mutex> lock(s_result_mutex);
    if (!s_has_result) {
        ipc::send_error(id, "NO_SLICE", "no slice result available — run 'slice' first");
        return;
    }

    if (layer_index < 0 || static_cast<uint32_t>(layer_index) >= s_cached_result.layer_count) {
        ipc::send_error(id, "INVALID_PARAMS",
            "layer_index " + std::to_string(layer_index) +
            " out of range [0, " + std::to_string(s_cached_result.layer_count) + ")");
        return;
    }

    const auto& layer = s_cached_result.layers[static_cast<uint32_t>(layer_index)];

    ipc::json contours_json = ipc::json::array();
    for (const auto& contour : layer.contours) {
        ipc::json points_json = ipc::json::array();
        for (const auto& pt : contour.points) {
            points_json.push_back({pt.x, pt.y});
        }
        contours_json.push_back({
            {"points", points_json},
            {"is_hole", contour.is_hole},
        });
    }

    ipc::send_ok(id, {
        {"z_height", layer.z_height},
        {"contours", contours_json},
        {"contour_count", layer.contours.size()},
    });
}

void with_cached_result(
    const std::function<void(const slicer::SliceResult& result, bool has_result)>& fn) {
    std::lock_guard<std::mutex> lock(s_result_mutex);
    fn(s_cached_result, s_has_result);
}

void register_all() {
    ipc::register_command("slice", handle_slice);
    ipc::register_command("get_layer", handle_get_layer);
}

} // namespace slicer_commands
