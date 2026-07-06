/// @file island_commands.cpp
/// @brief IPC command handlers for island detection — detect_islands, get_island_layer, cancel.
#include "island_commands.h"
#include "ipc.h"
#include "island_detection.h"
#include "slicer_commands.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace island_commands {

static std::atomic<bool> s_cancel{false};
static std::mutex s_result_mutex;
static island_detection::IslandResult s_cached_result;
static bool s_has_result = false;

/// @brief Handle the "detect_islands" command — run detection on a background thread.
static void handle_detect_islands(const std::string& id, const ipc::json& /*params*/) {
    s_cancel.store(true, std::memory_order_relaxed);

    std::thread worker([id]() {
        s_cancel.store(false, std::memory_order_relaxed);

        slicer::SliceResult slice_copy;
        bool has_slice = false;

        slicer_commands::with_cached_result([&](const slicer::SliceResult& result, bool has) {
            has_slice = has;
            if (has) {
                slice_copy = result;
            }
        });

        if (!has_slice) {
            ipc::send_error(id, "NO_SLICE", "no slice result available — run 'slice' first");
            return;
        }

        auto progress_cb = [&id](uint32_t current, uint32_t total) {
            ipc::send_progress(id, {{"layer", current}, {"total", total}});
        };

        auto result = island_detection::detect(slice_copy, progress_cb, &s_cancel);

        if (s_cancel.load(std::memory_order_relaxed)) {
            ipc::send_error(id, "CANCELLED", "island detection cancelled");
            return;
        }

        ipc::json scores_json(result.severity_scores);

        uint32_t total_count = result.total_island_count;
        uint32_t worst_idx = result.worst_layer_index;
        float max_sev = result.max_severity;
        size_t island_layer_count = result.layers.size();

        {
            std::lock_guard<std::mutex> lock(s_result_mutex);
            s_cached_result = std::move(result);
            s_has_result = true;
        }

        ipc::send_ok(id, {
            {"total_island_count", total_count},
            {"worst_layer_index", worst_idx},
            {"max_severity", max_sev},
            {"severity_scores", scores_json},
            {"island_layer_count", island_layer_count},
        });
    });
    worker.detach();
}

/// @brief Handle the "get_island_layer" command — return island data for a single layer.
static void handle_get_island_layer(const std::string& id, const ipc::json& params) {
    if (!params.contains("layer_index") || !params["layer_index"].is_number_integer()) {
        ipc::send_error(id, "INVALID_PARAMS", "missing or invalid 'layer_index' parameter");
        return;
    }

    int layer_index = params["layer_index"].get<int>();
    if (layer_index < 0) {
        ipc::send_error(id, "INVALID_PARAMS", "layer_index must be non-negative");
        return;
    }

    std::lock_guard<std::mutex> lock(s_result_mutex);
    if (!s_has_result) {
        ipc::send_error(id, "NO_ISLANDS", "no island detection result available");
        return;
    }

    for (const auto& il : s_cached_result.layers) {
        if (il.layer_index == static_cast<uint32_t>(layer_index)) {
            ipc::json islands_json = ipc::json::array();
            for (const auto& ic : il.islands) {
                islands_json.push_back({
                    {"contour_index", ic.contour_index},
                    {"area", ic.area},
                });
            }

            ipc::send_ok(id, {
                {"layer_index", il.layer_index},
                {"island_count", il.islands.size()},
                {"total_island_area", il.total_island_area},
                {"severity", il.severity},
                {"islands", islands_json},
            });
            return;
        }
    }

    ipc::send_ok(id, {
        {"layer_index", layer_index},
        {"island_count", 0},
        {"total_island_area", 0.0},
        {"severity", 0.0},
        {"islands", ipc::json::array()},
    });
}

/// @brief Handle the "cancel_island_detection" command.
static void handle_cancel(const std::string& id, const ipc::json& /*params*/) {
    s_cancel.store(true, std::memory_order_relaxed);
    ipc::send_ok(id, {{"cancelled", true}});
}

void register_all() {
    ipc::register_command("detect_islands", handle_detect_islands);
    ipc::register_command("get_island_layer", handle_get_island_layer);
    ipc::register_command("cancel_island_detection", handle_cancel);
}

} // namespace island_commands
