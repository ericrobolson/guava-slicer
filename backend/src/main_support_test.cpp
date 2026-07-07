/// @file main_support_test.cpp
/// @brief Headless CLI for support generation testing.
/// Usage: support-test <input.stl> <output-supports.stl> [output-combined.stl] [--tilt <degrees>]
#include "island_detection.h"
#include "linalg_types.h"
#include "mesh_raycaster.h"
#include "overhang.h"
#include "slicer.h"
#include "stl_parser.h"
#include "stl_writer.h"
#include "support_gen.h"
#include "support_types.h"

#include <chrono>
#include <cmath>
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

static linalg_types::Mat4 rotation_x(float deg) {
    float rad = deg * 3.14159265f / 180.0f;
    float c = std::cos(rad), s = std::sin(rad);
    linalg_types::Mat4 r;
    r[0] = {1, 0, 0, 0};
    r[1] = {0, c, s, 0};
    r[2] = {0, -s, c, 0};
    r[3] = {0, 0, 0, 1};
    return r;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <input.stl> <output-supports.stl> [output-combined.stl] [--tilt <deg>]\n", argv[0]);
        return 1;
    }

    const char* input_path = argv[1];
    const char* support_path = argv[2];
    const char* combined_path = nullptr;
    float tilt_deg = 0.0f;

    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "--tilt" && i + 1 < argc) {
            tilt_deg = static_cast<float>(std::atof(argv[++i]));
        } else if (!combined_path) {
            combined_path = argv[i];
        }
    }

    auto t0 = std::chrono::steady_clock::now();

    std::printf("Loading %s...\n", input_path);
    auto result = stl_parser::parse_file(input_path);
    if (!result.ok()) {
        std::fprintf(stderr, "ERROR: %s — %s\n", stl_parser::error_string(result.error), result.error_message.c_str());
        return 1;
    }
    auto& m = result.mesh;
    std::printf("  %u vertices, %u triangles\n", m.vertex_count, m.triangle_count);
    std::printf("  bbox: (%.1f,%.1f,%.1f) -> (%.1f,%.1f,%.1f)\n",
        m.bounding_box.min.x, m.bounding_box.min.y, m.bounding_box.min.z,
        m.bounding_box.max.x, m.bounding_box.max.y, m.bounding_box.max.z);

    auto transform = identity_mat4();
    if (std::abs(tilt_deg) > 0.01f) {
        transform = linalg::mul(rotation_x(tilt_deg), transform);
        std::printf("  tilted X by %.1f deg\n", tilt_deg);
    }

    // Place on build plate
    float min_y = 1e30f;
    for (uint32_t i = 0; i < m.vertex_count; ++i) {
        float x = m.vertices[i*3], y = m.vertices[i*3+1], z = m.vertices[i*3+2];
        float ty = transform[0][1]*x + transform[1][1]*y + transform[2][1]*z + transform[3][1];
        if (ty < min_y) min_y = ty;
    }
    auto plate = identity_mat4();
    plate[3][1] = -min_y;
    transform = linalg::mul(plate, transform);
    std::printf("  placed on plate: Y offset = %.2f\n", -min_y);

    std::printf("Analyzing overhangs (%.0f deg)...\n", overhang::DEFAULT_THRESHOLD_DEG);
    auto oh = overhang::analyze(m.vertices.data(), m.indices.data(),
        m.vertex_count, m.triangle_count, transform, overhang::DEFAULT_THRESHOLD_DEG);
    std::printf("  %zu overhang tris (%.1f mm2 / %.1f mm2)\n",
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
        std::printf("  island: %zu\n", pts.size());
        coll.points.insert(coll.points.end(), pts.begin(), pts.end());
    }
    if (sp.enabled_categories & support::CATEGORY_BIT_OVERHANG) {
        auto pts = support_gen::sample_overhang_points(oh,
            m.vertices.data(), m.indices.data(), m.vertex_count, transform,
            sp.spacing, coll.next_id);
        std::printf("  overhang: %zu\n", pts.size());
        coll.points.insert(coll.points.end(), pts.begin(), pts.end());
    }

    std::printf("Pre-dedup: %zu\n", coll.points.size());
    support_gen::deduplicate_points(coll.points, sp.spacing);
    std::printf("Post-dedup: %zu\n", coll.points.size());

    coll.params = sp;

    std::printf("Building BVH (%u tris)...\n", m.triangle_count);
    raycaster::MeshRaycaster rc;
    rc.build(m.vertices.data(), m.indices.data(), m.vertex_count, m.triangle_count, transform);

    std::printf("Generating supports...\n");
    support_gen::rebuild_mesh(coll, &rc);

    uint32_t support_tris = static_cast<uint32_t>(coll.mesh_indices.size() / 3);
    uint32_t support_verts = static_cast<uint32_t>(coll.mesh_vertices.size() / 3);
    std::printf("  mesh: %u verts, %u tris\n", support_verts, support_tris);

    std::printf("Writing %s...\n", support_path);
    stl_writer::write_stl(support_path, coll.mesh_vertices.data(), coll.mesh_indices.data(), support_tris);

    if (combined_path) {
        std::vector<float> wv(m.vertices.size());
        for (uint32_t i = 0; i < m.vertex_count; ++i) {
            float x = m.vertices[i*3], y = m.vertices[i*3+1], z = m.vertices[i*3+2];
            wv[i*3+0] = transform[0][0]*x + transform[1][0]*y + transform[2][0]*z + transform[3][0];
            wv[i*3+1] = transform[0][1]*x + transform[1][1]*y + transform[2][1]*z + transform[3][1];
            wv[i*3+2] = transform[0][2]*x + transform[1][2]*y + transform[2][2]*z + transform[3][2];
        }
        std::printf("Writing %s...\n", combined_path);
        stl_writer::write_combined_stl(combined_path, wv.data(), m.indices.data(), m.triangle_count,
            coll.mesh_vertices.data(), coll.mesh_indices.data(), support_tris);
    }

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::printf("Done in %lldms\n", static_cast<long long>(ms));
    return 0;
}
