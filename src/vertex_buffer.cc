// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <memory>
#include "vertex_buffer.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace edge
{

VertexBuffer::~VertexBuffer() = default;

void VertexBuffer::share(std::shared_ptr<std::byte const> bytes, size_t size)
{
	if ((!bytes && size != 0) || size != this->size())
		throw std::invalid_argument("VertexBuffer::share: " + (bytes ? std::to_string(size) + " bytes" : std::string("no bytes"))
			+ " for a buffer of " + std::to_string(this->size()));
	do_share(std::move(bytes));
}

void VertexBuffer::do_share(std::shared_ptr<std::byte const> bytes)
{
	auto mapping = write(Contents::DISCARD);
	std::copy_n(bytes.get(), mapping.size(), mapping.data());
	mapping.unmap();
}

}
