// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "unistd.h"
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "renderer.h"
#include "scheduler.h"

namespace edge
{

/// What the tests share: where edge's own files are (test/, the shaders), the source tree the build was made from.
struct TestFixture {
	static std::string graphic_sources_path()
	{
		return std::string(EDGE_ROOT) + "/";
	}
};

/// The SPIR-V of a test's own shader, test/shaders/<name> (a .vert or a .frag) built by edge_add_shaders: a path the renderers take
/// as it is (and GLES finds the translation beside).
inline std::string test_shader(std::string const &name)
{
	return std::string(EDGE_TEST_SHADER_DIR) + "/" + name + ".spv";
}

/// A new directory of the test's own under the temporary one, <prefix>XXXXXX made unique (mkdtemp): never one another run of the same
/// test, in another build or process, writes into or removes at the same time.  std::runtime_error when it cannot be made.
inline std::filesystem::path unique_temp_directory(std::string const &prefix)
{
	std::string pattern = (std::filesystem::temp_directory_path() / (prefix + "XXXXXX")).string();
	if (!mkdtemp(pattern.data()))
		throw std::runtime_error("no directory " + pattern + " for the test's files");
	return pattern;
}

/// Flushes @p scheduler as a frame does: in a pass of its renderer, into the default target.
inline void flush_in_a_pass(SchedulerPtr const &scheduler, bool clear = true)
{
	RendererPtr const renderer = scheduler->renderer();
	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	scheduler->flush(clear);
	renderer->end_pass();
	renderer->finish_frame();
}

}
