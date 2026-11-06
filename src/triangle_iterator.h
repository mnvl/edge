// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>
#include <rove/matrix.h>
#include <rove/triangle.h>
#include <rove/vec.h>

namespace edge
{

/// Sentinel type for TriangleIterator end-of-range comparison.
struct TriangleSentinel {};

/// C++ forward iterator over transformed triangles from indexed mesh data.
/// Templated on vertex type V, index type I, and triangle group type G
/// (e.g. Mesh::Triangles with first_index_offset / triangles_count members).
///
/// Also acts as its own range — use directly in range-for:
///   TriangleIterator<V, I, G> tris(vertices, indices, groups, transform);
///   for (auto const &tri : tris) { ... }
template<class V, class I, class G>
class TriangleIterator {
public:
	/// Constructs an iterator over triangles from indexed mesh data.
	/// @param vertices  Vertex buffer (each element must have a `.position` member).
	/// @param indices   Index buffer (triplets of indices forming triangles).
	/// @param groups    Triangle groups defining sub-ranges within the index buffer.
	///                  If empty, the entire index buffer is treated as one flat list.
	/// @param transform Model-to-world matrix applied to each vertex position.
	TriangleIterator(
		std::vector<V> const &vertices,
		std::vector<I> const &indices,
		std::vector<G> const &groups,
		rove::matrix<4,4> const &transform)
		: vertices_(vertices)
		, indices_(indices)
		, groups_(groups)
		, transform_(transform)
		, vertex_count_(vertices.size())
		, index_count_(indices.size())
	{
		if (!vertices_.empty() && !indices_.empty())
		{
			if (groups_.empty())
			{
				flat_mode_ = true;
				offset_ = 0;
				end_ = index_count_;
			}
			else
			{
				group_idx_ = 0;
				begin_group();
			}
			skip_invalid();
		}
	}

	/// Returns the current triangle with vertex positions transformed by the matrix.
	rove::triangle<3> operator*() const
	{
		std::size_t const ia = indices_[offset_ + 0];
		std::size_t const ib = indices_[offset_ + 1];
		std::size_t const ic = indices_[offset_ + 2];
		return rove::triangle<3>(rove::transform_point(transform_, vertices_[ia].position),
			rove::transform_point(transform_, vertices_[ib].position), rove::transform_point(transform_, vertices_[ic].position));
	}

	/// Advances to the next valid triangle, skipping degenerate index triplets.
	TriangleIterator &operator++()
	{
		offset_ += 3;
		skip_invalid();
		return *this;
	}

	friend bool operator!=(TriangleIterator const &it, TriangleSentinel) { return it.valid(); }
	friend bool operator==(TriangleIterator const &it, TriangleSentinel) { return !it.valid(); }

	/// @name Range interface
	/// @{
	TriangleIterator &begin() { return *this; }
	TriangleSentinel end() const { return {}; }
	/// @}

private:
	bool valid() const { return offset_ + 2 < end_; }

	void begin_group()
	{
		offset_ = groups_[group_idx_].first_index_offset;
		std::size_t const count = groups_[group_idx_].triangles_count * 3;
		end_ = (offset_ < index_count_ && count > 0)
			? std::min(index_count_, offset_ + count)
			: 0;
	}

	void skip_invalid()
	{
		for (;;)
		{
			while (offset_ + 2 < end_)
			{
				std::size_t const ia = indices_[offset_ + 0];
				std::size_t const ib = indices_[offset_ + 1];
				std::size_t const ic = indices_[offset_ + 2];
				if (ia < vertex_count_ && ib < vertex_count_ && ic < vertex_count_)
					return;
				offset_ += 3;
			}
			if (flat_mode_)
				return;
			++group_idx_;
			if (group_idx_ >= groups_.size())
				return;
			begin_group();
		}
	}

	std::vector<V> const &vertices_;
	std::vector<I> const &indices_;
	std::vector<G> const &groups_;
	rove::matrix<4,4> transform_;
	std::size_t vertex_count_;
	std::size_t index_count_;
	std::size_t group_idx_ = 0;
	std::size_t offset_ = 0;
	std::size_t end_ = 0;
	bool flat_mode_ = false;
};

}  // namespace edge
