// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "index.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

#include "forward.h"

namespace edge
{

class VertexTraits;

/// A mesh whose vertices and triangles the host makes again whenever it likes (cloth, a wake, a UI): one draw call of buffers that
/// grow to the most they were given and are written again at each upload.  Its draw call is the caller's to set up further (world
/// matrix, priority, ...); it is always visible (not culled: no bounds are kept).
class DynamicMesh
{
public:
	/// Vertices of @p traits drawn with @p material, by an uber draw call (texture blending, a skin) where @p uber.
	/// std::invalid_argument for no renderer, material or traits.
	DynamicMesh(RendererPtr const &renderer, MaterialPtr const &material, VertexTraits const *traits, bool uber = false);

	/// Replaces what the mesh holds with @p count vertices of the traits' size at @p vertices and the triangles of @p indices
	/// (16 or 32 bits).  std::invalid_argument, the mesh left as it was, for indices that are not whole triangles or name a vertex
	/// past @p count.
	void upload(void const *vertices, std::size_t count, std::span<std::uint16_t const> indices);
	void upload(void const *vertices, std::size_t count, std::span<std::uint32_t const> indices);
	/// The same, std::invalid_argument for a vertex of another size than the traits'.
	template <class V, class I>
	void upload(std::span<V const> vertices, std::span<I const> indices)
	{
		if (sizeof(V) != vertex_size_)
			throw std::invalid_argument("DynamicMesh::upload: vertices of " + std::to_string(sizeof(V)) + " bytes for a mesh of " +
				std::to_string(vertex_size_));
		upload(static_cast<void const *>(vertices.data()), vertices.size(), indices);
	}

	/// Queues the draw call in @p scheduler, seen by @p camera, if the mesh holds a triangle.
	void draw(Scheduler &scheduler, CameraPtr const &camera);

	std::size_t triangles() const { return triangles_; }
	DrawCallPtr const &draw_call() const { return draw_call_; }

private:
	RendererPtr renderer_;
	VertexTraits const *traits_;
	std::size_t vertex_size_;
	DrawCallPtr draw_call_;
	VertexBufferPtr vertex_buffer_;
	IndexBufferPtr index_buffer_;
	IndexFormat index_format_{};
	std::size_t vertex_capacity_ = 0, index_capacity_ = 0, triangles_ = 0;

	template <class I> void upload_indexed(void const *vertices, std::size_t count, std::span<I const> indices, IndexFormat format);
};

}
