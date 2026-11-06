// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <stdexcept>

#include "model_builder.h"

namespace edge
{

ModelBuilder::ModelBuilder(RendererPtr const &renderer_ptr, SchedulerPtr const &scheduler_ptr):
	Model(renderer_ptr, scheduler_ptr)
{
}

ModelBuilder::~ModelBuilder() = default;

MeshBuilderPtr ModelBuilder::add_mesh_builder(std::string const &id, std::string const &name)
{
	MeshBuilderPtr const mesh_ptr = std::static_pointer_cast<MeshBuilder>(add_mesh());
	if (!id.empty())
	{
		mesh_ptr->set_id(id);
	}
	if (!name.empty())
	{
		mesh_ptr->set_name(name);
	}
	return mesh_ptr;
}

JointPtr ModelBuilder::attach_mesh_to_joint(MeshBuilderPtr const &mesh_ptr, std::string const &joint_name)
{
	if (!mesh_ptr)
	{
		throw std::runtime_error("cannot attach null mesh to joint");
	}

	JointPtr const joint_ptr = add_joint();
	joint_ptr->matrix.identity();
	std::string const base_name = !joint_name.empty() ? joint_name :
		(!mesh_ptr->name().empty() ? mesh_ptr->name() : ("mesh_" + std::to_string(joint_ptr->index)));
	joint_ptr->id = base_name;
	joint_ptr->name = base_name;
	// Assign the weak_ptr from the meshes vector (not the argument's shared_ptr)
	// to ensure the weak_ptr tracks the same control block that keeps the mesh alive.
	// Python bindings may create shared_ptrs with independent control blocks.
	bool found = false;
	for (auto &mesh : meshes())
	{
		if (mesh.get() == mesh_ptr.get())
		{
			joint_ptr->mesh = mesh;
			found = true;
			break;
		}
	}
	if (!found)
	{
		throw std::runtime_error("attach_mesh_to_joint: mesh not found in model's meshes vector");
	}

	return joint_ptr;
}

MeshBuilderPtr ModelBuilder::add_geometry(std::vector<MeshBuilder::vertex_type> const &vertices,
	std::vector<MeshBuilder::index_type> const &indices,
	std::string const &mesh_name,
	MaterialPtr const &material_ptr)
{
	MeshBuilderPtr mesh_ptr = add_mesh_builder(mesh_name, mesh_name);
	mesh_ptr->set_geometry(vertices, indices);
	if (!indices.empty())
	{
		if (indices.size() % 3 != 0)
		{
			throw std::runtime_error("add_geometry requires triangle-list indices (count must be divisible by 3)");
		}
		mesh_ptr->add_triangles(material_ptr, 0, indices.size() / 3);
	}

	if (material_ptr)
	{
		if (std::find(materials().begin(), materials().end(), material_ptr) == materials().end())
		{
			materials_.push_back(material_ptr);
		}
	}

	attach_mesh_to_joint(mesh_ptr);
	return mesh_ptr;
}

MeshPtr ModelBuilder::add_mesh()
{
	meshes_.push_back(std::make_shared<MeshBuilder>(this));
	return meshes().back();
}

void ModelBuilder::draw(CameraPtr const &, rove::matrix<4,4> const &, unsigned)
{
	throw std::runtime_error("ModelBuilder cannot render. Save the model with save_to_file() "
		"and load it with StaticModel/AnimatedModel for rendering.");
}

}
