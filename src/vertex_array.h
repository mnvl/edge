// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <utility>
#include "forward.h"
#include "vertex.h"
#include "vertex_buffer.h"

namespace edge
{

class Renderer;

template <vertex_layout T>
class VertexArray {
public:
	using vertex_type = T;

	VertexArray(VertexBufferPtr const &vertex_buffer):
		vertex_buffer_(vertex_buffer)
	{
	}

	~VertexArray() = default;

	/// The vertices to read (mapping.as<vertex_type const>()), or to write, sent when the mapping ends (VertexBuffer::read(), write()).
	Mapping<VertexBuffer const> read() const { return std::as_const(*vertex_buffer_).read(); }
	Mapping<VertexBuffer> write(Contents contents = Contents::KEEP) { return vertex_buffer_->write(contents); }

	size_t size() const { return vertex_buffer_->size(); }
	size_t count() const { return size() / sizeof(T); }

	VertexBufferPtr const &vertex_buffer() { return vertex_buffer_;	}

protected:
	VertexBufferPtr vertex_buffer_;
};

using VertexArrayPT = VertexArray<VERTEX_PT>;
using VertexArrayPNT = VertexArray<VERTEX_PNT>;
using VertexArrayPNTB = VertexArray<VERTEX_PNTB>;
using VertexArrayPTBNT = VertexArray<VERTEX_PTBNT>;
using VertexArrayPTBNTC = VertexArray<VERTEX_PTBNTC>;
using VertexArrayPNJWT = VertexArray<VERTEX_PNJWT>;
using VertexArrayPTBNJWT = VertexArray<VERTEX_PTBNJWT>;
using VertexArrayPTBNJWTC = VertexArray<VERTEX_PTBNJWTC>;

}
