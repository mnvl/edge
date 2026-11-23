// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace edge
{

/// The CPU's copy of a buffer's bytes: its own, zero at first, or someone else's shared with it until it is written.  Read by any
/// thread while none writes or shares.
class CpuCopy {
public:
	explicit CpuCopy(std::size_t size): own_(size), size_(size) {}

	std::size_t size() const { return size_; }

	/// Null for a buffer of no bytes.
	std::byte const *read() const { return size_ == 0 ? nullptr : shared_ ? shared_.get() : own_.data(); }

	/// The bytes to change, this one's own from now: what was shared, or (@p discard) not.
	std::byte *write(bool discard)
	{
		if (shared_)
		{
			std::vector<std::byte> own(size_);
			if (!discard)
				std::copy_n(shared_.get(), size_, own.begin());
			own_ = std::move(own);
			shared_.reset();
		}
		return size_ == 0 ? nullptr : own_.data();
	}

	/// @p bytes, size() of them, in place of this one's own.
	void share(std::shared_ptr<std::byte const> bytes)
	{
		shared_ = std::move(bytes);
		own_ = std::vector<std::byte>();
	}

private:
	std::vector<std::byte> own_;
	std::shared_ptr<std::byte const> shared_;
	std::size_t size_;
};

}
