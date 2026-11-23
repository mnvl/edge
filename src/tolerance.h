// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// The thresholds below which edge takes a quantity for zero.

namespace edge
{

inline constexpr float ZERO_DETERMINANT = 1.0e-6f;     // a bind shape matrix whose determinant is this small flattens the mesh
inline constexpr float SAME_TIME = 1.0e-6f;            // seconds: two samples this close are at one moment

}
