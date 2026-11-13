// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "model.h"
#include "static_mesh.h"
#include "renderer.h"

namespace edge {

template<vertex_layout V = VERTEX_PNT>
class BasicStaticModel: public Model {
public:
	BasicStaticModel(RendererPtr const &renderer_ptr, SchedulerPtr const &scheduler_ptr);
	/// An instance of @p prototype; see Model(Model const &, SchedulerPtr const &).
	BasicStaticModel(BasicStaticModel const &prototype, SchedulerPtr const &scheduler_ptr):
		Model(prototype, scheduler_ptr) {}
	/// An instance holding @p prototype; see Model(std::shared_ptr<Model const> const &, SchedulerPtr const &).
	BasicStaticModel(std::shared_ptr<BasicStaticModel const> const &prototype, SchedulerPtr const &scheduler_ptr):
		Model(std::shared_ptr<Model const>(prototype), scheduler_ptr) {}
	~BasicStaticModel() override;

	MeshPtr add_mesh() override;
	std::string mesh_serialization_type() const override { return BasicStaticMesh<V>::class_serialization_type(); }

};

using StaticModelPNT = BasicStaticModel<VERTEX_PNT>;
using StaticModelPTBNT = BasicStaticModel<VERTEX_PTBNT>;
using StaticModelPTBNTC = BasicStaticModel<VERTEX_PTBNTC>;

}
