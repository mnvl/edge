// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <exception>
#include <span>
#include <type_traits>
#include <utility>

namespace edge
{

/// What a mapping to write starts from: the bytes as they are (KEEP), or bytes that are all to be written (DISCARD: a backend need not
/// bring the old ones to the CPU).
enum class Contents
{
	KEEP,
	DISCARD,
};

namespace detail
{
/// What ~Mapping does with an error of unmap(), which a destructor cannot throw: it logs it (mapping.cc).
void unmap_failed(std::exception_ptr error) noexcept;
}

/// The CPU's copy of a buffer's or a texture's bytes, held from read() or write() for as long as the Mapping lives, or until unmap():
/// what is written through a mapping of a resource that is not const is sent to the GPU then; a mapping of a const one reads.  It is
/// moved, never copied.  @p Resource is VertexBuffer, IndexBuffer or Texture, const or not.
///
///     {
///         auto vertices = vertex_buffer->write(Contents::DISCARD);
///         std::ranges::copy(quad, vertices.as<VERTEX_PT>().begin());
///     }                                       // sent here
///
/// An error in sending (a buffer whose renderer is gone) is thrown by unmap(); the destructor, which cannot throw, logs it: call unmap()
/// where it matters.
template <class Resource>
class [[nodiscard]] Mapping {
public:
	using byte_type = std::conditional_t<std::is_const_v<Resource>, std::byte const, std::byte>;

	Mapping() = default;
	Mapping(Mapping const &) = delete;
	Mapping &operator=(Mapping const &) = delete;

	Mapping(Mapping &&other) noexcept:
		resource_(std::exchange(other.resource_, nullptr)),
		bytes_(std::exchange(other.bytes_, {}))
	{
	}

	Mapping &operator=(Mapping &&other)
	{
		if (this != &other)
		{
			unmap();
			resource_ = std::exchange(other.resource_, nullptr);
			bytes_ = std::exchange(other.bytes_, {});
		}
		return *this;
	}

	~Mapping()
	{
		try
		{
			unmap();
		}
		catch (...)
		{
			detail::unmap_failed(std::current_exception());
		}
	}

	/// Ends the mapping: what was written is sent.  Nothing for one already ended.
	void unmap()
	{
		if (Resource *const resource = std::exchange(resource_, nullptr))
		{
			bytes_ = {};
			resource->do_unlock();
		}
	}

	/// Ends the mapping without sending what was written: the resource keeps it written and unsent, for what sends many at once
	/// (Renderer::batch_upload_textures(), stream_textures()) to send.
	void release()
	{
		resource_ = nullptr;
		bytes_ = {};
	}

	/// The bytes; none once unmapped.
	std::span<byte_type> bytes() const { return bytes_; }
	byte_type *data() const { return bytes_.data(); }
	std::size_t size() const { return bytes_.size(); }

	/// The bytes as Ts (vertices, indices, texels): as many as fit.  A mapping of a const resource gives const Ts.
	template <class T> requires (std::is_trivially_copyable_v<T> && (!std::is_const_v<Resource> || std::is_const_v<T>))
	std::span<T> as() const
	{
		return std::span<T>(reinterpret_cast<T *>(bytes_.data()), bytes_.size() / sizeof(T));
	}

private:
	friend std::remove_const_t<Resource>;

	Mapping(Resource &resource, std::span<byte_type> bytes):
		resource_(&resource),
		bytes_(bytes)
	{
	}

	Resource *resource_ = nullptr;
	std::span<byte_type> bytes_;
};

}
