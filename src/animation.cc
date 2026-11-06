// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "animation.h"
#include "model.h"

namespace edge
{

void Animation::apply(rove::scalar t)
{
	for (auto &sampler : samplers)
		sampler.apply(t);
	for (auto &weight_sampler : weight_samplers)
		weight_sampler.apply(t);
}

void Animation::pose(rove::scalar t)
{
	apply(t);
	if (auto m = model.lock())
		m->rebuild_joints();
}

}
