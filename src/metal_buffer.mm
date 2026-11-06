// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "metal_impl.h"

#include "index.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace edge
{

namespace
{

id<MTLBuffer> make_buffer(MetalContext &context, size_t size, void const *bytes)
{
	// (a buffer of no bytes is not Metal's to make)
	size_t const length = std::max<size_t>(size, 16);
	id<MTLBuffer> buffer = [context.impl().device newBufferWithLength:length options:MTLResourceStorageModeShared];
	if (!buffer)
		throw std::runtime_error("Metal could not make a buffer of " + std::to_string(length) + " bytes");
	if (bytes && size)
		std::memcpy(buffer.contents, bytes, size);
	return buffer;
}

}

MetalBufferStore::MetalBufferStore(MetalContextPtr const &context, size_t size):
	bytes_(size, 0),
	context_(context),
	impl_(new Impl)
{
	impl_->buffer = make_buffer(*context, size, nullptr);
}

MetalBufferStore::~MetalBufferStore() = default;

void MetalBufferStore::upload()
{
	if (context_->busy(impl_->last_use))
	{
		// a command buffer still to run reads the buffer: it keeps that one, and later draws get the new
		impl_->buffer = make_buffer(*context_, bytes_.size(), bytes_.data());
		impl_->last_use = 0;
		return;
	}
	if (!bytes_.empty())
		std::memcpy(impl_->buffer.contents, bytes_.data(), bytes_.size());
}

void MetalBufferStore::used()
{
	impl_->last_use = context_->current_frame();
}

MetalVertexBuffer::MetalVertexBuffer(MetalContextPtr const &context, VertexTraits const *traits, size_t size):
	context_(context),
	store_(context, size),
	traits_(traits)
{
}

MetalIndexBuffer::MetalIndexBuffer(MetalContextPtr const &context, IndexFormat index_format, size_t size):
	context_(context),
	store_(context, size),
	index_size_(0)
{
	if (index_format == IndexFormat::U16)
		index_size_ = 2;
	else if (index_format == IndexFormat::U32)
		index_size_ = 4;
	else
		throw std::invalid_argument("an index buffer of format " + std::to_string(static_cast<unsigned>(index_format)));
}

}
