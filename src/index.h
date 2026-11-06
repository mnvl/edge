// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdint>

namespace edge
{

/// What an index buffer's indices are.  (The values are those the indices were known by: the bindings take them as numbers too.)
enum class IndexFormat : unsigned {
	U16 = 101,
	U32 = 102,
};
using INDEX_U16 = std::uint16_t;
using INDEX_U32 = std::uint32_t;

}
