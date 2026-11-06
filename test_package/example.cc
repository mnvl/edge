// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Links edge and draws nothing: the null renderer needs no GPU.
#include <cstdio>
#include <memory>
#include <edge/camera.h>
#include <edge/null_renderer.h>
#include <edge/shader_dirs.h>
#include <rove/matrix.h>

int main()
{
	auto const renderer = std::make_shared<edge::NullRenderer>(64, 48);
	auto const camera = std::make_shared<edge::Camera>();
	rove::matrix<4, 4> world;
	world.identity();
	renderer->begin_frame();
	renderer->finish_frame();
	std::printf("edge: null renderer %dx%d\n", renderer->screen_width(), renderer->screen_height());
#ifdef EDGE_USE_VULKAN
	std::printf("edge: Vulkan shaders in %s\n", edge::vulkan_shader_dir().c_str());
#endif
	return 0;
}
