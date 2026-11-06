// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include <GLES3/gl3.h>

#include "gles_context.h"
#include "gpu_timer.h"

namespace edge
{

/// Per-pass GPU durations on OpenGL ES 3.0 and WebGL 2, by GL_EXT_disjoint_timer_query (EXT_disjoint_timer_query_webgl2 in a
/// browser): one time-elapsed query around each pass.  A frame's queries are read when they are there, never waited for: when the
/// renderer begins or ends a frame, oldest frame first (after finish_frame() on a desktop driver, whose glFinish() has them done; a few
/// frames later in a browser, which gives no result before the page's task returns).  GL_GPU_DISJOINT_EXT, set when the GPU's clock was
/// disturbed, makes every frame not yet read disjoint (GpuTimer::disjoint).  Frames still unread when HISTORY more are waiting are
/// given up (they never get durations).
///
/// Not available() without the extension, with a time-elapsed counter of no bits, or where the context gives no
/// glGetQueryObjectui64vEXT (GLESContext::proc_address; WebGL links it).
class GLESGpuTimer: public GpuTimer {
public:
	/// In @p context, current, its extensions loaded.
	explicit GLESGpuTimer(GLESContext &context);
	/// With the context current: deletes the queries.
	~GLESGpuTimer() override;

	/// The frame numbered @p frame begins; reads what earlier frames have.
	void begin_frame(uint64_t frame);
	/// Around the pass's GL commands.
	void begin_pass();
	void end_pass();
	/// The frame's commands are given (and, after glFinish(), done): its queries wait to be read; reads what is there.
	void end_frame();

	using GetQueryObjectui64v = void (*)(GLuint, GLenum, GLuint64 *);

protected:
	void start() override;
	void stop() override;
	/// Whether the GPU's clock was disturbed since this was last asked: GL_GPU_DISJOINT_EXT, which reading clears.
	virtual bool disjoint_happened();

private:
	GetQueryObjectui64v get_query_object_ui64v_ = nullptr;

	struct Frame
	{
		uint64_t number = 0;
		std::vector<GLuint> queries;      // one a pass, in begin_pass order
	};
	bool timed_ = false;                  // the frame recorded began with timing on
	bool in_pass_ = false;                // a query of the frame recorded is open
	Frame recording_;
	std::deque<Frame> pending_;           // ended, not read yet, oldest first
	std::vector<GLuint> free_;            // queries to use again

	GLuint take_query();
	void give_back(Frame &frame);
	/// Reads the frames whose queries are all done, oldest first, without waiting; all of them disjoint where the GPU says so.
	void poll();
};

}
