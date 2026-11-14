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
	explicit BasicStaticModel(RendererPtr const &renderer_ptr);
	/// An instance holding @p prototype; see Model(std::shared_ptr<Model const> const &).
	explicit BasicStaticModel(std::shared_ptr<BasicStaticModel const> const &prototype):
		Model(std::shared_ptr<Model const>(prototype)) {}
	~BasicStaticModel() override;

	MeshPtr add_mesh() override;
	std::string mesh_serialization_type() const override { return BasicStaticMesh<V>::class_serialization_type(); }

};

using StaticModelPNT = BasicStaticModel<VERTEX_PNT>;
using StaticModelPTBNT = BasicStaticModel<VERTEX_PTBNT>;
using StaticModelPTBNTC = BasicStaticModel<VERTEX_PTBNTC>;

}
