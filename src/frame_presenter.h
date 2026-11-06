// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <functional>
#include <memory>

#include "forward.h"
#include "fullscreen_copy.h"

namespace edge
{

/// Shows frames in the window of a VulkanRenderer's surface (attach_surface; vulkan_sdl_surface.h for an SDL window): the frame
/// stretched over a window-sized target (FullscreenCopy), what the caller draws over it (a UI), that target presented.  Each present
/// is a frame of the renderer's own, outside any other.
class FramePresenter
{
public:
	/// std::invalid_argument for no renderer.
	explicit FramePresenter(VulkanRendererPtr const &renderer);

	/// Shows @p frame stretched over the window, @p window_width x @p window_height pixels, and what @p over draws in the same
	/// pass over it.  False, showing nothing, when the window has no area or its surface is lost (VulkanRenderer::present).  std::invalid_argument for no
	/// frame or a window size not above 0; what the renderer throws (std::logic_error without a surface), the frame ended.
	bool present(TexturePtr const &frame, int window_width, int window_height, std::function<void()> const &over = {});
	/// The same of @p width x @p height pixels of B, G, R, X bytes, row 0 at the top (a picture made on the CPU), uploaded first.
	/// std::invalid_argument for no bytes or a size not above 0.
	bool present(void const *bgrx, int width, int height, int window_width, int window_height);

	/// The window-sized picture the last present made, what was drawn over it included.  std::logic_error before one.
	TexturePtr picture() const;

private:
	VulkanRendererPtr renderer_;
	FullscreenCopy copy_;
	TexturePtr uploaded_;          // present(bytes)'s, made again when the frame's size changes
	RenderTargetPtr target_;       // the window's size, made again when that changes
};

}
