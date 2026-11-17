// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <stdexcept>

#include <rove/frustum.h>
#include "vertex_traits.h"
#include "camera.h"
#include "texture.h"
#include "material.h"
#include "renderer.h"
#include "draw_call.h"

namespace edge
{

DrawCall::DrawCall(RendererPtr const &renderer):
	renderer_(renderer),
	priority_(0),
	always_visible_(false),
	minimal_vertex_index_(0),
	first_index_offset_(0),
	triangles_count_(0)
{
	world_matrix_.identity();
}

DrawCall::~DrawCall() = default;

void DrawCall::set_priority(int priority)
{
	priority_ = priority;
}

void DrawCall::set_always_visible(bool always_visible)
{
	always_visible_ = always_visible;
}

void DrawCall::set_obb(rove::obb<3> const &bounds)
{
	bounds_ = bounds;
}

void DrawCall::set_vertex_buffer(VertexBufferPtr const &vertex_buffer)
{
	vertex_buffer_ = vertex_buffer;
}

void DrawCall::set_index_buffer(IndexBufferPtr const &index_buffer)
{
	index_buffer_ = index_buffer;
}

void DrawCall::set_material(MaterialPtr const &material)
{
	material_ = material;
}

void DrawCall::set_minimal_vertex_index(size_t minimal_vertex_index)
{
	minimal_vertex_index_ = minimal_vertex_index;
}

void DrawCall::set_first_index_offset(size_t first_index_offset)
{
	first_index_offset_ = first_index_offset;
}

void DrawCall::set_triangles_count(size_t triangles_count)
{
	triangles_count_ = triangles_count;
}

void DrawCall::set_world_matrix(rove::matrix<4,4> const &world_matrix)
{
	world_matrix_ = world_matrix;
}

void DrawCall::set_instances(std::vector<rove::matrix<4,4>> const &instances)
{
	if (instances.empty())
	{
		throw std::invalid_argument("an instanced draw call of no instances");
	}
	instances_ = std::make_shared<std::vector<rove::matrix<4,4>> const>(instances);
}

void DrawCall::set_instances(SharedMatrices const &instances)
{
	if (!instances || instances->empty())
	{
		throw std::invalid_argument("an instanced draw call of no instances");
	}
	instances_ = instances;
}

void DrawCall::set_camera(CameraPtr const &camera)
{
	camera_ = camera;
}

// commit() bakes the shader's matrices from the camera's current state.  The
// matrices are NOT updated automatically when the camera moves — commit() must be called again before execute() whenever
// the camera or world matrix has changed (e.g. once per frame for a moving
// camera).
void DrawCall::commit()
{
	if (!vertex_buffer_) throw std::logic_error("vertex buffer not set");
	if (!material_) throw std::logic_error("material not set");
	if (!camera_) throw std::logic_error("camera not set");

	rove::matrix<4,4> const view_projection = camera_->projection_matrix() * camera_->view_matrix();
	if (!instances_)
	{
		shader_world_view_projection_ = view_projection * world_matrix_;
		shader_world_ = world_matrix_;
	}
	else
	{
		shader_world_view_projection_ = view_projection;
		shader_world_.identity();
	}

	frustum_.load(view_projection);
	visible_instances_.clear();
	rove::vec<3> centres(0, 0, 0);
	for (size_t i = 0; instances_ && i < instances_->size(); ++i)
	{
		rove::matrix<4,4> const &instance = (*instances_)[i];
		rove::obb<3> bounds = bounds_;
		bounds.transform(instance);
		if (always_visible_ || frustum_.test_intersection(bounds))
		{
			visible_instances_.push_back(instance * world_matrix_);
			centres += bounds.centre();
		}
	}
	rove::vec<3> const centre = !instances_ ? bounds_.centre()
		: visible_instances_.empty() ? rove::vec<3>(camera_->eye_point()) : centres / float(visible_instances_.size());
	view_depth_ = rove::dot(centre - camera_->eye_point(), camera_->view_dir());
	committed_ = true;
}

bool DrawCall::is_visible() const
{
	if (instances_)
	{
		return !visible_instances_.empty();
	}
	return always_visible_ || frustum_.test_intersection(bounds_);
}

size_t DrawCall::instance_count() const
{
	return !instances_ ? 1 : visible_instances_.size();
}

size_t DrawCall::offered_instance_count() const
{
	return !instances_ ? 1 : instances_->size();
}

void DrawCall::execute()
{
	if (!committed_)
	{
		throw std::logic_error("a draw call executed before commit()");
	}
	if (triangles_count_)
	{
		renderer_->draw(*this);
	}
}

void DrawCall::execute(DrawList &list)
{
	if (!committed_)
	{
		throw std::logic_error("a draw call executed before commit()");
	}
	if (triangles_count_)
	{
		list.draw(*this);
	}
}

}
