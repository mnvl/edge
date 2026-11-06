// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "load_model.h"

#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>

#include "animated_model.h"
#include "serialized_model.h"
#include "static_model.h"

namespace edge
{

namespace
{

/// A model of the type whose meshes are @p mesh_type; none for a type nothing loads.
ModelPtr model_of_type(std::string const &mesh_type, RendererPtr const &renderer, SchedulerPtr const &scheduler)
{
	if (mesh_type == BasicStaticMesh<VERTEX_PTBNT>::class_serialization_type())
		return std::make_shared<StaticModelPTBNT>(renderer, scheduler);
	if (mesh_type == BasicStaticMesh<VERTEX_PTBNTC>::class_serialization_type())
		return std::make_shared<StaticModelPTBNTC>(renderer, scheduler);
	if (mesh_type == BasicStaticMesh<VERTEX_PNT>::class_serialization_type())
		return std::make_shared<StaticModelPNT>(renderer, scheduler);
	if (mesh_type == BasicAnimatedMesh<VERTEX_PTBNJWT>::class_serialization_type())
		return std::make_shared<AnimatedModelPTBNJWT>(renderer, scheduler);
	if (mesh_type == BasicAnimatedMesh<VERTEX_PTBNJWTC>::class_serialization_type())
		return std::make_shared<AnimatedModelPTBNJWTC>(renderer, scheduler);
	if (mesh_type == BasicAnimatedMesh<VERTEX_PNJWT>::class_serialization_type())
		return std::make_shared<AnimatedModelPNJWT>(renderer, scheduler);
	return ModelPtr();
}

}

ModelPtr load_model(RendererPtr const &renderer, SchedulerPtr const &scheduler, std::string const &filename, GltfOptions const &options)
{
	std::string const extension = std::filesystem::path(filename).extension().string();
	if (extension == ".edge")
	{
		SerializedModel const serialized = Model::read_file(filename);
		std::set<std::string> types;
		for (SerializedMesh const &mesh : serialized.meshes)
			types.insert(mesh.serialization_type);
		if (types.size() != 1)
		{
			std::string names;
			for (std::string const &type : types)
				names += (names.empty() ? "" : ", ") + type;
			throw std::runtime_error("model file " + filename + " has meshes of " + std::to_string(types.size()) + " types (" + names +
				"): a model is of one");
		}
		ModelPtr const model = model_of_type(*types.begin(), renderer, scheduler);
		if (!model)
			throw std::runtime_error("model file " + filename + " has meshes of type " + *types.begin() + ", which no model of edge's loads");
		try
		{
			model->load(serialized, options.keep_cpu_copy);
		}
		catch (std::runtime_error const &e)
		{
			throw std::runtime_error("model file " + filename + ": " + e.what());
		}
		return model;
	}
	if (extension == ".glb" || extension == ".gltf")
	{
		GltfContents const contents = gltf_contents(filename);
		ModelPtr model;
		if (contents.skin)
			model = std::make_shared<AnimatedModelPTBNJWT>(renderer, scheduler);
		else if (contents.vertex_colors)
			model = std::make_shared<StaticModelPTBNTC>(renderer, scheduler);
		else
			model = std::make_shared<StaticModelPTBNT>(renderer, scheduler);
		model->load_from_gltf(filename, options);
		return model;
	}
	throw std::invalid_argument("load_model: " + filename + " is neither a .edge file nor a glTF (.glb, .gltf)");
}

}
