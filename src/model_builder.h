// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <rove/matrix.h>
#include "model.h"
#include "mesh_builder.h"

namespace edge {

// ModelBuilder is for constructing models and saving them to .edge files.
// It cannot render — load saved .edge files with StaticModel/AnimatedModel for rendering.
class ModelBuilder: public Model {
public:
	ModelBuilder(RendererPtr const &renderer_ptr, SchedulerPtr const &scheduler_ptr);
	~ModelBuilder() override;

	MeshBuilderPtr mesh_by_id(std::string const &id) {
		return std::static_pointer_cast<MeshBuilder>(Model::mesh_by_id(id));
	}

	MeshBuilderPtr mesh_by_name(std::string const &name) {
		return std::static_pointer_cast<MeshBuilder>(Model::mesh_by_name(name));
	}

	MeshBuilderPtr add_mesh_builder(std::string const &id = std::string(),
		std::string const &name = std::string());
	JointPtr attach_mesh_to_joint(MeshBuilderPtr const &mesh_ptr, std::string const &joint_name = std::string());
	MeshBuilderPtr add_geometry(std::vector<MeshBuilder::vertex_type> const &vertices,
		std::vector<MeshBuilder::index_type> const &indices,
		std::string const &mesh_name = std::string(),
		MaterialPtr const &material_ptr = MaterialPtr());

	MeshPtr add_mesh() override;
	std::string mesh_serialization_type() const override { return MeshBuilder::class_serialization_type(); }


	using Model::draw;
	/// A ModelBuilder does not draw: std::runtime_error.  Save it and load it as a StaticModelPTBNT.
	void draw(CameraPtr const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod = 0) override;
};

}
