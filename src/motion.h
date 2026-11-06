// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>
#include <rove/matrix.h>

namespace edge
{

/// What places a draw, as far as where it was the frame before goes (motion vectors, DrawCall::shader_previous_world()): its world
/// matrix, its instances (none, or empty: not instanced), its joint palette (none, or empty: not skinned).  The lists are shared, not
/// copied, by what keeps or hands on a placement.
struct Placement
{
	using Matrices = std::shared_ptr<const std::vector<rove::matrix<4, 4>>>;

	rove::matrix<4,4> world;
	Matrices instances;
	Matrices joints;

	bool instanced() const { return instances && !instances->empty(); }
	std::size_t joint_count() const { return joints ? joints->size() : 0; }
	/// Bit for bit.
	bool operator==(Placement const &other) const;
	/// Whether this can be where @p other is a frame later: both instanced or neither, palettes of one length.
	bool follows(Placement const &other) const;
};

/// Where one thing was placed in the frames it was used in, the last FRAMES of them and the one of its last use (each a frame of its
/// renderer, Renderer::frame_index()).  A thing used again in a frame at the same placement (another pass, another camera) is the
/// same; used at two placements in a frame, it is two things, and where either was in that frame is not known.
class PlacementHistory
{
public:
	/// The frames before the one of the last use that are kept: a use whose frame before is older than they finds none.
	static constexpr std::size_t FRAMES = 4;

	/// A use in @p frame at @p placement, the frame before being @p before (the camera's, Renderer::previous_frame(); none: there is
	/// none): the placement of the use in @p before, or none (nullptr) where it is not known: not used in @p before, or not among the
	/// frames kept, used there at two placements, reset since, or placed otherwise (Placement::follows()); and none for a use at a
	/// placement other than its frame's first.  Valid until the next use or reset.
	Placement const *use(std::uint64_t frame, std::optional<std::uint64_t> before, Placement placement);
	/// From the next use on, as if never used.
	void reset() { frames_.clear(); }

private:
	struct Frame
	{
		std::uint64_t frame;
		Placement placement;          // its first use's
		bool several;                 // it had uses at more than one placement
	};
	std::deque<Frame> frames_;        // oldest first; the last one's is the frame of the last use
};

}
