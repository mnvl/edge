// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <vector>

#include "index.h"
#include "index_buffer.h"
#include "metal_context.h"
#include "vertex_buffer.h"
#include "vertex_traits.h"

namespace edge
{

/// A Metal buffer (shared storage) and its CPU copy: the end of a write() sends the copy whole.  A buffer a command buffer still to run draws with is
/// not written in place: it is made again (copy on write), the draws already encoded keep the old one.
class MetalBufferStore
{
public:
	MetalBufferStore(MetalContextPtr const &context, size_t size);
	~MetalBufferStore();
	MetalBufferStore(MetalBufferStore const &) = delete;
	MetalBufferStore &operator=(MetalBufferStore const &) = delete;

	void upload();
	/// A draw of the open frame uses it.
	void used();

	/// The CPU's copy, which upload() sends.
	std::vector<unsigned char> &bytes() { return bytes_; }
	std::vector<unsigned char> const &bytes() const { return bytes_; }
	struct Impl;
	Impl &impl() const { return *impl_; }

private:
	std::vector<unsigned char> bytes_;
	MetalContextPtr context_;
	std::unique_ptr<Impl> impl_;
};

class MetalVertexBuffer: public VertexBuffer
{
public:
	MetalVertexBuffer(MetalContextPtr const &context, VertexTraits const *traits, size_t size);

	MetalBufferStore &store() { return store_; }
	VertexTraits const *vertex_traits() const override { return traits_; }
	MetalContextPtr const &context() const { return context_; }

	size_t size() const override { return store_.bytes().size(); }

protected:
	const void *do_lock() const override { return store_.bytes().data(); }
	void do_unlock() const override {}
	void *do_lock(bool) override { return store_.bytes().data(); }
	void do_unlock() override { store_.upload(); }

private:
	MetalContextPtr context_;
	MetalBufferStore store_;
	VertexTraits const *traits_;
};

class MetalIndexBuffer: public IndexBuffer
{
public:
	/// std::invalid_argument for a format that is not IndexFormat::U16 or IndexFormat::U32.
	MetalIndexBuffer(MetalContextPtr const &context, IndexFormat index_format, size_t size);

	MetalBufferStore &store() { return store_; }
	size_t index_size() const override { return index_size_; }
	MetalContextPtr const &context() const { return context_; }

	size_t size() const override { return store_.bytes().size(); }

protected:
	const void *do_lock() const override { return store_.bytes().data(); }
	void do_unlock() const override {}
	void *do_lock(bool) override { return store_.bytes().data(); }
	void do_unlock() override { store_.upload(); }

private:
	MetalContextPtr context_;
	MetalBufferStore store_;
	size_t index_size_;
};

}
