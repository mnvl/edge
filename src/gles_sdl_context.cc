// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <stdexcept>
#include <string>

#include <SDL3/SDL.h>

#include "gles_sdl_context.h"

namespace edge
{

GLESSDLContext::GLESSDLContext()
{
	create(true);
}

GLESSDLContext::GLESSDLContext(SDL_Window *host_window):
	window_(host_window),
	owns_window_(false)
{
	if (!host_window)
		throw std::invalid_argument("a GLES context needs the host's window");
	create(false);
}

void GLESSDLContext::create(bool own_window)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		throw std::runtime_error(std::string("SDL video: ") + SDL_GetError());

	auto fail = [&](std::string const &what)
	{
		std::string const message = what + ": " + SDL_GetError();
		if (context_)
			SDL_GL_DestroyContext(context_);
		if (own_window && window_)
			SDL_DestroyWindow(window_);
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		throw std::runtime_error(message);
	};

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	if (own_window)
	{
		window_ = SDL_CreateWindow("edge", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
		if (!window_)
			fail("SDL window for OpenGL ES");
	}
	context_ = SDL_GL_CreateContext(window_);       // (current in this thread, once made)
	if (!context_)
		fail("OpenGL ES 3.0 context");
}

GLESSDLContext::~GLESSDLContext()
{
	SDL_GL_MakeCurrent(window_, nullptr);
	SDL_GL_DestroyContext(context_);
	if (owns_window_)
		SDL_DestroyWindow(window_);
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void GLESSDLContext::make_current()
{
	if (SDL_GL_GetCurrentContext() == context_)
		return;
	if (!SDL_GL_MakeCurrent(window_, context_))
		throw std::runtime_error(std::string("SDL_GL_MakeCurrent: ") + SDL_GetError());
}

void GLESSDLContext::swap()
{
	if (!hosted())
		throw std::logic_error("GLES swap of a context with no host window");
	if (!SDL_GL_SwapWindow(window_))
		throw std::runtime_error(std::string("SDL_GL_SwapWindow: ") + SDL_GetError());
}

void *GLESSDLContext::proc_address(char const *name)
{
	make_current();
	return reinterpret_cast<void *>(SDL_GL_GetProcAddress(name));
}

}
