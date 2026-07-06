/// @file support_commands.cpp
/// @brief IPC command handlers for support generation — generate, place, remove, clear.
#include "support_commands.h"
#include "app_state.h"
#include "ipc.h"
#include "island_detection.h"
#include "mesh_raycaster.h"
#include "overhang.h"
#include "slicer_commands.h"
#include "support_gen.h"
#include "support_types.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace support_commands {

/// @brief Cancellation flag for auto-generate.
static std::atomic<bool> s_cancel{false};

/// @brief Build JSON summary of support collection state.
static ipc::json support_summary(const support::SupportCollection& coll) {
    return {
        {"total_count", coll.points.size()},
        {"island_count", coll.count_category(support::Category::ISLAND)},
        {"reinforcement_count", coll.count_category(support::Category::REINFORCEMENT)},
        {"overhang_count", coll.count_category(support::Category::OVERHANG)},
        {"stabilization_count", coll.count_category(support::Category::STABILIZATION)},
        {"vertex_count", coll.mesh_vertices.size() / 3},
        {"triangle_count", coll.mesh_indices.size() / 3},
    };
}

/// @brief Send support mesh geometry as binary frames (vertices, normals, indices).
static void send_support_binary(const support::SupportCollection& coll) {
    auto send_floats = [](const std::vector<float>& vec) {
        ipc::send_binary(
            reinterpret_cast<const uint8_t*>(vec.data()),
            static_cast<uint32_t>(vec.size() * sizeof(float)));
    };
    send_floats(coll.mesh_vertices);
    send_floats(coll.mesh_normals);
    ipc::send_binary(
        reinterpret_cast<const uint8_t*>(coll.mesh_indices.data()),
        static_cast<uint32_t>(coll.mesh_indices.size() * sizeof(uint32_t)));
}

/// @brief Parse SupportParams from IPC JSON params.
static support::SupportParams parse_support_params(const ipc::json& params) {
    support::SupportParams sp;
    if (params.contains("tip_diameter") && params["tip_diameter"].is_number())
        sp.tip_diameter = params["tip_diameter"].get<float>();
    if (params.contains("tip_penetration") && params["tip_penetration"].is_number())
        sp.tip_penetration = params["tip_penetration"].get<float>();
    if (params.contains("shaft_diameter") && params["shaft_diameter"].is_number())
        sp.shaft_diameter = params["shaft_diameter"].get<float>();
    if (params.contains("base_diameter") && params["base_diameter"].is_number())
        sp.base_diameter = params["base_diameter"].get<float>();
    if (params.contains("base_height") && params["base_height"].is_number())
        sp.base_height = params["base_height"].get<float>();
    if (params.contains("spacing") && params["spacing"].is_number())
        sp.spacing = params["spacing"].get<float>();
    if (params.contains("tip_end_diameter") && params["tip_end_diameter"].is_number())
        sp.tip_end_diameter = params["tip_end_diameter"].get<float>();
    if (params.contains("raycast_margin") && params["raycast_margin"].is_number())
        sp.raycast_margin = params["raycast_margin"].get<float>();
    if (params.contains("enabled_categories") && params["enabled_categories"].is_number_unsigned())
        sp.enabled_categories = static_cast<uint8_t>(params["enabled_categories"].get<uint32_t>());
    return sp;
}

/// @brief Command that replaces the full support collection (used for auto-generate).
struct GenerateSupportsCommand : command::Command {
    support::SupportCollection new_supports;
    support::SupportCollection previous_supports;
    app_state::AppState& state;

    GenerateSupportsCommand(support::SupportCollection s, app_state::AppState& st)
        : new_supports(std::move(s)), state(st) {}

    void execute() override {
        previous_supports = std::move(state.supports);
        state.supports = std::move(new_supports);
    }

    void undo() override {
        new_supports = std::move(state.supports);
        state.supports = std::move(previous_supports);
    }

    std::string name() const override { return "Generate supports"; }
};

/// @brief Command that adds a single support point.
struct PlaceSupportCommand : command::Command {
    support::SupportPoint point;
    app_state::AppState& state;

    PlaceSupportCommand(support::SupportPoint p, app_state::AppState& st)
        : point(p), state(st) {}

    void execute() override {
        state.supports.points.push_back(point);
        support_gen::rebuild_mesh(state.supports);
    }

    void undo() override {
        state.supports.points.pop_back();
        support_gen::rebuild_mesh(state.supports);
    }

    std::string name() const override { return "Place support"; }
};

/// @brief Command that removes a single support point by ID.
struct RemoveSupportCommand : command::Command {
    uint32_t target_id;
    support::SupportPoint removed_point;
    size_t removed_index;
    app_state::AppState& state;

    RemoveSupportCommand(uint32_t id, app_state::AppState& st)
        : target_id(id), removed_index(0), state(st) {}

    void execute() override {
        auto& pts = state.supports.points;
        for (size_t i = 0; i < pts.size(); ++i) {
            if (pts[i].id == target_id) {
                removed_point = pts[i];
                removed_index = i;
                pts.erase(pts.begin() + static_cast<long>(i));
                support_gen::rebuild_mesh(state.supports);
                return;
            }
        }
    }

    void undo() override {
        auto& pts = state.supports.points;
        pts.insert(pts.begin() + static_cast<long>(removed_index), removed_point);
        support_gen::rebuild_mesh(state.supports);
    }

    std::string name() const override { return "Remove support"; }
};

/// @brief Command that clears all supports.
struct ClearSupportsCommand : command::Command {
    support::SupportCollection previous;
    app_state::AppState& state;

    explicit ClearSupportsCommand(app_state::AppState& st) : state(st) {}

    void execute() override {
        previous = std::move(state.supports);
        state.supports.clear();
    }

    void undo() override {
        state.supports = std::move(previous);
    }

    std::string name() const override { return "Clear supports"; }
};

/// @brief Handle auto-generate supports — runs on a background thread.
static void handle_generate_supports(const std::string& id, const ipc::json& params) {
    if (!app_state::get().has_mesh) {
        ipc::send_error(id, "NO_MESH", "no mesh loaded");
        return;
    }

    support::SupportParams sp = parse_support_params(params);

    float threshold = overhang::DEFAULT_THRESHOLD_DEG;
    if (params.contains("threshold") && params["threshold"].is_number())
        threshold = params["threshold"].get<float>();

    s_cancel.store(false, std::memory_order_relaxed);

    std::thread worker([id, sp, threshold]() {
        auto& state = app_state::get();
        const auto& m = state.mesh;
        auto transform = state.transforms.composite_matrix();

        support::SupportCollection new_coll;
        new_coll.params = sp;

        ipc::send_progress(id, {{"phase", "analyzing"}, {"step", 0}, {"total", 4}});

        auto overhang_result = overhang::analyze(
            m.vertices.data(), m.indices.data(),
            m.vertex_count, m.triangle_count,
            transform, threshold);

        if (s_cancel.load(std::memory_order_relaxed)) {
            ipc::send_error(id, "CANCELLED", "support generation cancelled");
            return;
        }

        // Always slice fresh with current transform (cached slices may be stale after rotation)
        auto slice_copy = slicer::slice_mesh(
            m.vertices.data(), m.indices.data(),
            m.vertex_count, m.triangle_count,
            transform, slicer::DEFAULT_LAYER_HEIGHT);
        bool has_slice = slice_copy.layer_count > 0;

        island_detection::IslandResult island_result;
        bool has_islands = false;
        if (has_slice) {
            island_result = island_detection::detect(slice_copy, nullptr, nullptr);
            has_islands = true;
        }

        if (s_cancel.load(std::memory_order_relaxed)) {
            ipc::send_error(id, "CANCELLED", "support generation cancelled");
            return;
        }

        ipc::log_debug("generate_supports: has_slice=" + std::to_string(has_slice) +
                       " has_islands=" + std::to_string(has_islands) +
                       " total_islands=" + std::to_string(has_islands ? island_result.total_island_count : 0) +
                       " overhang_faces=" + std::to_string(overhang_result.triangle_indices.size()));

        ipc::send_progress(id, {{"phase", "sampling"}, {"step", 1}, {"total", 4}});

        if (has_islands && (sp.enabled_categories & support::CATEGORY_BIT_ISLAND)) {
            auto pts = support_gen::sample_island_points(
                island_result, slice_copy,
                m.vertices.data(), m.indices.data(),
                m.vertex_count, m.triangle_count,
                transform, new_coll.next_id);
            new_coll.points.insert(new_coll.points.end(), pts.begin(), pts.end());
        }

        if (has_islands && (sp.enabled_categories & support::CATEGORY_BIT_REINFORCEMENT)) {
            auto pts = support_gen::sample_reinforcement_points(
                island_result, slice_copy, new_coll.next_id);
            new_coll.points.insert(new_coll.points.end(), pts.begin(), pts.end());
        }

        if (sp.enabled_categories & support::CATEGORY_BIT_OVERHANG) {
            auto pts = support_gen::sample_overhang_points(
                overhang_result,
                m.vertices.data(), m.indices.data(),
                m.vertex_count, transform, sp.spacing, new_coll.next_id);
            new_coll.points.insert(new_coll.points.end(), pts.begin(), pts.end());
        }

        if (sp.enabled_categories & support::CATEGORY_BIT_STABILIZATION) {
            auto pts = support_gen::sample_stabilization_points(
                m.vertices.data(), m.indices.data(),
                m.vertex_count, m.triangle_count,
                transform, sp.spacing, new_coll.next_id);
            new_coll.points.insert(new_coll.points.end(), pts.begin(), pts.end());
        }

        if (s_cancel.load(std::memory_order_relaxed)) {
            ipc::send_error(id, "CANCELLED", "support generation cancelled");
            return;
        }

        size_t pre_dedup = new_coll.points.size();
        support_gen::deduplicate_points(new_coll.points, sp.spacing);
        ipc::log_debug("generate_supports: pre_dedup=" + std::to_string(pre_dedup) +
                       " post_dedup=" + std::to_string(new_coll.points.size()) +
                       " spacing=" + std::to_string(sp.spacing));

        support_gen::adjust_bases_for_mesh_avoidance(
            new_coll.points,
            m.vertices.data(), m.vertex_count, transform);

        uint32_t angled_count = 0;
        for (const auto& p : new_coll.points) {
            float dx = p.ground_pos.x - p.position.x;
            float dz = p.ground_pos.z - p.position.z;
            if (dx * dx + dz * dz > 0.01f) ++angled_count;
        }
        ipc::log_debug("generate_supports: angled=" + std::to_string(angled_count) +
                       "/" + std::to_string(new_coll.points.size()));

        for (size_t i = 0; i < std::min(new_coll.points.size(), size_t(5)); ++i) {
            const auto& p = new_coll.points[i];
            ipc::log_debug("  support[" + std::to_string(i) + "]: pos=(" +
                std::to_string(p.position.x) + "," + std::to_string(p.position.y) + "," +
                std::to_string(p.position.z) + ") base=(" +
                std::to_string(p.ground_pos.x) + ",0," + std::to_string(p.ground_pos.z) +
                ") cat=" + support::category_name(p.category));
        }

        ipc::send_progress(id, {{"phase", "building_raycaster"}, {"step", 2}, {"total", 6}});

        raycaster::MeshRaycaster rc;
        rc.build(m.vertices.data(), m.indices.data(), m.vertex_count, m.triangle_count, transform);

        ipc::send_progress(id, {{"phase", "snapping_contacts"}, {"step", 3}, {"total", 6}});

        support_gen::snap_contacts_to_surface(new_coll.points, rc);

        ipc::send_progress(id, {{"phase", "generating_mesh"}, {"step", 4}, {"total", 6}});

        support_gen::rebuild_mesh(new_coll, &rc);

        ipc::send_progress(id, {{"phase", "complete"}, {"step", 5}, {"total", 6}});

        auto cmd = std::make_unique<GenerateSupportsCommand>(std::move(new_coll), state);
        state.commands.push(std::move(cmd));

        if (!state.supports.mesh_vertices.empty()) {
            send_support_binary(state.supports);
        }

        auto resp = app_state::transform_response(state);
        resp["supports"] = support_summary(state.supports);
        resp["binary_follows"] = !state.supports.mesh_vertices.empty();
        ipc::send_ok(id, resp);
        ipc::log_debug("generate_supports: complete, " +
                       std::to_string(state.supports.points.size()) + " supports");
    });
    worker.detach();
}

/// @brief Handle place_support — add a single support at a given position.
static void handle_place_support(const std::string& id, const ipc::json& params) {
    if (!app_state::get().has_mesh) {
        ipc::send_error(id, "NO_MESH", "no mesh loaded");
        return;
    }

    if (!params.contains("position") || !params["position"].is_array() ||
        params["position"].size() != 3) {
        ipc::send_error(id, "INVALID_PARAMS", "missing 'position' as [x,y,z]");
        return;
    }

    auto& state = app_state::get();

    support::SupportPoint sp;
    sp.position = {
        params["position"][0].get<float>(),
        params["position"][1].get<float>(),
        params["position"][2].get<float>(),
    };

    if (params.contains("normal") && params["normal"].is_array() && params["normal"].size() == 3) {
        sp.normal = {
            params["normal"][0].get<float>(),
            params["normal"][1].get<float>(),
            params["normal"][2].get<float>(),
        };
    } else {
        sp.normal = {0, -1, 0};
    }

    sp.ground_pos = {sp.position.x, 0, sp.position.z};
    sp.category = support::Category::ISLAND;
    sp.id = state.supports.next_id++;

    auto cmd = std::make_unique<PlaceSupportCommand>(sp, state);
    state.commands.push(std::move(cmd));

    if (!state.supports.mesh_vertices.empty()) {
        send_support_binary(state.supports);
    }

    auto resp = app_state::transform_response(state);
    resp["supports"] = support_summary(state.supports);
    resp["placed_id"] = sp.id;
    resp["binary_follows"] = !state.supports.mesh_vertices.empty();
    ipc::send_ok(id, resp);
}

/// @brief Handle remove_support — remove a support by ID.
static void handle_remove_support(const std::string& id, const ipc::json& params) {
    if (!params.contains("support_id") || !params["support_id"].is_number_unsigned()) {
        ipc::send_error(id, "INVALID_PARAMS", "missing 'support_id' parameter");
        return;
    }

    auto& state = app_state::get();
    uint32_t target_id = params["support_id"].get<uint32_t>();

    bool found = false;
    for (const auto& p : state.supports.points) {
        if (p.id == target_id) { found = true; break; }
    }

    if (!found) {
        ipc::send_error(id, "NOT_FOUND", "support with given ID not found");
        return;
    }

    auto cmd = std::make_unique<RemoveSupportCommand>(target_id, state);
    state.commands.push(std::move(cmd));

    if (!state.supports.mesh_vertices.empty()) {
        send_support_binary(state.supports);
    }

    auto resp = app_state::transform_response(state);
    resp["supports"] = support_summary(state.supports);
    resp["binary_follows"] = !state.supports.mesh_vertices.empty();
    ipc::send_ok(id, resp);
}

/// @brief Handle clear_supports — remove all supports.
static void handle_clear_supports(const std::string& id, const ipc::json& /*params*/) {
    auto& state = app_state::get();

    if (state.supports.points.empty()) {
        ipc::send_ok(id, {{"supports", support_summary(state.supports)}});
        return;
    }

    auto cmd = std::make_unique<ClearSupportsCommand>(state);
    state.commands.push(std::move(cmd));

    auto resp = app_state::transform_response(state);
    resp["supports"] = support_summary(state.supports);
    resp["binary_follows"] = false;
    ipc::send_ok(id, resp);
}

/// @brief Handle cancel_supports — cancel running generation.
static void handle_cancel_supports(const std::string& id, const ipc::json& /*params*/) {
    s_cancel.store(true, std::memory_order_relaxed);
    ipc::send_ok(id, {{"cancelled", true}});
}

/// @brief Handle get_support_points — return all support point positions and IDs.
static void handle_get_support_points(const std::string& id, const ipc::json& /*params*/) {
    auto& state = app_state::get();
    ipc::json points_json = ipc::json::array();

    for (const auto& p : state.supports.points) {
        points_json.push_back({
            {"id", p.id},
            {"position", {p.position.x, p.position.y, p.position.z}},
            {"category", support::category_name(p.category)},
        });
    }

    ipc::send_ok(id, {
        {"points", points_json},
        {"supports", support_summary(state.supports)},
    });
}

void register_all() {
    ipc::register_command("generate_supports", handle_generate_supports);
    ipc::register_command("place_support", handle_place_support);
    ipc::register_command("remove_support", handle_remove_support);
    ipc::register_command("clear_supports", handle_clear_supports);
    ipc::register_command("cancel_supports", handle_cancel_supports);
    ipc::register_command("get_support_points", handle_get_support_points);
}

} // namespace support_commands
