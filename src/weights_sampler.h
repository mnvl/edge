// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <vector>
#include <rove/scalar.h>
#include "forward.h"

namespace edge
{

class WeightsSampler
{
public:
	/// How the weights go from one key to the next (glTF's LINEAR and STEP).  (The values are those .edge files keep.)
	enum class Interpolation
	{
		LINEAR,
		STEP,
	};

private:
	struct Sample
	{
		rove::scalar time;
		std::vector<float> weights;

		Sample(rove::scalar t = 0):
			time(t)
		{
		}
	};

	using samples_type = std::vector<Sample>;

	MeshWeakPtr mesh_;
	Interpolation interpolation_;
	samples_type samples_;

public:
	WeightsSampler():
		interpolation_(Interpolation::LINEAR)
	{
	}

	void set_target(MeshPtr mesh_ptr) { mesh_ = mesh_ptr; }
	void set_interpolation(Interpolation interpolation) { interpolation_ = interpolation; }
	void add_sample(rove::scalar time, std::vector<float> const &weights);
	std::vector<float> get(rove::scalar time) const;

	void apply(rove::scalar t) const;

	/// From time 0 to the last key, as Sampler::duration().
	rove::scalar duration() const {
		if (samples_.empty()) return 0;
		return samples_.back().time;
	}

	Interpolation interpolation() const { return interpolation_; }
	/// The keyframes, in time order.
	std::size_t samples_count() const { return samples_.size(); }
	rove::scalar sample_time(std::size_t index) const { return samples_.at(index).time; }
	std::vector<float> const &sample_weights(std::size_t index) const { return samples_.at(index).weights; }
	MeshPtr mesh() const { return mesh_.lock(); }
	void set_mesh(MeshPtr const &mesh_ptr) { mesh_ = mesh_ptr; }
};

}
