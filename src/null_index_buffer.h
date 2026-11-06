// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "index.h"
#include <vector>
#include "index_buffer.h"

namespace edge
{

class NullIndexBuffer: public IndexBuffer {
public:
	NullIndexBuffer(IndexFormat index_format, size_t size);
	~NullIndexBuffer() override;

	size_t size() const override { return size_; }


protected:
	const void *do_lock() const override;
	void do_unlock() const override;
	void *do_lock(bool discard) override;
	void do_unlock() override;

private:
	std::vector<char> buffer_;
	size_t size_;
	IndexFormat index_format_;
};

}
