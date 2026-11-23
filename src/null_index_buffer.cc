// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "null_index_buffer.h"

#include <utility>

namespace edge
{

NullIndexBuffer::NullIndexBuffer(IndexFormat index_format, size_t size):
	copy_(size),
	index_format_(index_format)
{
}

NullIndexBuffer::~NullIndexBuffer() = default;

const void *NullIndexBuffer::do_lock() const { return copy_.read(); }
void NullIndexBuffer::do_unlock() const {}
void *NullIndexBuffer::do_lock(bool discard) { return copy_.write(discard); }
void NullIndexBuffer::do_unlock() {}
void NullIndexBuffer::do_share(std::shared_ptr<std::byte const> bytes) { copy_.share(std::move(bytes)); }


}
