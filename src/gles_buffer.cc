// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <stdexcept>
#include <string>

#include "gles_buffer.h"
#include "index.h"

namespace edge
{

GLESBufferStore::GLESBufferStore(GLESContextPtr const &context, size_t size, bool dynamic, GLenum target):
	bytes_(size, 0),
	context_(context),
	target_(target)
{
	context_->make_current();
	glGenBuffers(1, &id_);
	glBindBuffer(target_, id_);
	glBufferData(target_, static_cast<GLsizeiptr>(size), nullptr, dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
	check_gl("creating a buffer");
}

GLESBufferStore::~GLESBufferStore()
{
	context_->make_current();
	glDeleteBuffers(1, &id_);
}

void GLESBufferStore::upload()
{
	context_->make_current();
	glBindBuffer(target_, id_);
	glBufferSubData(target_, 0, static_cast<GLsizeiptr>(bytes_.size()), bytes_.data());
	check_gl_often("uploading a buffer");
}

// A vertex buffer through a binding of its own, which no draw reads.
GLESVertexBuffer::GLESVertexBuffer(GLESContextPtr const &context, VertexTraits const *traits, size_t size, bool dynamic):
	store_(context, size, dynamic, GL_COPY_WRITE_BUFFER),
	traits_(traits)
{
}

// An index buffer as indices (WebGL's rule, GLESBufferStore): that makes it the element buffer of the renderer's vertex array, which
// every indexed draw binds its own over.
GLESIndexBuffer::GLESIndexBuffer(GLESContextPtr const &context, IndexFormat index_format, size_t size, bool dynamic):
	store_(context, size, dynamic, GL_ELEMENT_ARRAY_BUFFER)
{
	if (index_format == IndexFormat::U16)
		type_ = GL_UNSIGNED_SHORT;
	else if (index_format == IndexFormat::U32)
		type_ = GL_UNSIGNED_INT;
	else
		throw std::invalid_argument("index format " + std::to_string(static_cast<unsigned>(index_format)));
}

}
