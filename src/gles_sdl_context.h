// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>

#include "gles_context.h"

struct SDL_Window;
struct SDL_GLContextState;

namespace edge
{

/// A GLES context made with SDL (the only part of edge that knows SDL, built as edge_sdl; the renderer takes any GLESContext).
class GLESSDLContext: public GLESContext
{
public:
	/// With a hidden window of its own (offscreen: nothing to show frames in).  std::runtime_error (SDL's message) when there is no
	/// video, or no GLES 3.0.
	GLESSDLContext();
	/// In the host's window, made with SDL_WINDOW_OPENGL and kept alive by the host for as long as the context lives (a phone has only
	/// the one).  std::invalid_argument for none; std::runtime_error as above.
	explicit GLESSDLContext(SDL_Window *host_window);
	~GLESSDLContext() override;

	void make_current() override;
	bool hosted() const override { return !owns_window_; }
	void swap() override;
	/// SDL_GL_GetProcAddress.
	void *proc_address(char const *name) override;

private:
	SDL_Window *window_ = nullptr;
	SDL_GLContextState *context_ = nullptr;
	bool owns_window_ = true;

	void create(bool own_window);
};

}
