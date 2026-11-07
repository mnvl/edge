// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <memory>
#include "index_buffer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace edge
{

IndexBuffer::~IndexBuffer() = default;

size_t IndexBuffer::highest_index(size_t first, size_t count) const
{
	auto const known = highest_.find({first, count});
	if (known != highest_.end())
		return known->second;
	auto const bytes = read();
	size_t highest = 0;
	std::byte const *const data = bytes.data() + first * index_size();
	if (index_size() == 2)
	{
		for (size_t k = 0; k < count; ++k)
		{
			std::uint16_t value;
			std::memcpy(&value, data + k * 2, 2);
			highest = std::max(highest, size_t(value));
		}
	}
	else
	{
		for (size_t k = 0; k < count; ++k)
		{
			std::uint32_t value;
			std::memcpy(&value, data + k * 4, 4);
			highest = std::max(highest, size_t(value));
		}
	}
	highest_[{first, count}] = highest;
	return highest;
}

}
