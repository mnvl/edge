// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "gles_gpu_timer.h"

#include <stdexcept>
#include <string>

#ifdef __EMSCRIPTEN__
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2ext.h>
#endif

#include "log.h"

// GL_EXT_disjoint_timer_query's names (GLES2/gl2ext.h has them; WebGL 2's extension the same)
#ifndef GL_TIME_ELAPSED_EXT
#define GL_TIME_ELAPSED_EXT 0x88BF
#endif
#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif
#ifndef GL_QUERY_COUNTER_BITS_EXT
#define GL_QUERY_COUNTER_BITS_EXT 0x8864
#endif

namespace edge
{

namespace
{

#ifdef __EMSCRIPTEN__
char const *const TIMER_EXTENSION = "GL_EXT_disjoint_timer_query_webgl2";
#else
char const *const TIMER_EXTENSION = "GL_EXT_disjoint_timer_query";
#endif

/// The function that reads a query's 64 bits, none where @p context gives none.
GLESGpuTimer::GetQueryObjectui64v query_getter(GLESContext &context)
{
#ifdef __EMSCRIPTEN__
	(void)context;
	return &glGetQueryObjectui64vEXT;
#else
	return reinterpret_cast<GLESGpuTimer::GetQueryObjectui64v>(context.proc_address("glGetQueryObjectui64vEXT"));
#endif
}

/// Why @p context cannot time; empty when it can.
std::string timer_unavailable(GLESContext &context)
{
	if (!context.has_extension(TIMER_EXTENSION))
		return std::string("the OpenGL ES context has no ") + TIMER_EXTENSION;
	GLint bits = 0;
	glGetQueryiv(GL_TIME_ELAPSED_EXT, GL_QUERY_COUNTER_BITS_EXT, &bits);
	check_gl("reading the bits of a time-elapsed query");
	if (bits == 0)
		return "the OpenGL ES context's time-elapsed queries have no bits";
	if (!query_getter(context))
		return std::string("the OpenGL ES context lists ") + TIMER_EXTENSION + " and gives no glGetQueryObjectui64vEXT";
	return std::string();
}

std::string logged(std::string const &unavailable)
{
	if (!unavailable.empty())
		EDGE_LOG_INFO("GLES GPU timer: not available: {}", unavailable);
	return unavailable;
}

}

GLESGpuTimer::GLESGpuTimer(GLESContext &context):
	GpuTimer(logged(timer_unavailable(context)))
{
	if (available())
		get_query_object_ui64v_ = query_getter(context);
}

GLESGpuTimer::~GLESGpuTimer()
{
	give_back(recording_);
	for (Frame &frame : pending_)
		give_back(frame);
	if (!free_.empty())
		glDeleteQueries(static_cast<GLsizei>(free_.size()), free_.data());
}

void GLESGpuTimer::start()
{
	// a disjoint that happened while off belongs to no frame of ours: read, it is cleared
	disjoint_happened();
}

bool GLESGpuTimer::disjoint_happened()
{
	GLint disjoint = 0;
	glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
	check_gl("reading GL_GPU_DISJOINT_EXT");
	return disjoint != 0;
}

void GLESGpuTimer::stop()
{
	for (Frame &frame : pending_)
		give_back(frame);
	pending_.clear();
}

GLuint GLESGpuTimer::take_query()
{
	if (free_.empty())
	{
		GLuint query = 0;
		glGenQueries(1, &query);
		return query;
	}
	GLuint const query = free_.back();
	free_.pop_back();
	return query;
}

void GLESGpuTimer::give_back(Frame &frame)
{
	free_.insert(free_.end(), frame.queries.begin(), frame.queries.end());
	frame.queries.clear();
}

void GLESGpuTimer::begin_frame(uint64_t frame)
{
	poll();
	give_back(recording_);
	recording_.number = frame;
	timed_ = enabled();
	in_pass_ = false;
}

void GLESGpuTimer::begin_pass()
{
	if (!timed_ || recording_.queries.size() >= MAX_PASSES)
		return;
	GLuint const query = take_query();
	glBeginQuery(GL_TIME_ELAPSED_EXT, query);
	recording_.queries.push_back(query);
	in_pass_ = true;
}

void GLESGpuTimer::end_pass()
{
	if (!in_pass_)
		return;
	glEndQuery(GL_TIME_ELAPSED_EXT);
	in_pass_ = false;
}

void GLESGpuTimer::end_frame()
{
	if (!timed_)
		return;
	timed_ = false;
	// timed when it began and turned off since (between frames only): nothing is kept of it
	if (!enabled())
	{
		give_back(recording_);
		return;
	}
	pending_.push_back(std::move(recording_));
	recording_ = Frame();
	while (pending_.size() > HISTORY)
	{
		give_back(pending_.front());
		pending_.pop_front();
	}
	poll();
}

// Done is asked of each query, then GL_GPU_DISJOINT_EXT, then the times are read (the extension's order): a disjoint read after the
// results are there covers them.
void GLESGpuTimer::poll()
{
	if (pending_.empty())
		return;
	size_t done = 0;
	for (; done < pending_.size(); ++done)
	{
		bool all = true;
		for (GLuint query : pending_[done].queries)
		{
			GLuint available = GL_FALSE;
			glGetQueryObjectuiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
			if (!available)
			{
				all = false;
				break;
			}
		}
		if (!all)
			break;
	}
	check_gl("polling the GPU timer's queries");
	if (disjoint_happened())
	{
		// every frame not read is suspect, those not done too: they never get durations
		for (Frame &frame : pending_)
		{
			record_disjoint(frame.number);
			give_back(frame);
		}
		pending_.clear();
		return;
	}
	for (size_t i = 0; i < done; ++i)
	{
		Frame &frame = pending_.front();
		std::vector<double> times;
		for (GLuint query : frame.queries)
		{
			GLuint64 ns = 0;
			get_query_object_ui64v_(query, GL_QUERY_RESULT, &ns);
			times.push_back(static_cast<double>(ns) / 1000.0);
		}
		check_gl("reading the GPU timer's queries");
		record(frame.number, std::move(times));
		give_back(frame);
		pending_.pop_front();
	}
}

}
