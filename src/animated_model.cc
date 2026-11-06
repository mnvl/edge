// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "animated_mesh.h"
#include "animated_model.h"

namespace edge
{

template <skinned_vertex_layout V>
BasicAnimatedModel<V>::BasicAnimatedModel(RendererPtr const &renderer_ptr, SchedulerPtr const &scheduler_ptr):
	Model(renderer_ptr, scheduler_ptr)
{
}

template <skinned_vertex_layout V>
BasicAnimatedModel<V>::~BasicAnimatedModel() = default;

template <skinned_vertex_layout V>
MeshPtr BasicAnimatedModel<V>::add_mesh()
{
	meshes_.push_back(std::make_shared<BasicAnimatedMesh<V>>(this));
	return meshes().back();
}

template <skinned_vertex_layout V>
void BasicAnimatedModel<V>::compute_pose_bounds(float margin)
{
	std::vector<BasicAnimatedMesh<V> *> skinned;
	for (MeshPtr const &mesh : meshes())
	{
		if (auto *animated = dynamic_cast<BasicAnimatedMesh<V> *>(mesh.get()))
		{
			skinned.push_back(animated);
		}
	}
	if (skinned.empty())
	{
		return;
	}

	// The joints go back where they are when this is done.
	std::vector<std::vector<rove::matrix<4,4>>> bind;
	for (JointPtr const &joint : joints())
	{
		bind.emplace_back();
		for (Joint::Transform const &t : joint->transforms)
		{
			bind.back().push_back(t.matrix);
		}
	}

	struct Walk
	{
		rove::aabb<3> box;
		std::vector<rove::vec<3>> previous;
		rove::scalar step = 0;           // the farthest a vertex has moved between two successive samples of a clip
	};
	std::vector<Walk> walks(skinned.size());
	for (Walk &walk : walks)
	{
		walk.box.null();
	}

	auto sample = [&](bool first_of_clip) {
		rebuild_joints();
		for (size_t m = 0; m < skinned.size(); ++m)
		{
			auto const positions = skinned[m]->skin(joints());
			Walk &walk = walks[m];
			for (size_t i = 0; i < positions.size(); ++i)
			{
				walk.box.extend(positions[i].position);
				if (!first_of_clip)
				{
					walk.step = std::max(walk.step, (positions[i].position - walk.previous[i]).length());
				}
			}
			walk.previous.resize(positions.size());
			for (size_t i = 0; i < positions.size(); ++i)
			{
				walk.previous[i] = positions[i].position;
			}
		}
	};

	sample(true);
	for (AnimationPtr const &clip : animations())
	{
		// from time 0 (before its first key a clip holds that key's pose) to its last key
		rove::scalar const end = clip->duration();
		int const steps = std::max(1, int(std::ceil(end * POSE_SAMPLES_PER_SECOND)));
		for (int i = 0; i <= steps; ++i)
		{
			rove::scalar const t = i == steps ? end : end * rove::scalar(i) / rove::scalar(steps);
			for (Sampler const &sampler : clip->samplers)
			{
				sampler.apply(t);
			}
			sample(i == 0);
		}
	}

	for (size_t j = 0; j < joints().size(); ++j)
	{
		for (size_t t = 0; t < joints()[j]->transforms.size(); ++t)
		{
			joints()[j]->transforms[t].matrix = bind[j][t];
		}
	}
	rebuild_joints();

	for (size_t m = 0; m < skinned.size(); ++m)
	{
		rove::aabb<3> box = walks[m].box;
		if (box.lo.x > box.hi.x)
		{
			throw std::runtime_error("failed to compute bounds: no vertices in mesh '" + skinned[m]->name() + "'");
		}
		// a vertex on its way from one sample to the next is at most half the way from one of them, and the way is the step
		rove::scalar const half_step = walks[m].step / 2;
		rove::vec<3> const pad(half_step, half_step, half_step);
		box.lo -= pad;
		box.hi += pad;
		rove::vec<3> const room = (box.hi - box.lo) * margin;
		skinned[m]->set_pose_bounds(rove::aabb<3>(box.lo - room, box.hi + room));
	}
}

template class BasicAnimatedModel<VERTEX_PNJWT>;
template class BasicAnimatedModel<VERTEX_PTBNJWT>;
template class BasicAnimatedModel<VERTEX_PTBNJWTC>;

}
