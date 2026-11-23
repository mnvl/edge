// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include "cpu_copy.h"
#include "vertex_buffer.h"
#include "vertex_traits.h"

namespace edge
{

class NullVertexBuffer: public VertexBuffer {
public:
	NullVertexBuffer(VertexTraits const *vertex_traits, size_t size);
	~NullVertexBuffer() override;

	size_t size() const override;
	VertexTraits const *vertex_traits() const override { return traits_; }


protected:
	const void *do_lock() const override;
	void do_unlock() const override;
	void *do_lock(bool discard) override;
	void do_unlock() override;
	void do_share(std::shared_ptr<std::byte const> bytes) override;

private:
	CpuCopy copy_;
	VertexTraits const *traits_;
};

}
