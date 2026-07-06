/// @file slicer_commands.h
/// @brief IPC command registration for the slicing engine.
#pragma once

#include "slicer.h"

#include <mutex>

namespace slicer_commands {

/// @brief Register slice and get_layer IPC commands.
void register_all();

/// @brief Access the cached slice result under a lock.
///
/// Calls the provided function with a const reference to the cached result
/// and a bool indicating whether a result exists. The mutex is held for
/// the duration of the callback.
void with_cached_result(
    const std::function<void(const slicer::SliceResult& result, bool has_result)>& fn);

} // namespace slicer_commands
