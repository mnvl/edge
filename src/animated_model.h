// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "animated_mesh.h"
#include "model.h"

namespace edge
{

class Mesh;
class Renderer;

template<skinned_vertex_layout V = VERTEX_PNJWT>
class BasicAnimatedModel: public Model {
public:
	BasicAnimatedModel(RendererPtr const &renderer_ptr, SchedulerPtr const &scheduler_ptr);
	/// An instance of @p prototype; see Model(Model const &, SchedulerPtr const &).
	BasicAnimatedModel(BasicAnimatedModel const &prototype, SchedulerPtr const &scheduler_ptr):
		Model(prototype, scheduler_ptr) {}
	/// An instance holding @p prototype; see Model(std::shared_ptr<Model const> const &, SchedulerPtr const &).
	BasicAnimatedModel(std::shared_ptr<BasicAnimatedModel const> const &prototype, SchedulerPtr const &scheduler_ptr):
		Model(std::shared_ptr<Model const>(prototype), scheduler_ptr) {}
	~BasicAnimatedModel() override;

	MeshPtr add_mesh() override;
	std::string mesh_serialization_type() const override { return BasicAnimatedMesh<V>::class_serialization_type(); }

	/// The pose box of every skinned mesh (BasicAnimatedMesh::pose_bounds()): the union of its vertices in the bind pose and at
	/// 60 samples a second of every clip from time 0 to its last key (the joints are left as they were), grown by half the farthest
	/// any vertex moves between two samples, then by @p margin of its size on each side.  Throws std::runtime_error for a mesh with no
	/// vertices or a vertex weighted to a skin joint the mesh does not have.
	void compute_pose_bounds(float margin = 0) override;

private:
	/// How often compute_pose_bounds() poses the model along a clip.
	static constexpr int POSE_SAMPLES_PER_SECOND = 60;
};

using AnimatedModelPNJWT = BasicAnimatedModel<VERTEX_PNJWT>;
using AnimatedModelPTBNJWT = BasicAnimatedModel<VERTEX_PTBNJWT>;
using AnimatedModelPTBNJWTC = BasicAnimatedModel<VERTEX_PTBNJWTC>;

}
