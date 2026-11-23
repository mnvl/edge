// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <memory>
#include "forward.h"
#include "mapping.h"

namespace edge
{

class VertexTraits;

class VertexBuffer: public std::enable_shared_from_this<VertexBuffer> {
public:
	VertexBuffer() = default;
	VertexBuffer(VertexBuffer const &) = delete;
	VertexBuffer &operator=(VertexBuffer const &) = delete;
	virtual ~VertexBuffer();

	/// The buffer's bytes (the CPU's copy), to read.
	Mapping<VertexBuffer const> read() const
	{
		return Mapping<VertexBuffer const>(*this, {static_cast<std::byte const *>(do_lock()), size()});
	}
	/// The buffer's bytes to write: sent to the GPU when the mapping ends.
	Mapping<VertexBuffer> write(Contents contents = Contents::KEEP)
	{
		return Mapping<VertexBuffer>(*this, {static_cast<std::byte *>(do_lock(contents == Contents::DISCARD)), size()});
	}

	/// @p bytes, as many as the buffer has (@p size says so), become the buffer's and are sent to the GPU as a write of them all is.  Its
	/// CPU copy is the bytes themselves, held and not copied, until a write() copies them first: whoever made them may read them
	/// on, and may not change them while the buffer holds them.  (OpenGL ES and Metal keep a copy of their own.)
	/// std::invalid_argument for none (but to a buffer of no bytes), or for another size than the buffer's.
	void share(std::shared_ptr<std::byte const> bytes, size_t size);

	virtual size_t size() const = 0;

	/// What is in the buffer (null where the maker did not say): a draw counts its vertices by it.
	virtual VertexTraits const *vertex_traits() const = 0;
	/// The renderer the buffer was made by, to tell whether a draw may use it: only compared.
	void const *owner() const { return owner_; }
	void set_owner(void const *owner) { owner_ = owner; }

protected:
	friend class Mapping<VertexBuffer>;
	friend class Mapping<VertexBuffer const>;

	/// What a backend gives read() and write(): the CPU's copy (null for an empty buffer), and the end of the mapping.
	virtual void const *do_lock() const = 0;
	virtual void do_unlock() const = 0;
	virtual void *do_lock(bool discard) = 0;
	virtual void do_unlock() = 0;
	/// share() once its arguments are checked; the default writes a copy.
	virtual void do_share(std::shared_ptr<std::byte const> bytes);

private:
	void const *owner_ = nullptr;
};

}
