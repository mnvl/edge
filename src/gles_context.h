// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <memory>
#include <set>
#include <string>

namespace edge
{

/// An OpenGL ES 3.0 context, which the host provides (edge asks no window system for it, as it asks none for a Vulkan surface: the
/// host makes the context and gives it to the renderer; gles_sdl_context.h is one made with SDL).  Everything the renderer makes
/// in it holds the context, so that it outlives them and they can delete their GL objects.  One thread uses it at a time: the one
/// that made it, or after make_current() another.
class GLESContext
{
public:
	virtual ~GLESContext() = default;
	GLESContext(GLESContext const &) = delete;
	GLESContext &operator=(GLESContext const &) = delete;

	/// Makes this the calling thread's context if it is not.  std::runtime_error if it cannot be.
	virtual void make_current() = 0;
	/// Whether there is a window to show what was drawn in its own framebuffer in (swap() works).
	virtual bool hosted() const = 0;
	/// Shows what was drawn into the window's own framebuffer.  Only for a hosted() context.
	virtual void swap() = 0;
	/// The address of the GL function @p name (eglGetProcAddress, SDL_GL_GetProcAddress), null where the context gives none: what an
	/// extension adds that the GLES library does not export (GL_EXT_disjoint_timer_query's glGetQueryObjectui64vEXT).  A renderer does
	/// without what it is not given, and says so (GLESRenderer::has_gpu_timer()).
	virtual void *proc_address(char const *name) = 0;

	/// Reads the extensions the GL has; the renderer does it once, with the context current, before it asks has_extension().
	void load_extensions();
	/// Whether the GL has the extension (its name as GL_EXTENSIONS lists it: "GL_KHR_texture_compression_astc_ldr").
	/// std::logic_error before load_extensions().
	bool has_extension(std::string const &name) const;

protected:
	GLESContext() = default;

private:
	std::set<std::string> extensions_;
	bool extensions_loaded_ = false;
};

using GLESContextPtr = std::shared_ptr<GLESContext>;

/// Binds a texture for the renderer to make or fill it, in a texture unit that no draw samples from (the last of the 32 every ES 3.0 has):
/// a texture made or uploaded in the middle of a draw's bindings leaves the units it has set as they are.
void bind_texture_to_edit(unsigned texture);

/// std::runtime_error naming @p what when GL has an error pending.
void check_gl(char const *what);
/// check_gl for what a frame does many times over (a draw, an upload, a pass): the same, but in a browser, where nothing is checked
/// here: WebGL's getError is a round trip to the browser's GPU process that waits for all asked before it (on a phone 120 of them a
/// frame held the page 70-80 % of its time), and the renderer reads GL's errors once a frame instead (finish_frame),
/// so that an error still fails the frame it happened in, though not named by the call that made it.
void check_gl_often(char const *what);

}
