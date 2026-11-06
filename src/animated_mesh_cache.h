// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>
#include <rove/aabb.h>
#include <rove/matrix.h>

namespace edge
{

/// A pose a model can be cached in: a clip, and the hundredth of a second of it (floor(time * 100)).
struct PoseKey
{
	std::uint32_t animation;
	std::uint32_t tick;

	/// The time the pose is sampled at: the same for every instance that asks for the key, whatever time it had.
	rove::scalar time() const { return rove::scalar(tick) * rove::scalar(0.01); }
	bool operator<(PoseKey const &o) const { return animation != o.animation ? animation < o.animation : tick < o.tick; }
	bool operator==(PoseKey const &o) const { return animation == o.animation && tick == o.tick; }
};

/// What a skinned mesh has in a pose.
struct CachedPose
{
	/// The matrices vertex_skin.glsl is handed, one per skin joint.
	std::vector<rove::matrix<4,4>> palette;
	/// A model-space box that holds the mesh in the pose: AnimatedMeshCache::bounds_in().
	rove::aabb<3> bounds;
};

/// The poses of one skinned mesh, shared by every copy of it (a model copied from a ModelCache's prototype): a pose is sampled and worked
/// out once, however many instances stand in it.  A key is only ever added, never replaced; the keys are finite (clips are short and a
/// tick is a hundredth of a second), so nothing is evicted.  The instances that share it are their renderer's, of one thread at a time
/// (README: Threads): nothing here locks.
class AnimatedMeshCache {
public:
	/// @p influence_boxes: for each skin joint, the box of the vertices it pulls (those it gives weight to), in the mesh's vertex space; a
	/// joint that pulls none has a null box.
	explicit AnimatedMeshCache(std::vector<rove::aabb<3>> influence_boxes);

	/// The pose under @p key, or null.
	CachedPose const *find(PoseKey key) const;
	/// Stores @p pose under @p key.  Throws std::logic_error if the key has one already.
	CachedPose const &insert(PoseKey key, CachedPose pose);
	std::size_t size() const { return poses_.size(); }

	/// A box that holds every vertex of the mesh skinned with @p palette: a vertex is a blend of its joints' images of it, so it is inside
	/// the hull of those, and each joint's images of its vertices are inside its influence box through its matrix.  Not the tightest box
	/// (a joint's whole box is taken for every vertex it pulls), the cost of a joint's worth of corners and not of a vertex's worth.
	/// Throws std::invalid_argument for a palette of another size than the skin.
	rove::aabb<3> bounds_in(std::vector<rove::matrix<4,4>> const &palette) const;

private:
	std::vector<rove::aabb<3>> const influence_boxes_;
	std::map<PoseKey, CachedPose> poses_;
};

}
