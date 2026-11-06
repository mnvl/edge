// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "frame_presenter.h"

#include <cstring>
#include <stdexcept>
#include <string>

#include "pixel.h"
#include "render_target.h"
#include "texture.h"
#include "vulkan_renderer.h"

namespace edge
{

namespace
{

VulkanRendererPtr const &not_null(VulkanRendererPtr const &renderer)
{
	if (!renderer)
		throw std::invalid_argument("FramePresenter: no renderer");
	return renderer;
}

}

FramePresenter::FramePresenter(VulkanRendererPtr const &renderer):
	renderer_(not_null(renderer)),
	copy_(renderer)
{
}

bool FramePresenter::present(TexturePtr const &frame, int window_width, int window_height, std::function<void()> const &over)
{
	if (!frame)
		throw std::invalid_argument("FramePresenter::present: no frame");
	if (window_width <= 0 || window_height <= 0)
		throw std::invalid_argument("FramePresenter::present: a window of " + std::to_string(window_width) + " x " +
			std::to_string(window_height) + " pixels");
	if (!target_ || target_->width() != window_width || target_->height() != window_height)
		target_ = renderer_->create_render_target(window_width, window_height, PixelFormat::A8R8G8B8);
	renderer_->begin_frame();
	try
	{
		renderer_->begin_pass(target_);
		copy_.draw(frame);
		if (over)
			over();
		renderer_->end_pass();
	}
	catch (...)
	{
		if (renderer_->in_pass())
			renderer_->end_pass();
		renderer_->finish_frame();
		throw;
	}
	renderer_->finish_frame();          // (the copy into the window comes after it on the GPU: neither is waited for)
	return renderer_->present(target_->color_texture(), window_width, window_height);
}

bool FramePresenter::present(void const *bgrx, int width, int height, int window_width, int window_height)
{
	if (!bgrx)
		throw std::invalid_argument("FramePresenter::present: no bytes");
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("FramePresenter::present: a frame of " + std::to_string(width) + " x " + std::to_string(height) + " pixels");
	if (window_width <= 0 || window_height <= 0)
		throw std::invalid_argument("FramePresenter::present: a window of " + std::to_string(window_width) + " x " +
			std::to_string(window_height) + " pixels");
	if (!uploaded_ || uploaded_->width() != width || uploaded_->height() != height)
		uploaded_ = renderer_->create_texture(width, height, PixelFormat::X8R8G8B8);
	auto texels = uploaded_->write(Contents::DISCARD);
	std::memcpy(texels.data(), bgrx, size_t(width) * size_t(height) * 4);
	texels.unmap();
	return present(uploaded_, window_width, window_height);
}

TexturePtr FramePresenter::picture() const
{
	if (!target_)
		throw std::logic_error("FramePresenter::picture: nothing was presented");
	return target_->color_texture();
}

}
