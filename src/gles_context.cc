// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cstdio>
#include <stdexcept>
#include <string>

#include <GLES3/gl3.h>

#include "gles_context.h"

namespace edge
{

void GLESContext::load_extensions()
{
	GLint count = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &count);
	check_gl("reading the extensions");
	extensions_.clear();
	for (GLint i = 0; i < count; ++i)
	{
		char const *const name = reinterpret_cast<char const *>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
		if (!name)
			throw std::runtime_error("OpenGL ES lists " + std::to_string(count) + " extensions and no name for #" + std::to_string(i));
		extensions_.insert(name);
	}
	extensions_loaded_ = true;
}

bool GLESContext::has_extension(std::string const &name) const
{
	if (!extensions_loaded_)
		throw std::logic_error("GLES has_extension before load_extensions");
	return extensions_.count(name) != 0;
}

void bind_texture_to_edit(unsigned texture)
{
	glActiveTexture(GL_TEXTURE0 + 31);
	glBindTexture(GL_TEXTURE_2D, texture);
}

void check_gl(char const *what)
{
	GLenum const error = glGetError();
	if (error == GL_NO_ERROR)
		return;
	char code[16];
	snprintf(code, sizeof(code), "0x%x", error);
	throw std::runtime_error(std::string("OpenGL ES error ") + code + " in " + what);
}

void check_gl_often(char const *what)
{
#ifdef __EMSCRIPTEN__
	(void)what;          // (read once a frame: GLESRenderer::do_finish_frame)
#else
	check_gl(what);
#endif
}

}
