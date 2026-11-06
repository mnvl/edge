// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// The thresholds below which edge takes a quantity for zero.

namespace edge
{

inline constexpr float ZERO_WEIGHT = 1.0e-6f;          // a skin or morph weight this small does not count
// a normal, tangent or binormal whose squared length is this small (a length of 1e-3) has no direction: not rove's
// scalar_traits::zero_length_sq (1e-12), which is where a length is zero
inline constexpr float NO_DIRECTION_LENGTH_SQ = 1.0e-6f;
inline constexpr float ZERO_DETERMINANT = 1.0e-6f;     // a bind shape matrix whose determinant is this small flattens the mesh
inline constexpr float SAME_TIME = 1.0e-6f;            // seconds: two samples this close are at one moment

}
