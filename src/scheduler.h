// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include "forward.h"

namespace edge
{

class Scheduler: public std::enable_shared_from_this<Scheduler> {
public:
	Scheduler(Scheduler const &) = delete;
	Scheduler &operator=(Scheduler const &) = delete;
	Scheduler(RendererPtr const &renderer);
	~Scheduler();

	/// The renderer this scheduler was made for; null once it is gone.
	RendererPtr renderer() const { return renderer_.lock(); }

	void add(DrawCallPtr const &draw_call);

	/// Replace the camera on all queued draw calls and re-commit them
	/// (recomputes view-projection and frustum).  Use this to render the
	/// same geometry from a different viewpoint without resubmitting
	/// draw calls.
	void change_draw_calls_camera(CameraPtr const &camera);

	/// Sort queued draw calls by priority, cull against each call's frustum,
	/// and execute the visible ones.
	/// @param clear  If true (the default), remove all draw calls from the
	///               queue after execution.  Pass false to keep them for a
	///               subsequent flush (e.g. rendering the same geometry to
	///               a second render target).
	void flush(bool clear = true);
	/// With @p sort, each flush draws a priority's opaque calls first, in the order they were added, then its blended ones (a material
	/// with alpha blending) farthest first by DrawCall::view_depth(), so that what is seen through glass is drawn before the glass.
	/// Off by default: a priority's calls are drawn in the order they were added.
	void set_sort_blended(bool sort) { sort_blended_ = sort; }
	bool sort_blended() const { return sort_blended_; }

	/// Remove all queued draw calls without executing them.
	void clear();

	size_t visible_calls_count() const { return visible_calls_count_; }
	size_t invisible_calls_count() const { return invisible_calls_count_; }

	/// Draw calls and triangles executed by every flush since this scheduler was made (the counts above are of the last
	/// flush only); an instanced call's triangles are counted for each instance drawn (offered: each given).  Read before and after a frame, the difference is what the frame pushed through all its passes.
	uint64_t executed_calls_count() const { return executed_calls_; }
	uint64_t executed_triangles_count() const { return executed_triangles_; }

	/// The same, for every call a flush was given, culled or not.  Executed / offered is what culling saved.
	uint64_t offered_calls_count() const { return offered_calls_; }
	uint64_t offered_triangles_count() const { return offered_triangles_; }

private:
	using draw_calls_type = std::vector<DrawCallPtr>;

	std::weak_ptr<Renderer> renderer_;

	size_t visible_calls_count_, invisible_calls_count_;
	uint64_t executed_calls_ = 0, executed_triangles_ = 0, offered_calls_ = 0, offered_triangles_ = 0;

	draw_calls_type draw_calls_;
	bool sort_blended_ = false;
};

}
