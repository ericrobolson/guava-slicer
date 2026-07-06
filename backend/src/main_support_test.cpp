/// @file main_support_test.cpp
/// @brief Headless CLI for support generation testing.
/// Usage: support-test <input.stl> <output-supports.stl> [output-combined.stl]
#include "island_detection.h"
#include "linalg_types.h"
#include "overhang.h"
#include "slicer.h"
#include "stl_parser.h"
#include "stl_writer.h"
#include "support_gen.h"
#include "support_types.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static linalg_types::Mat4 identity_mat4() {
    linalg_types::Mat4 m;
    m[0] = {1, 0, 0, 0};
    m[1] = {0, 1, 0, 0};
    m[2] = {0, 0, 1, 0};
    m[3] = {0, 0, 0, 1};
    return m;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <input.stl> <output-supports.stl> [output-combined.stl]\n", argv[0]);
        return 1;
    }

    const char* input_path = argv[1];
    const char* support_path = argv[2];
    const char* combined_path = argc > 3 ? argv[3] : nullptr;

    auto t0 = std::chrono::steady_clock::now();

    std::printf("Loading %s...\n", input_path);
    auto result = stl_parser::parse_file(input_path);
    if (!result.ok()) {
        std::fprintf(stderr, "ERROR: %s — %s\n", stl_parser::error_string(result.error), result.error_message.c_str());
        return 1;
    }
    auto& m = result.mesh;
    std::printf("  %u vertices, %u triangles\n", m.vertex_count, m.triangle_count);
    std::printf("  bbox: (%.1f,%.1f,%.1f) → (%.1f,%.1f,%.1f)\n",
        m.bounding_box.min.x, m.bounding_box.min.y, m.bounding_box.min.z,
        m.bounding_box.max.x, m.bounding_box.max.y, m.bounding_box.max.z);

    auto transform = identity_mat4();

    std::printf("Analyzing overhangs (%.0f°)...\n", overhang::DEFAULT_THRESHOLD_DEG);
    auto oh = overhang::analyze(m.vertices.data(), m.indices.data(),
        m.vertex_count, m.triangle_count, transform, overhang::DEFAULT_THRESHOLD_DEG);
    std::printf("  %zu overhang triangles (%.1f mm² / %.1f mm² total)\n",
        oh.triangle_indices.size(), oh.overhang_area, oh.total_area);

    std::printf("Slicing (%.3fmm layers)...\n", slicer::DEFAULT_LAYER_HEIGHT);
    auto slices = slicer::slice_mesh(m.vertices.data(), m.indices.data(),
        m.vertex_count, m.triangle_count, transform, slicer::DEFAULT_LAYER_HEIGHT);
    std::printf("  %u layers\n", slices.layer_count);

    std::printf("Detecting islands...\n");
    auto islands = island_detection::detect(slices, nullptr, nullptr);
    std::printf("  %u total islands across %zu layers\n",
        islands.total_island_count, islands.layers.size());

    support::SupportCollection coll;
    support::SupportParams sp;

    std::printf("Sampling support points...\n");
    if (sp.enabled_categories & support::CATEGORY_BIT_ISLAND) {
        auto pts = support_gen::sample_island_points(islands, slices,
            m.vertices.data(), m.indices.data(), m.vertex_count, m.triangle_count,
            transform, coll.next_id);
        std::printf("  island: %zu points\n", pts.size());
        coll.points.insert(coll.points.end(), pts.begin(), pts.end());
    }
    if (sp.enabled_categories & support::CATEGORY_BIT_REINFORCEMENT) {
        auto pts = support_gen::sample_reinforcement_points(islands, slices, coll.next_id);
        std::printf("  reinforcement: %zu points\n", pts.size());
        coll.points.insert(coll.points.end(), pts.begin(), pts.end());
    }
    if (sp.enabled_categories & support::CATEGORY_BIT_OVERHANG) {
        auto pts = support_gen::sample_overhang_points(oh,
            m.vertices.data(), m.indices.data(), m.vertex_count, transform,
            sp.spacing, coll.next_id);
        std::printf("  overhang: %zu points\n", pts.size());
        coll.points.insert(coll.points.end(), pts.begin(), pts.end());
    }
    if (sp.enabled_categories & support::CATEGORY_BIT_STABILIZATION) {
        auto pts = support_gen::sample_stabilization_points(
            m.vertices.data(), m.indices.data(), m.vertex_count, m.triangle_count,
            transform, sp.spacing, coll.next_id);
        std::printf("  stabilization: %zu points\n", pts.size());
        coll.points.insert(coll.points.end(), pts.begin(), pts.end());
    }

    std::printf("Pre-dedup: %zu points\n", coll.points.size());
    support_gen::deduplicate_points(coll.points, sp.spacing);
    std::printf("Post-dedup: %zu points\n", coll.points.size());

    support_gen::adjust_bases_for_mesh_avoidance(
        coll.points, m.vertices.data(), m.vertex_count, transform);

    coll.params = sp;
    std::printf("Generating support mesh...\n");
    support_gen::rebuild_mesh(coll);

    uint32_t support_tris = static_cast<uint32_t>(coll.mesh_indices.size() / 3);
    uint32_t support_verts = static_cast<uint32_t>(coll.mesh_vertices.size() / 3);
    std::printf("  support mesh: %u vertices, %u triangles\n", support_verts, support_tris);

    std::printf("Writing %s...\n", support_path);
    if (!stl_writer::write_stl(support_path,
            coll.mesh_vertices.data(), coll.mesh_indices.data(), support_tris)) {
        std::fprintf(stderr, "ERROR: failed to write support STL\n");
        return 1;
    }

    if (combined_path) {
        std::printf("Writing %s...\n", combined_path);
        if (!stl_writer::write_combined_stl(combined_path,
                m.vertices.data(), m.indices.data(), m.triangle_count,
                coll.mesh_vertices.data(), coll.mesh_indices.data(), support_tris)) {
            std::fprintf(stderr, "ERROR: failed to write combined STL\n");
            return 1;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::printf("Done in %lldms\n", static_cast<long long>(ms));
    return 0;
}
