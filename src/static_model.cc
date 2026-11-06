// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "static_model.h"

namespace edge
{

template <vertex_layout V>
BasicStaticModel<V>::BasicStaticModel(RendererPtr const &renderer_ptr, SchedulerPtr const &scheduler_ptr):
	Model(renderer_ptr, scheduler_ptr)
{
}

template <vertex_layout V>
BasicStaticModel<V>::~BasicStaticModel() = default;

template <vertex_layout V>
MeshPtr BasicStaticModel<V>::add_mesh()
{
	meshes_.push_back(std::make_shared<BasicStaticMesh<V>>(this));
	return meshes().back();
}

template class BasicStaticModel<VERTEX_PNT>;
template class BasicStaticModel<VERTEX_PTBNT>;
template class BasicStaticModel<VERTEX_PTBNTC>;

}
