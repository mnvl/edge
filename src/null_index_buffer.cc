// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "null_index_buffer.h"

namespace edge
{

NullIndexBuffer::NullIndexBuffer(IndexFormat index_format, size_t size):
	size_(size),
	index_format_(index_format)
{
	buffer_.resize(size);
}

NullIndexBuffer::~NullIndexBuffer() = default;

const void *NullIndexBuffer::do_lock() const { return buffer_.empty() ? nullptr : &buffer_[0]; }
void NullIndexBuffer::do_unlock() const {}
void *NullIndexBuffer::do_lock(bool discard) { return buffer_.empty() ? nullptr : &buffer_[0]; }
void NullIndexBuffer::do_unlock() {}


}
