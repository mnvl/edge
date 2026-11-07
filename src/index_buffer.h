// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <map>
#include <utility>
#include <cstddef>
#include "forward.h"
#include "mapping.h"

namespace edge
{

class IndexBuffer: public std::enable_shared_from_this<IndexBuffer> {
public:
	IndexBuffer() = default;
	IndexBuffer(IndexBuffer const &) = delete;
	IndexBuffer &operator=(IndexBuffer const &) = delete;
	virtual ~IndexBuffer();

	/// The buffer's bytes (the CPU's copy), to read.
	Mapping<IndexBuffer const> read() const
	{
		return Mapping<IndexBuffer const>(*this, {static_cast<std::byte const *>(do_lock()), size()});
	}
	/// The buffer's bytes to write: sent to the GPU when the mapping ends.
	Mapping<IndexBuffer> write(Contents contents = Contents::KEEP)
	{
		highest_.clear();
		return Mapping<IndexBuffer>(*this, {static_cast<std::byte *>(do_lock(contents == Contents::DISCARD)), size()});
	}

	virtual size_t size() const = 0;

	/// The bytes of one index
	virtual size_t index_size() const = 0;
	/// The largest index of the @p count from @p first: what a draw of them needs its vertices to reach.
	size_t highest_index(size_t first, size_t count) const;
	/// The renderer the buffer was made by, to tell whether a draw may use it: only compared.
	void const *owner() const { return owner_; }
	void set_owner(void const *owner) { owner_ = owner; }

protected:
	friend class Mapping<IndexBuffer>;
	friend class Mapping<IndexBuffer const>;

	/// What a backend gives read() and write(): the CPU's copy (null for an empty buffer), and the end of the mapping.
	virtual void const *do_lock() const = 0;
	virtual void do_unlock() const = 0;
	virtual void *do_lock(bool discard) = 0;
	virtual void do_unlock() = 0;

private:
	void const *owner_ = nullptr;
	// the answers of highest_index, which every draw asks and a mesh's groups each ask of their own range: until the next write()
	mutable std::map<std::pair<size_t, size_t>, size_t> highest_;
};

}
