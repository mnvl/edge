// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "index.h"
#include <memory>
#include "cpu_copy.h"
#include "index_buffer.h"

namespace edge
{

class NullIndexBuffer: public IndexBuffer {
public:
	NullIndexBuffer(IndexFormat index_format, size_t size);
	~NullIndexBuffer() override;

	size_t size() const override { return copy_.size(); }
	size_t index_size() const override { return index_format_ == IndexFormat::U16 ? 2 : 4; }


protected:
	const void *do_lock() const override;
	void do_unlock() const override;
	void *do_lock(bool discard) override;
	void do_unlock() override;
	void do_share(std::shared_ptr<std::byte const> bytes) override;

private:
	CpuCopy copy_;
	IndexFormat index_format_;
};

}
