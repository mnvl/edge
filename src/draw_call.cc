// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <stdexcept>
#include <string>

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

void DrawCall::set_instances(Placement::Matrices const &instances)
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

void DrawCall::set_previous_world_matrix(rove::matrix<4,4> const &world_matrix)
{
	keep_given_of(renderer_->frame_index());
	given_world_ = world_matrix;
}

void DrawCall::set_previous_instances(std::vector<rove::matrix<4,4>> const &instances)
{
	if (instances.empty())
	{
		throw std::invalid_argument("previous instances of a draw call: none");
	}
	keep_given_of(renderer_->frame_index());
	given_instances_ = std::make_shared<std::vector<rove::matrix<4,4>> const>(instances);
}

void DrawCall::give_previous_joint_matrices(std::vector<rove::matrix<4,4>> const &joint_matrices)
{
	keep_given_of(renderer_->frame_index());
	given_joint_matrices_ = std::make_shared<std::vector<rove::matrix<4,4>> const>(joint_matrices);
}

void DrawCall::set_previous(Placement const &previous)
{
	if (previous.instances && previous.instances->empty())
	{
		throw std::invalid_argument("previous instances of a draw call: none");
	}
	keep_given_of(renderer_->frame_index());
	given_world_ = previous.world;
	given_instances_ = previous.instances;
	given_joint_matrices_ = previous.joints;
}

void DrawCall::reset_previous()
{
	history_.reset();
	given_world_.reset();
	given_instances_.reset();
	given_joint_matrices_.reset();
}

void DrawCall::keep_given_of(std::uint64_t frame)
{
	if (given_frame_ != frame)
	{
		given_world_.reset();
		given_instances_.reset();
		given_joint_matrices_.reset();
		given_frame_ = frame;
	}
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
	std::uint64_t const frame = renderer_->frame_index();
	keep_given_of(frame);
	std::vector<rove::matrix<4,4>> const *const joints = joint_matrices();
	if (given_instances_ && !instances_)
		throw std::logic_error("previous instances given to a draw call that is not instanced");
	if (given_joint_matrices_ && !joints)
		throw std::logic_error("previous joint matrices given to a draw call that has none");
	if (given_joint_matrices_ && given_joint_matrices_->size() != joints->size())
		throw std::invalid_argument(std::to_string(given_joint_matrices_->size()) + " previous joint matrices given to a draw call of "
			+ std::to_string(joints->size()));

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

	// the frame before (the camera's): what the call remembers or was given, through what its camera was
	using Matrices = std::vector<rove::matrix<4,4>>;
	Placement const *const before = history_.use(frame, renderer_->previous_frame(camera_), Placement{world_matrix_, instances_,
		shared_joint_matrices()});
	rove::matrix<4,4> const previous_world = given_world_ ? *given_world_ : before ? before->world : world_matrix_;
	Matrices const *const previous_instances = given_instances_ ? given_instances_.get() : before ? before->instances.get() : nullptr;
	previous_joint_matrices_ = !joints ? nullptr : given_joint_matrices_ ? given_joint_matrices_ : before ? before->joints
		: shared_joint_matrices();
	std::optional<rove::matrix<4,4>> const camera_before = renderer_->previous_view_projection(camera_, view_projection);
	rove::matrix<4,4> const previous_view_projection = camera_before ? *camera_before : view_projection;
	previous_known_ = camera_before && (before || (given_world_ && (!instances_ || given_instances_) && (!joints ||
		given_joint_matrices_)));
	if (!instances_)
	{
		shader_previous_world_view_projection_ = previous_view_projection * previous_world;
		shader_previous_world_ = previous_world;
	}
	else
	{
		shader_previous_world_view_projection_ = previous_view_projection;
		shader_previous_world_.identity();
	}

	frustum_.load(view_projection);
	visible_instances_.clear();
	previous_visible_instances_.clear();
	rove::vec<3> centres(0, 0, 0);
	for (size_t i = 0; instances_ && i < instances_->size(); ++i)
	{
		rove::matrix<4,4> const &instance = (*instances_)[i];
		rove::obb<3> bounds = bounds_;
		bounds.transform(instance);
		if (always_visible_ || frustum_.test_intersection(bounds))
		{
			visible_instances_.push_back(instance * world_matrix_);
			previous_visible_instances_.push_back(previous_instances && i < previous_instances->size()
				? (*previous_instances)[i] * previous_world : instance * world_matrix_);
			centres += bounds.centre();
		}
	}
	rove::vec<3> const centre = !instances_ ? bounds_.centre()
		: visible_instances_.empty() ? rove::vec<3>(camera_->eye_point()) : centres / float(visible_instances_.size());
	view_depth_ = rove::dot(centre - camera_->eye_point(), camera_->view_dir());
	committed_ = true;
	committed_frame_ = frame;
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
	std::uint64_t const frame = renderer_->frame_index();
	if (committed_frame_ != frame)
	{
		// drawn in a frame after its commit, which a call or camera that changed is given again: it did not move
		shader_previous_world_view_projection_ = shader_world_view_projection_;
		shader_previous_world_ = shader_world_;
		previous_visible_instances_ = visible_instances_;
		previous_joint_matrices_ = shared_joint_matrices();
		previous_known_ = true;
		committed_frame_ = frame;
	}
	else if (std::vector<rove::matrix<4,4>> const *const joints = joint_matrices();
		joints && (!previous_joint_matrices_ || joints->size() != previous_joint_matrices_->size()))
	{
		throw std::logic_error("a draw call drawn with " + std::to_string(joints->size()) + " joint matrices, committed in this frame with "
			+ std::to_string(previous_joint_matrices_ ? previous_joint_matrices_->size() : 0) +
			": the previous ones of these are not known (commit() again)");
	}
	if (triangles_count_)
	{
		renderer_->draw(*this);
	}
}

}
