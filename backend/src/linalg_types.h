/// @file linalg_types.h
/// @brief Shared linalg type aliases used across the backend.
#pragma once

#include <linalg/linalg.h>

namespace linalg_types {

using Vec3 = linalg::vec<float, 3>;
using Vec4 = linalg::vec<float, 4>;
using Mat4 = linalg::mat<float, 4, 4>;

} // namespace linalg_types
