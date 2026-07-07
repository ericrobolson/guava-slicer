/// @file stl_writer.cpp
/// @brief Binary STL export — no microstl dependency (avoids duplicate symbol issues).
#include "stl_writer.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace stl_writer {

static void write_facet(std::FILE* f, const float* v, const uint32_t* idx, uint32_t tri) {
    uint32_t i0 = idx[tri*3+0], i1 = idx[tri*3+1], i2 = idx[tri*3+2];
    float v1[3] = {v[i0*3], v[i0*3+1], v[i0*3+2]};
    float v2[3] = {v[i1*3], v[i1*3+1], v[i1*3+2]};
    float v3[3] = {v[i2*3], v[i2*3+1], v[i2*3+2]};

    float e1[3] = {v2[0]-v1[0], v2[1]-v1[1], v2[2]-v1[2]};
    float e2[3] = {v3[0]-v1[0], v3[1]-v1[1], v3[2]-v1[2]};
    float n[3] = {
        e1[1]*e2[2] - e1[2]*e2[1],
        e1[2]*e2[0] - e1[0]*e2[2],
        e1[0]*e2[1] - e1[1]*e2[0],
    };
    float len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
    if (len > 1e-12f) { n[0] /= len; n[1] /= len; n[2] /= len; }

    std::fwrite(n, sizeof(float), 3, f);
    std::fwrite(v1, sizeof(float), 3, f);
    std::fwrite(v2, sizeof(float), 3, f);
    std::fwrite(v3, sizeof(float), 3, f);
    uint16_t attr = 0;
    std::fwrite(&attr, sizeof(uint16_t), 1, f);
}

bool write_stl(const std::string& path,
    const float* vertices, const uint32_t* indices, uint32_t triangle_count) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    char header[80];
    std::memset(header, 0, 80);
    std::memcpy(header, "guava-slicer", 12);
    std::fwrite(header, 1, 80, f);
    std::fwrite(&triangle_count, sizeof(uint32_t), 1, f);

    for (uint32_t i = 0; i < triangle_count; ++i)
        write_facet(f, vertices, indices, i);

    std::fclose(f);
    return true;
}

bool write_combined_stl(const std::string& path,
    const float* verts_a, const uint32_t* idx_a, uint32_t tris_a,
    const float* verts_b, const uint32_t* idx_b, uint32_t tris_b) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    char header[80];
    std::memset(header, 0, 80);
    std::memcpy(header, "guava-slicer", 12);
    std::fwrite(header, 1, 80, f);
    uint32_t total = tris_a + tris_b;
    std::fwrite(&total, sizeof(uint32_t), 1, f);

    for (uint32_t i = 0; i < tris_a; ++i)
        write_facet(f, verts_a, idx_a, i);
    for (uint32_t i = 0; i < tris_b; ++i)
        write_facet(f, verts_b, idx_b, i);

    std::fclose(f);
    return true;
}

uint64_t export_combined_stl(const std::string& path,
    const float* model_verts, const uint32_t* model_idx, uint32_t model_vert_count, uint32_t model_tri_count,
    const float transform[4][4],
    const float* support_verts, const uint32_t* support_idx, uint32_t support_tri_count) {

    std::vector<float> baked(model_vert_count * 3);
    for (uint32_t i = 0; i < model_vert_count; ++i) {
        float x = model_verts[i*3], y = model_verts[i*3+1], z = model_verts[i*3+2];
        baked[i*3+0] = transform[0][0]*x + transform[1][0]*y + transform[2][0]*z + transform[3][0];
        baked[i*3+1] = transform[0][1]*x + transform[1][1]*y + transform[2][1]*z + transform[3][1];
        baked[i*3+2] = transform[0][2]*x + transform[1][2]*y + transform[2][2]*z + transform[3][2];
    }

    bool ok;
    if (support_verts && support_idx && support_tri_count > 0) {
        ok = write_combined_stl(path,
            baked.data(), model_idx, model_tri_count,
            support_verts, support_idx, support_tri_count);
    } else {
        ok = write_stl(path, baked.data(), model_idx, model_tri_count);
    }

    if (!ok) return 0;

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;
    std::fseek(f, 0, SEEK_END);
    uint64_t size = static_cast<uint64_t>(std::ftell(f));
    std::fclose(f);
    return size;
}

} // namespace stl_writer
