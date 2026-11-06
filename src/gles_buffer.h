// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include "index.h"
#include <vector>

#include <GLES3/gl3.h>

#include "gles_context.h"
#include "index_buffer.h"
#include "vertex_buffer.h"
#include "vertex_traits.h"

namespace edge
{

/// A GL buffer and its CPU copy: the end of a write() sends the copy whole.  target is what it is made and filled through: WebGL lets a buffer
/// that was once bound as indices be bound as nothing else, and one bound as anything else never as indices.
class GLESBufferStore
{
public:
	GLESBufferStore(GLESContextPtr const &context, size_t size, bool dynamic, GLenum target);
	~GLESBufferStore();
	GLESBufferStore(GLESBufferStore const &) = delete;
	GLESBufferStore &operator=(GLESBufferStore const &) = delete;

	GLuint get() const { return id_; }
	void upload();
	/// The CPU's copy, which upload() sends.
	std::vector<unsigned char> &bytes() { return bytes_; }
	std::vector<unsigned char> const &bytes() const { return bytes_; }

private:
	std::vector<unsigned char> bytes_;
	GLESContextPtr context_;
	GLenum target_;
	GLuint id_ = 0;
};

class GLESVertexBuffer: public VertexBuffer
{
public:
	GLESVertexBuffer(GLESContextPtr const &context, VertexTraits const *traits, size_t size, bool dynamic);

	GLuint get() const { return store_.get(); }
	VertexTraits const *vertex_traits() const { return traits_; }

	size_t size() const override { return store_.bytes().size(); }

protected:
	const void *do_lock() const override { return store_.bytes().data(); }
	void do_unlock() const override {}
	void *do_lock(bool) override { return store_.bytes().data(); }
	void do_unlock() override { store_.upload(); }

private:
	GLESBufferStore store_;
	VertexTraits const *traits_;
};

class GLESIndexBuffer: public IndexBuffer
{
public:
	/// std::invalid_argument for a format that is not IndexFormat::U16 or IndexFormat::U32.
	GLESIndexBuffer(GLESContextPtr const &context, IndexFormat index_format, size_t size, bool dynamic);

	GLuint get() const { return store_.get(); }
	/// GL_UNSIGNED_SHORT or GL_UNSIGNED_INT
	GLenum index_type() const { return type_; }
	size_t index_size() const { return type_ == GL_UNSIGNED_SHORT ? 2 : 4; }

	size_t size() const override { return store_.bytes().size(); }

protected:
	const void *do_lock() const override { return store_.bytes().data(); }
	void do_unlock() const override {}
	void *do_lock(bool) override { return store_.bytes().data(); }
	void do_unlock() override { store_.upload(); }

private:
	GLESBufferStore store_;
	GLenum type_;
};

}
