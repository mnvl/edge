// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "null_vertex_buffer.h"

namespace edge
{

NullVertexBuffer::NullVertexBuffer(VertexTraits const *vertex_traits, size_t size):
	size_(size),
	traits_(vertex_traits)
{
	buffer_.resize(size);
}

NullVertexBuffer::~NullVertexBuffer() = default;

const void *NullVertexBuffer::do_lock() const { return buffer_.empty() ? nullptr : &buffer_[0]; }
void NullVertexBuffer::do_unlock() const {}
void *NullVertexBuffer::do_lock(bool discard) { return buffer_.empty() ? nullptr : &buffer_[0]; }
void NullVertexBuffer::do_unlock() {}
size_t NullVertexBuffer::size() const { return size_; }


}
