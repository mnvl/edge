// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <utility>
#include "forward.h"
#include "index.h"
#include "index_buffer.h"

namespace edge
{

template <class T>
class IndexArray {
public:
	using index_type = T;

	IndexArray(IndexBufferPtr const &index_buffer):
		index_buffer_(index_buffer)
	{
	}

	~IndexArray() = default;

	/// The indices to read (mapping.as<index_type const>()), or to write, sent when the mapping ends (IndexBuffer::read(), write()).
	Mapping<IndexBuffer const> read() const { return std::as_const(*index_buffer_).read(); }
	Mapping<IndexBuffer> write(Contents contents = Contents::KEEP) { return index_buffer_->write(contents); }

	IndexBufferPtr const &index_buffer() { return index_buffer_; }

protected:
	IndexBufferPtr const index_buffer_;
};

using IndexArray16 = IndexArray<INDEX_U16>;
using IndexArray32 = IndexArray<INDEX_U32>;

}
