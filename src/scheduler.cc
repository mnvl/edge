// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "renderer.h"
#include "scheduler.h"

namespace edge
{

Scheduler::Scheduler(std::shared_ptr<Renderer> const &renderer):
	renderer_(renderer)
{
	draw_calls_.reserve(4096);
}

Scheduler::~Scheduler() = default;

void Scheduler::add(std::shared_ptr<DrawCall> const &draw_call)
{
	draw_calls_.push_back(draw_call);
}

void Scheduler::change_draw_calls_camera(std::shared_ptr<Camera> const &camera)
{
	for (auto &dc : draw_calls_)
	{
		dc->set_camera(camera);
		dc->commit();
	}
}

void Scheduler::flush(bool clear)
{
	flush_into(nullptr, clear);
}

void Scheduler::flush(DrawList &list, bool clear)
{
	flush_into(&list, clear);
}

void Scheduler::flush_into(DrawList *list, bool clear)
{
	visible_calls_count_ = invisible_calls_count_ = 0;

	// By priority, and within one priority in the order the calls were
	// added.  Never by anything address-like: the order two coplanar
	// surfaces are drawn in decides which one a pixel shows, so an order
	// that follows the heap makes the same scene render differently run to
	// run.
	std::stable_sort(draw_calls_.begin(), draw_calls_.end(),
		[](std::shared_ptr<DrawCall> const &a, std::shared_ptr<DrawCall> const &b) { return a->priority() < b->priority(); });
	if (sort_blended_)
	{
		for (draw_calls_type::iterator run = draw_calls_.begin(); run != draw_calls_.end();)
		{
			int const priority = (*run)->priority();
			draw_calls_type::iterator const end = std::find_if(run, draw_calls_.end(),
				[priority](std::shared_ptr<DrawCall> const &dc) { return dc->priority() != priority; });
			draw_calls_type::iterator const blended = std::stable_partition(run, end,
				[](std::shared_ptr<DrawCall> const &dc) { return !dc->material()->alpha_blend_enabled(); });
			std::stable_sort(blended, end,
				[](std::shared_ptr<DrawCall> const &a, std::shared_ptr<DrawCall> const &b) { return a->view_depth() > b->view_depth(); });
			run = end;
		}
	}

	for(auto const &draw_call : draw_calls_)
	{
		++offered_calls_;
		offered_triangles_ += draw_call->triangles_count() * draw_call->offered_instance_count();
		if (draw_call->is_visible())
		{
			++visible_calls_count_;
			++executed_calls_;
			executed_triangles_ += draw_call->triangles_count() * draw_call->instance_count();
			if (list)
				draw_call->execute(*list);
			else
				draw_call->execute();
		}
		else
		{
			++invisible_calls_count_;
		}
	}

	if (clear)
	{
		draw_calls_.clear();
	}
}

void Scheduler::clear()
{
	draw_calls_.clear();
}

}
