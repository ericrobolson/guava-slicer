/// @file island_commands.h
/// @brief IPC command registration for island detection.
#pragma once

namespace island_commands {

/// @brief Register detect_islands, get_island_layer, and cancel_island_detection IPC commands.
void register_all();

} // namespace island_commands
