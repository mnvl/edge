// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "motion.h"

#include <cstring>
#include <utility>

namespace edge
{

namespace
{

bool same_bits(Placement::Matrices const &a, Placement::Matrices const &b)
{
	std::size_t const n = a ? a->size() : 0, m = b ? b->size() : 0;
	return n == m && (n == 0 || std::memcmp(a->data(), b->data(), n * sizeof(rove::matrix<4,4>)) == 0);
}

}

bool Placement::operator==(Placement const &other) const
{
	return std::memcmp(&world, &other.world, sizeof(world)) == 0 && same_bits(instances, other.instances) && same_bits(joints, other.joints);
}

bool Placement::follows(Placement const &other) const
{
	return instanced() == other.instanced() && joint_count() == other.joint_count();
}

Placement const *PlacementHistory::use(std::uint64_t frame, std::optional<std::uint64_t> before, Placement placement)
{
	if (!frames_.empty() && frames_.back().frame == frame)
	{
		Frame &now = frames_.back();
		if (now.several || !(placement == now.placement))
		{
			now.several = true;
			return nullptr;
		}
	}
	else
	{
		frames_.push_back(Frame{frame, std::move(placement), false});
		while (frames_.size() > FRAMES + 1)
		{
			frames_.pop_front();
		}
	}
	if (!before)
	{
		return nullptr;
	}
	Placement const &now = frames_.back().placement;
	for (Frame const &then : frames_)
	{
		if (then.frame == *before)
		{
			return !then.several && now.follows(then.placement) ? &then.placement : nullptr;
		}
	}
	return nullptr;
}

}
