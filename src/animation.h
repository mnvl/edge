// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vector>
#include <memory>
#include "forward.h"
#include "sampler.h"
#include "weights_sampler.h"

namespace edge
{

class Model;

struct Animation {
	using samplers_type = std::vector<Sampler>;
	using weight_samplers_type = std::vector<WeightsSampler>;

	std::string name;
	samplers_type samplers;
	weight_samplers_type weight_samplers;

	/// Owning model, set by Model::load_from_gltf() / load_from_file().
	/// Used by pose() to trigger a joint hierarchy rebuild after applying keyframes.
	/// Not copied — a copied Animation is unbound until explicitly added to a model.
	std::weak_ptr<Model> model;

	Animation() = default;
	/// Copy constructor intentionally leaves @c model empty;
	/// the copy is unbound until it is added to a model.
	Animation(Animation const &other):
		name(other.name),
		samplers(other.samplers),
		weight_samplers(other.weight_samplers)
	{}

	/// How long the clip runs (seconds): from 0 to the last key of any of its samplers.
	rove::scalar duration() const {
		rove::scalar d = 0;
		for (auto const &s : samplers) d = std::max(d, s.duration());
		for (auto const &w : weight_samplers) d = std::max(d, w.duration());
		return d;
	}

	/// Evaluates all samplers at time @p t, writing the results into the joints' local transforms (and the meshes' morph weights), and
	/// builds nothing.  pose() is this and a rebuild of the joints.
	void apply(rove::scalar t);

	/// Evaluates all samplers at time @p t, writing the results into the joints'
	/// local transforms, then calls Model::rebuild_joints() to propagate those
	/// transforms through the joint hierarchy.
	/// @param t  Animation time in seconds (not clamped or looped; caller is responsible).
	void pose(rove::scalar t);
};

}
