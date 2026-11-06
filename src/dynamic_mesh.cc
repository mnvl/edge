// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "dynamic_mesh.h"

#include <algorithm>
#include <cstring>

#include "draw_call.h"
#include "index.h"
#include "index_buffer.h"
#include "renderer.h"
#include "scheduler.h"
#include "uber_draw_call.h"
#include "vertex_buffer.h"
#include "vertex_traits.h"

namespace edge
{

DynamicMesh::DynamicMesh(RendererPtr const &renderer, MaterialPtr const &material, VertexTraits const *traits, bool uber):
	renderer_(renderer),
	traits_(traits)
{
	if (!renderer_ || !material || !traits_)
		throw std::invalid_argument("DynamicMesh: a renderer, a material and the vertex's traits are needed");
	vertex_size_ = traits_->size();
	draw_call_ = uber ? DrawCallPtr(renderer_->create_uber_draw_call()) : renderer_->create_draw_call();
	draw_call_->set_material(material);
	draw_call_->set_always_visible(true);
}

void DynamicMesh::upload(void const *vertices, std::size_t count, std::span<std::uint16_t const> indices)
{
	upload_indexed(vertices, count, indices, IndexFormat::U16);
}

void DynamicMesh::upload(void const *vertices, std::size_t count, std::span<std::uint32_t const> indices)
{
	upload_indexed(vertices, count, indices, IndexFormat::U32);
}

template <class I>
void DynamicMesh::upload_indexed(void const *vertices, std::size_t count, std::span<I const> indices, IndexFormat format)
{
	if (indices.size() % 3)
		throw std::invalid_argument("DynamicMesh::upload: " + std::to_string(indices.size()) + " indices are not whole triangles");
	auto const past = std::find_if(indices.begin(), indices.end(), [&](I i) { return std::size_t(i) >= count; });
	if (past != indices.end())
		throw std::invalid_argument("DynamicMesh::upload: index " + std::to_string(*past) + " of " + std::to_string(count) + " vertices");
	std::size_t const vertex_bytes = count * vertex_size_, index_bytes = indices.size() * sizeof(I);
	if (vertex_bytes > vertex_capacity_)
	{
		vertex_capacity_ = std::max(vertex_bytes, 2 * vertex_capacity_);
		vertex_buffer_ = renderer_->create_vertex_buffer(traits_, vertex_capacity_, BufferUsage::DYNAMIC);
		draw_call_->set_vertex_buffer(vertex_buffer_);
	}
	if (format != index_format_ || index_bytes > index_capacity_)
	{
		index_capacity_ = std::max(index_bytes, format == index_format_ ? 2 * index_capacity_ : std::size_t(0));
		index_format_ = format;
		index_buffer_ = index_capacity_ ? renderer_->create_index_buffer(format, index_capacity_, BufferUsage::DYNAMIC) : IndexBufferPtr();
		draw_call_->set_index_buffer(index_buffer_);
	}
	if (vertex_bytes)
	{
		auto mapped_vertices = vertex_buffer_->write(Contents::DISCARD);
		std::memcpy(mapped_vertices.data(), vertices, vertex_bytes);
		mapped_vertices.unmap();
	}
	if (index_bytes)
	{
		auto mapped_indices = index_buffer_->write(Contents::DISCARD);
		std::memcpy(mapped_indices.data(), indices.data(), index_bytes);
		mapped_indices.unmap();
	}
	triangles_ = indices.size() / 3;
	draw_call_->set_triangles_count(triangles_);
}

void DynamicMesh::draw(Scheduler &scheduler, CameraPtr const &camera)
{
	if (!triangles_)
		return;
	draw_call_->set_camera(camera);
	draw_call_->commit();
	scheduler.add(draw_call_);
}

}
