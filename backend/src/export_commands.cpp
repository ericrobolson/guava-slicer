/// @file export_commands.cpp
/// @brief IPC handler for STL export.
#include "export_commands.h"

#include "app_state.h"
#include "ipc.h"
#include "stl_writer.h"

#include <nlohmann/json.hpp>

namespace export_commands {

using json = nlohmann::json;

/// @brief Handle the export_stl IPC command — write combined model+supports to a binary STL.
static void handle_export_stl(const std::string& id, const json& params) {
    auto& state = app_state::get();

    if (!state.has_mesh) {
        ipc::send_error(id, "NO_MESH", "No mesh loaded");
        return;
    }

    if (!params.contains("path") || !params["path"].is_string()) {
        ipc::send_error(id, "INVALID_REQUEST", "missing 'path' parameter");
        return;
    }

    const std::string path = params["path"].get<std::string>();
    const auto& m = state.mesh;
    const auto& sup = state.supports;
    auto mat = state.transforms.composite_matrix();

    float transform[4][4];
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            transform[col][row] = mat[col][row];

    const float* sup_verts = sup.mesh_vertices.empty() ? nullptr : sup.mesh_vertices.data();
    const uint32_t* sup_idx = sup.mesh_indices.empty() ? nullptr : sup.mesh_indices.data();
    uint32_t sup_tris = static_cast<uint32_t>(sup.mesh_indices.size() / 3);

    uint64_t file_size = stl_writer::export_combined_stl(path,
        m.vertices.data(), m.indices.data(), m.vertex_count, m.triangle_count,
        transform,
        sup_verts, sup_idx, sup_tris);

    if (file_size == 0) {
        ipc::send_error(id, "IO_ERROR", "Failed to write STL file: " + path);
        return;
    }

    uint32_t total_tris = m.triangle_count + sup_tris;

    json result;
    result["path"] = path;
    result["triangle_count"] = total_tris;
    result["file_size_bytes"] = file_size;
    ipc::send_ok(id, result);
}

void register_all() {
    ipc::register_command("export_stl", handle_export_stl);
}

} // namespace export_commands
