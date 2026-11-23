// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace edge
{

/// A std::vector its copies share until one of them is written (copy on write): what a mesh and its copies (Mesh::shallow_copy(),
/// every instance of a ModelCache's model) hold of the CPU geometry they all draw, read by each and written by none once loaded.
/// Read through the const members or as the vector itself; written through write(), which copies first if a copy shares it.
/// Of one thread at a time, as the meshes holding it are.
template <class T>
class SharedVector {
public:
	using value_type = T;
	using size_type = std::size_t;
	using const_iterator = typename std::vector<T>::const_iterator;

	SharedVector(): data_(std::make_shared<std::vector<T>>()) {}

	operator std::vector<T> const &() const { return *data_; }

	std::size_t size() const { return data_->size(); }
	bool empty() const { return data_->empty(); }
	T const &operator[](std::size_t i) const { return (*data_)[i]; }
	T const *data() const { return data_->data(); }
	const_iterator begin() const { return data_->cbegin(); }
	const_iterator end() const { return data_->cend(); }

	/// The vector to change, this one's own.
	std::vector<T> &write()
	{
		if (data_.use_count() > 1)
			data_ = std::make_shared<std::vector<T>>(*data_);
		return *data_;
	}

	/// The vector's bytes, held by whoever is given them: the vector is copied before it is next written while they are.
	std::shared_ptr<std::byte const> shared_bytes() const
	{
		return std::shared_ptr<std::byte const>(data_, reinterpret_cast<std::byte const *>(data_->data()));
	}

	/// Empty, sharing nothing.
	void clear() { data_ = std::make_shared<std::vector<T>>(); }

private:
	std::shared_ptr<std::vector<T>> data_;
};

}
