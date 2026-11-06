// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "animated_mesh_cache.h"

#include <stdexcept>
#include <string>

namespace edge
{

AnimatedMeshCache::AnimatedMeshCache(std::vector<rove::aabb<3>> influence_boxes):
	influence_boxes_(std::move(influence_boxes))
{
}

CachedPose const *AnimatedMeshCache::find(PoseKey key) const
{
	auto it = poses_.find(key);
	return it == poses_.end() ? nullptr : &it->second;
}

CachedPose const &AnimatedMeshCache::insert(PoseKey key, CachedPose pose)
{
	auto inserted = poses_.emplace(key, std::move(pose));
	if (!inserted.second)
	{
		throw std::logic_error("AnimatedMeshCache: pose (" + std::to_string(key.animation) + ", " + std::to_string(key.tick) + ") is stored already");
	}
	return inserted.first->second;
}

rove::aabb<3> AnimatedMeshCache::bounds_in(std::vector<rove::matrix<4,4>> const &palette) const
{
	if (palette.size() != influence_boxes_.size())
	{
		throw std::invalid_argument("AnimatedMeshCache::bounds_in: " + std::to_string(palette.size()) + " matrices for " +
			std::to_string(influence_boxes_.size()) + " skin joints");
	}
	rove::aabb<3> bounds;
	bounds.null();
	for (std::size_t j = 0; j < palette.size(); ++j)
	{
		rove::aabb<3> const &box = influence_boxes_[j];
		if (box.lo.i[0] > box.hi.i[0])
		{
			continue;      // pulls nothing
		}
		for (int corner = 0; corner < 8; ++corner)
		{
			rove::vec<3> const p((corner & 1) ? box.hi.i[0] : box.lo.i[0], (corner & 2) ? box.hi.i[1] : box.lo.i[1],
				(corner & 4) ? box.hi.i[2] : box.lo.i[2]);
			bounds.extend(rove::transform_point(palette[j], p));
		}
	}
	return bounds;
}

}
