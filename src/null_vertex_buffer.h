// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vector>
#include "vertex_buffer.h"
#include "vertex_traits.h"

namespace edge
{

class NullVertexBuffer: public VertexBuffer {
public:
	NullVertexBuffer(VertexTraits const *vertex_traits, size_t size);
	~NullVertexBuffer() override;

	size_t size() const override;


protected:
	const void *do_lock() const override;
	void do_unlock() const override;
	void *do_lock(bool discard) override;
	void do_unlock() override;

private:
	std::vector<char> buffer_;
	size_t size_;
	VertexTraits const *traits_;
};

}
