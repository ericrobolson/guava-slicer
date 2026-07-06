/// @file support_types.h
/// @brief Data structures for support point placement and pillar geometry.
#pragma once

#include "linalg_types.h"

#include <cstdint>
#include <vector>

namespace support {

using linalg_types::Vec3;

/// @brief Category of support point — determines why it was placed.
enum class Category : uint8_t {
    ISLAND = 0,
    REINFORCEMENT = 1,
    OVERHANG = 2,
    STABILIZATION = 3,
};

constexpr uint8_t CATEGORY_COUNT = 4;

/// @brief Bitmask values for enabling/disabling support categories.
constexpr uint8_t CATEGORY_BIT_ISLAND        = 1 << static_cast<uint8_t>(Category::ISLAND);
constexpr uint8_t CATEGORY_BIT_REINFORCEMENT = 1 << static_cast<uint8_t>(Category::REINFORCEMENT);
constexpr uint8_t CATEGORY_BIT_OVERHANG      = 1 << static_cast<uint8_t>(Category::OVERHANG);
constexpr uint8_t CATEGORY_BIT_STABILIZATION = 1 << static_cast<uint8_t>(Category::STABILIZATION);
constexpr uint8_t CATEGORY_ALL = CATEGORY_BIT_ISLAND | CATEGORY_BIT_REINFORCEMENT |
                                  CATEGORY_BIT_OVERHANG | CATEGORY_BIT_STABILIZATION;

/// @brief A single support contact point on the model surface.
struct SupportPoint {
    Vec3 position;
    Vec3 normal;
    Vec3 ground_pos;
    Category category;
    uint32_t id;
};

/// @brief Default pillar geometry parameters.
constexpr float DEFAULT_TIP_DIAMETER = 0.2f;
constexpr float DEFAULT_TIP_PENETRATION = 0.1f;
constexpr float DEFAULT_TIP_LENGTH = 1.0f;
constexpr float DEFAULT_SHAFT_DIAMETER = 0.5f;
constexpr float DEFAULT_BASE_DIAMETER = 2.0f;
constexpr float DEFAULT_BASE_HEIGHT = 1.0f;
constexpr float DEFAULT_SPACING = 1.2f;

/// @brief Configurable parameters for pillar geometry generation.
struct SupportParams {
    float tip_diameter = DEFAULT_TIP_DIAMETER;
    float tip_penetration = DEFAULT_TIP_PENETRATION;
    float tip_length = DEFAULT_TIP_LENGTH;
    float shaft_diameter = DEFAULT_SHAFT_DIAMETER;
    float base_diameter = DEFAULT_BASE_DIAMETER;
    float base_height = DEFAULT_BASE_HEIGHT;
    float spacing = DEFAULT_SPACING;
    uint8_t enabled_categories = CATEGORY_ALL;
};

/// @brief Number of segments for cylindrical/conical cross-sections.
constexpr uint32_t PILLAR_SEGMENTS = 8;

/// @brief Minimum pillar height (mm) — must clear raft thickness.
constexpr float MIN_PILLAR_HEIGHT = 2.0f;

/// @brief Reinforcement ring radius multiplier relative to island area.
constexpr float REINFORCEMENT_RING_RADIUS = 2.0f;

/// @brief Number of reinforcement points placed around each island.
constexpr uint32_t REINFORCEMENT_POINTS_PER_ISLAND = 4;

/// @brief Stabilization aspect ratio threshold — features taller than this ratio are stabilized.
constexpr float STABILIZATION_ASPECT_RATIO = 3.0f;

/// @brief Stabilization support spacing along height (mm).
constexpr float STABILIZATION_VERTICAL_SPACING = 10.0f;

/// @brief Complete support state: points, parameters, and generated mesh geometry.
struct SupportCollection {
    std::vector<SupportPoint> points;
    SupportParams params;
    std::vector<float> mesh_vertices;
    std::vector<float> mesh_normals;
    std::vector<uint32_t> mesh_indices;
    uint32_t next_id = 1;

    /// @brief Count supports in a specific category.
    uint32_t count_category(Category cat) const {
        uint32_t n = 0;
        for (const auto& p : points) {
            if (p.category == cat) ++n;
        }
        return n;
    }

    /// @brief Clear all support data.
    void clear() {
        points.clear();
        mesh_vertices.clear();
        mesh_normals.clear();
        mesh_indices.clear();
        next_id = 1;
    }
};

/// @brief Human-readable name for a category.
inline const char* category_name(Category cat) {
    switch (cat) {
        case Category::ISLAND:         return "island";
        case Category::REINFORCEMENT:  return "reinforcement";
        case Category::OVERHANG:       return "overhang";
        case Category::STABILIZATION:  return "stabilization";
    }
    return "unknown";
}

/// @brief Parse category name string to enum. Returns false if unrecognized.
inline bool parse_category(const char* str, Category& out) {
    if (!str) return false;
    if (str[0] == 'i') { out = Category::ISLAND; return true; }
    if (str[0] == 'r') { out = Category::REINFORCEMENT; return true; }
    if (str[0] == 'o') { out = Category::OVERHANG; return true; }
    if (str[0] == 's') { out = Category::STABILIZATION; return true; }
    return false;
}

} // namespace support
