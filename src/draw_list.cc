// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <iterator>
#include <stdexcept>
#include <string>

#include "draw_list.h"

#include "draw_call.h"
#include "render_target.h"
#include "renderer.h"

namespace edge
{

DrawList::DrawList(Renderer &renderer):
	renderer_(renderer)
{
}

DrawList::~DrawList() = default;

void DrawList::set_viewport(int x, int y, int width, int height)
{
	RenderTarget const &target = *renderer_.pass_target();
	if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > target.width() || y + height > target.height())
	{
		throw std::out_of_range("viewport " + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(width) + " x " +
			std::to_string(height) + " is not inside the target of " + std::to_string(target.width()) + " x " +
			std::to_string(target.height()));
	}
	do_set_viewport(x, y, width, height);
}

void DrawList::draw(DrawCall const &call)
{
	renderer_.check_draw(call);
	do_draw(call);
	hold(call.vertex_buffer());
	if (call.index_buffer())
		hold(call.index_buffer());
	hold(call.material());
}

void DrawList::hand_over(std::vector<std::shared_ptr<void const>> &frame_resources)
{
	frame_resources.insert(frame_resources.end(), std::make_move_iterator(resources_.begin()), std::make_move_iterator(resources_.end()));
	resources_.clear();
	held_.clear();
}

}
