// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <unordered_set>
#include <vector>
#include "forward.h"

namespace edge
{

/// A part of a pass: its viewport and its draws, drawn after those of the lists before it.  Renderer::begin_pass() gives a pass its
/// lists (one unless it asks for more) and end_pass() takes them back: a list is the renderer's, and used only inside its pass.
/// One thread at a time uses a list; a pass's lists are used by different threads at once where the renderer records on several
/// (Renderer::max_draw_lists()).  A list may remember what a material draws with for its later draws of it, and looks again when
/// the material has changed (Material::revision()): a material is not changed while another thread draws it.
class DrawList {
public:
	DrawList(DrawList const &) = delete;
	DrawList &operator=(DrawList const &) = delete;
	virtual ~DrawList();

	/// This list's draws from now on go to the rectangle of the pass's target x, y (from its top left corner), w x h pixels, and
	/// nowhere else (scissored): several views in one target, a shadow atlas.  A list starts with the whole target.
	/// std::out_of_range for a rectangle not inside the target.
	void set_viewport(int x, int y, int width, int height);

protected:
	explicit DrawList(Renderer &renderer);
	/// The rectangle is inside the pass's target.
	virtual void do_set_viewport(int x, int y, int width, int height) = 0;
	/// @p call committed, of this list's renderer, and checked as Renderer checks a draw.
	virtual void do_draw(DrawCall const &call) = 0;

private:
	friend class DrawCall;
	friend class Renderer;
	/// Checks and draws @p call in the pass; DrawCall::execute() gives it here once committed.
	void draw(DrawCall const &call);
	/// The pass has ended: what the list held is @p frame_resources' now, and the list holds nothing for its next pass.
	void hand_over(std::vector<std::shared_ptr<void const>> &frame_resources);

	Renderer &renderer_;
	/// What the list's draws draw with, each once: the frame's to hold once the pass ends (Renderer::end_pass).
	std::vector<std::shared_ptr<void const>> resources_;
	std::unordered_set<void const *> held_;
	template<class T> void hold(std::shared_ptr<T> const &resource)
	{
		// (once a list: a copy for every draw is a count every thread drawing the same buffer or material fights over)
		if (held_.insert(resource.get()).second)
			resources_.push_back(resource);
	}
};

/// The one list of a renderer that records a pass on one thread, a member of @p R: its viewport and draws are R's own,
/// R::do_set_viewport() and R::do_draw() (R befriends it).
template<class R> class InlineDrawList final: public DrawList {
public:
	explicit InlineDrawList(R &renderer): DrawList(renderer), renderer_(renderer) {}

private:
	R &renderer_;
	void do_set_viewport(int x, int y, int width, int height) override { renderer_.do_set_viewport(x, y, width, height); }
	void do_draw(DrawCall const &call) override { renderer_.do_draw(call); }
};

}
