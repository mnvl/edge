// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "null_vertex_buffer.h"

#include <utility>

namespace edge
{

NullVertexBuffer::NullVertexBuffer(VertexTraits const *vertex_traits, size_t size):
	copy_(size),
	traits_(vertex_traits)
{
}

NullVertexBuffer::~NullVertexBuffer() = default;

const void *NullVertexBuffer::do_lock() const { return copy_.read(); }
void NullVertexBuffer::do_unlock() const {}
void *NullVertexBuffer::do_lock(bool discard) { return copy_.write(discard); }
void NullVertexBuffer::do_unlock() {}
void NullVertexBuffer::do_share(std::shared_ptr<std::byte const> bytes) { copy_.share(std::move(bytes)); }
size_t NullVertexBuffer::size() const { return copy_.size(); }


}
