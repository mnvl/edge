// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <vector>
#include <rove/aabb.h>
#include <rove/matrix.h>
#include <rove/obb.h>
#include <rove/vec.h>
#include "forward.h"

namespace edge
{

class DynamicMesh;

/// Lines in the world, for seeing what a program does: segments, boxes, spheres and axes, each of a colour (r, g, b, a; blended where a
/// is under 1), drawn as flat strips `width` world units wide that face the camera, unlit, in one draw call for each colour.  They are
/// collected between draws: draw() queues what was collected and starts afresh.  Depth-tested like the rest of the scene; the draw
/// calls' priority is priority()'s.
class DebugLines
{
public:
	/// std::invalid_argument for no renderer or a width that is not more than zero.
	DebugLines(RendererPtr const &renderer, float width);
	~DebugLines();
	DebugLines(DebugLines const &) = delete;
	DebugLines &operator=(DebugLines const &) = delete;

	/// From a to b; a line of no length is nothing to draw.
	void line(rove::vec<3> const &a, rove::vec<3> const &b, rove::vec<4> const &colour);
	/// The 12 edges of the box
	void box(rove::aabb<3> const &bounds, rove::vec<4> const &colour);
	void box(rove::obb<3> const &bounds, rove::vec<4> const &colour);
	/// Three great circles, about each axis, of `segments` segments each (std::invalid_argument for fewer than 3)
	void sphere(rove::vec<3> const &centre, float radius, rove::vec<4> const &colour, int segments = 32);
	/// The frame's x, y and z axes from its origin, `length` long, red, green and blue
	void axes(rove::matrix<4, 4> const &frame, float length);

	/// Segments collected since the last draw
	std::size_t segments() const;
	/// Queues one draw call for each colour collected in @p scheduler, the strips turned to face @p camera, and starts afresh.
	void draw(Scheduler &scheduler, CameraPtr const &camera);
	void clear();
	void set_priority(int priority) { priority_ = priority; }

private:
	struct Segment
	{
		rove::vec<3> a, b;
	};
	struct Colour
	{
		std::vector<Segment> segments;
		std::shared_ptr<DynamicMesh> mesh;
	};

	RendererPtr renderer_;
	float width_;
	int priority_ = 0;
	std::map<rove::vec<4>, Colour> colours_;           // by colour, lexicographically (std::less of a rove vector)
};

}
