// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include "tolerance.h"
#include "mesh.h"
#include "weights_sampler.h"

namespace edge
{

void WeightsSampler::add_sample(rove::scalar time, std::vector<float> const &weights)
{
	Sample s(time);
	s.weights = weights;
	samples_.push_back(s);
}

std::vector<float> WeightsSampler::get(rove::scalar time) const
{
	if (samples_.empty())
	{
		return std::vector<float>();
	}

	if (time <= samples_.front().time)
	{
		return samples_.front().weights;
	}

	if (time >= samples_.back().time)
	{
		return samples_.back().weights;
	}

	samples_type::const_iterator it = std::lower_bound(
		samples_.begin(),
		samples_.end(),
		Sample(time),
		[](Sample const &left, Sample const &right)
		{
			return left.time < right.time;
		});

	if (it == samples_.begin())
	{
		return it->weights;
	}

	Sample const &left = *(it - 1);
	Sample const &right = *it;
	if (interpolation_ == Interpolation::STEP || right.time <= left.time + SAME_TIME)
	{
		return left.weights;
	}

	rove::scalar const k = (time - left.time) / (right.time - left.time);
	rove::scalar const inv_k = 1.0f - k;

	std::size_t const components = std::max(left.weights.size(), right.weights.size());
	std::vector<float> result(components, 0.0f);
	for (std::size_t i = 0; i < components; ++i)
	{
		float const a = i < left.weights.size() ? left.weights[i] : 0.0f;
		float const b = i < right.weights.size() ? right.weights[i] : 0.0f;
		result[i] = a * inv_k + b * k;
	}

	return result;
}

void WeightsSampler::apply(rove::scalar t) const
{
	if (MeshPtr mesh_ptr = mesh_.lock())
	{
		mesh_ptr->set_morph_weights(get(t));
	}
}

}

