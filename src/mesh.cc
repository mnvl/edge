// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "log.h"
#include <stdexcept>
#include <utility>
#include "mesh.h"
#include "model.h"
#include "renderer.h"
#include "uber_draw_call.h"

namespace edge {

Mesh::Mesh(Model *model_ptr):
	model_(model_ptr)
{
	bind_shape_matrix_.identity();
}

Mesh::~Mesh() = default;

void Mesh::set_model(Model *model)
{
	model_ = model;
}

Model *Mesh::model() const
{
	return model_;
}

void Mesh::check()
{
	EDGE_LOG_INFO("checking mesh id = {}, name = {}...", id_, name_);
}

void Mesh::set_morph_targets(std::vector<std::vector<GenericVertex>> const &targets,
	GenericVertex::format_type format)
{
	(void)targets;
	(void)format;
}

void Mesh::set_morph_weights(std::vector<float> const &weights)
{
	(void)weights;
}

std::size_t Mesh::morph_targets_count() const
{
	return 0;
}

void Mesh::draw(Scheduler &scheduler, Joint *joint_ptr, CameraPtr const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod)
{
}

void Mesh::draw_instances(Scheduler &scheduler, Joint *joint_ptr, CameraPtr const &camera, std::vector<rove::matrix<4,4>> const &world_matrices, unsigned lod)
{
	for (rove::matrix<4,4> const &world_matrix : world_matrices)
		draw(scheduler, joint_ptr, camera, world_matrix, lod);
}

std::string Mesh::serialization_type() const
{
	return "mesh";
}

void Mesh::save_geometry(SerializedMesh &) const
{
	throw std::runtime_error("mesh '" + name() + "' is of a type that is not saved");
}

void Mesh::load_geometry(SerializedMesh const &, bool)
{
	throw std::runtime_error("mesh '" + name() + "' is of a type that is not loaded");
}

}
