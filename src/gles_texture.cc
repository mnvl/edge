// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>

#include "gles_texture.h"
#include "pixel.h"

namespace edge
{

namespace
{

struct GlFormat
{
	GLenum internal_format, format, type;
	bool swap_red_blue;      // the CPU's texels are B, G, R, A
	char const *extension = nullptr;     // what a block-compressed format needs of the GL
};

GlFormat gl_format(PixelFormat pixel_format, bool srgb = false)
{
	if (srgb && pixel_format != PixelFormat::X8R8G8B8 && pixel_format != PixelFormat::A8R8G8B8
		&& !(is_block_compressed(pixel_format) && pixel_format != PixelFormat::BC5))
		throw std::invalid_argument("an sRGB texture of pixel format " + to_string(pixel_format));
	// block-compressed: the GL extensions' names for them, in the sRGB form where asked (BC5 holds data: never sRGB)
	// (a space between the names of extensions that each give the format)
	char const *const s3tc = "GL_EXT_texture_compression_s3tc GL_NV_texture_compression_s3tc";
	char const *const s3tc_srgb = "GL_EXT_texture_compression_s3tc_srgb GL_NV_sRGB_formats";
	char const *const astc = "GL_KHR_texture_compression_astc_ldr";
	switch (pixel_format)
	{
	case PixelFormat::BC1:      return {srgb ? 0x8C4Du : 0x83F1u, 0, 0, false, srgb ? s3tc_srgb : s3tc};
	case PixelFormat::BC3:      return {srgb ? 0x8C4Fu : 0x83F3u, 0, 0, false, srgb ? s3tc_srgb : s3tc};
	case PixelFormat::BC5:      return {0x8DBDu, 0, 0, false, "GL_EXT_texture_compression_rgtc"};
	case PixelFormat::BC7:      return {srgb ? 0x8E8Du : 0x8E8Cu, 0, 0, false, "GL_EXT_texture_compression_bptc"};
	case PixelFormat::ASTC_4x4: return {srgb ? 0x93D0u : 0x93B0u, 0, 0, false, astc};
	case PixelFormat::ASTC_6x6: return {srgb ? 0x93D4u : 0x93B4u, 0, 0, false, astc};
	case PixelFormat::ASTC_8x8: return {srgb ? 0x93D7u : 0x93B7u, 0, 0, false, astc};
	case PixelFormat::X8R8G8B8:      return {srgb ? GLenum(GL_SRGB8_ALPHA8) : GLenum(GL_RGBA8), GL_RGBA, GL_UNSIGNED_BYTE, true};
	case PixelFormat::A8R8G8B8:      return {srgb ? GLenum(GL_SRGB8_ALPHA8) : GLenum(GL_RGBA8), GL_RGBA, GL_UNSIGNED_BYTE, true};
	case PixelFormat::R16G16B16A16F: return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, false};
	case PixelFormat::R32G32B32A32F: return {GL_RGBA32F, GL_RGBA, GL_FLOAT, false};
	case PixelFormat::R32F:          return {GL_R32F, GL_RED, GL_FLOAT, false};
	default:
		throw std::logic_error("GLES backend: pixel format " + to_string(pixel_format) + " is not implemented yet");
	}
}

/// The half-precision float (IEEE binary16) nearest to @p value, as the bits a texel of R16G16B16A16F holds (round to nearest even).
uint16_t to_half(float value)
{
	uint32_t bits;
	std::memcpy(&bits, &value, sizeof(bits));
	uint32_t const sign = (bits >> 16) & 0x8000u;
	int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
	uint32_t mantissa = bits & 0x7FFFFFu;
	if (((bits >> 23) & 0xFFu) == 0xFFu)                                  // infinity, NaN
		return static_cast<uint16_t>(sign | 0x7C00u | (mantissa ? 0x200u : 0u));
	if (exponent >= 31)                                                   // too big: infinity
		return static_cast<uint16_t>(sign | 0x7C00u);
	if (exponent <= 0)                                                    // subnormal, or too small
	{
		if (exponent < -10)
			return static_cast<uint16_t>(sign);
		mantissa |= 0x800000u;
		uint32_t const shift = static_cast<uint32_t>(14 - exponent);
		uint32_t half = mantissa >> shift;
		uint32_t const rest = mantissa & ((1u << shift) - 1), halfway = 1u << (shift - 1);
		if (rest > halfway || (rest == halfway && (half & 1u)))
			++half;
		return static_cast<uint16_t>(sign | half);
	}
	uint32_t half = (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
	uint32_t const rest = mantissa & 0x1FFFu;
	if (rest > 0x1000u || (rest == 0x1000u && (half & 1u)))
		++half;                                                           // (a carry into the exponent is right)
	return static_cast<uint16_t>(sign | half);
}

void swap_red_blue(unsigned char *texels, size_t count)
{
	for (size_t i = 0; i < count; ++i)
		std::swap(texels[4 * i], texels[4 * i + 2]);
}

}

GLESTexture::GLESTexture(GLESContextPtr const &context, int width, int height, PixelFormat pixel_format, TextureUsage const &usage):
	Texture(width, height, pixel_format, usage),
	context_(context)
{
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a texture of " + std::to_string(width) + " x " + std::to_string(height));
	if (usage.array_layers)
		throw std::logic_error("GLES backend: array textures are not implemented yet");
	GlFormat const format = gl_format(pixel_format, usage.srgb);
	auto const has_extension = [&]
	{
		std::istringstream names(format.extension);
		for (std::string name; names >> name;)
			if (context_->has_extension(name))
				return true;
		return false;
	};
	if (format.extension && !has_extension())
		throw std::runtime_error(std::string("a texture of pixel format ") + to_string(pixel_format) + " needs the GL extension "
			+ format.extension + ", which this device lacks");

	context_->make_current();
	glGenTextures(1, &id_);
	bind_texture_to_edit(id_);
	glTexStorage2D(GL_TEXTURE_2D, mip_levels_, format.internal_format, width, height);
	// 32-bit floats are filtered only with an extension some phones lack
	GLint const filter = pixel_format == PixelFormat::R32G32B32A32F || pixel_format == PixelFormat::R32F ? GL_NEAREST : GL_LINEAR;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	if (mip_levels_ > 1)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mip_levels_ - 1);
	check_gl("creating a texture");
	cpu_.assign(cpu_size(), 0);
}

namespace
{

/// A depth texture's, or a layered target's colour's: no CPU copy, @p layers layers.
TextureUsage depth_usage(int layers = 0)
{
	TextureUsage usage;
	usage.keep_cpu_copy = false;
	usage.array_layers = layers;
	return usage;
}

/// What a texture of these layers is bound as.
GLenum layers_target(GLESTexture::Layers const &layers)
{
	if (layers.count < 0 || (layers.cube && layers.count != 6))
		throw std::logic_error("a render target's texture of " + std::to_string(layers.count) + " layers");
	return layers.cube ? GL_TEXTURE_CUBE_MAP : layers.count ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
}

/// Storage of one level for a texture bound to @p target (in unit 31, bind_texture_to_edit's) of @p layers layers.
void layers_storage(GLenum target, GLenum internal_format, int width, int height, int layers)
{
	if (target == GL_TEXTURE_2D_ARRAY)
		glTexStorage3D(target, 1, internal_format, width, height, layers);
	else
		glTexStorage2D(target, 1, internal_format, width, height);
}

}

GLESTexture::GLESTexture(GLESContextPtr const &context, int width, int height, PixelFormat pixel_format, Layers layers):
	Texture(width, height, pixel_format, depth_usage(layers.count)),
	context_(context),
	target_(layers_target(layers))
{
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a texture of " + std::to_string(width) + " x " + std::to_string(height));
	if (!layers.count)
		throw std::logic_error("a layered target's colour of no layers");
	cube_ = layers.cube;
	GlFormat const format = gl_format(pixel_format);
	if (format.format == 0)
		throw std::invalid_argument("a render target's colour of block-compressed pixel format " + to_string(pixel_format));
	context_->make_current();
	glGenTextures(1, &id_);
	glActiveTexture(GL_TEXTURE0 + 31);
	glBindTexture(target_, id_);
	layers_storage(target_, format.internal_format, width, height, layers.count);
	// 32-bit floats are filtered only with an extension some phones lack
	GLint const filter = pixel_format == PixelFormat::R32G32B32A32F || pixel_format == PixelFormat::R32F ? GL_NEAREST : GL_LINEAR;
	glTexParameteri(target_, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(target_, GL_TEXTURE_MAG_FILTER, filter);
	check_gl("creating a layered texture");
}

GLESTexture::GLESTexture(GLESContextPtr const &context, int width, int height, Depth, Layers layers):
	Texture(width, height, PixelFormat::R32F, depth_usage(layers.count)),
	context_(context),
	target_(layers_target(layers)),
	depth_(true)
{
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a depth texture of " + std::to_string(width) + " x " + std::to_string(height));
	cube_ = layers.cube;
	context_->make_current();
	glGenTextures(1, &id_);
	glActiveTexture(GL_TEXTURE0 + 31);
	glBindTexture(target_, id_);
	layers_storage(target_, GL_DEPTH_COMPONENT24, width, height, layers.count);
	glTexParameteri(target_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(target_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(target_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(target_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	check_gl("creating a depth texture");
}

GLESTexture::~GLESTexture()
{
	context_->make_current();
	glDeleteTextures(1, &id_);
}

const void *GLESTexture::do_lock() const
{
	if (cpu_.empty())
		throw std::logic_error("a texture without its CPU copy cannot be read");
	return cpu_.data();
}

void GLESTexture::do_unlock() const
{
}

void *GLESTexture::do_lock(bool)
{
	if (cpu_.empty())
		throw std::logic_error("a texture without its CPU copy cannot be locked");
	return cpu_.data();
}

void GLESTexture::do_unlock()
{
	if (cpu_.empty())
		return;
	GlFormat const format = gl_format(pixel_format_, usage_.srgb);
	std::vector<unsigned char> swapped;
	unsigned char const *texels = cpu_.data();
	if (format.swap_red_blue)
	{
		swapped = cpu_;
		swap_red_blue(swapped.data(), swapped.size() / 4);
		texels = swapped.data();
	}
	context_->make_current();
	bind_texture_to_edit(id_);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	// the levels the caller brings, one after the other; or level 0, and the rest made from it
	int const levels = std::max(1, usage_.provided_mip_levels);
	size_t offset = 0;
	for (int level = 0; level < levels; ++level)
	{
		int const w = std::max(1, width_ >> level), h = std::max(1, height_ >> level);
		size_t const size = level_size(width_, height_, pixel_format_, level);
		if (format.format == 0)
			glCompressedTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, w, h, format.internal_format, static_cast<GLsizei>(size), texels + offset);
		else
			glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, w, h, format.format, format.type, texels + offset);
		offset += size;
	}
	if (usage_.mipmaps)
		glGenerateMipmap(GL_TEXTURE_2D);
	check_gl("uploading a texture");
	uploaded_ = true;
	if (!usage_.keep_cpu_copy)
	{
		cpu_.clear();
		cpu_.shrink_to_fit();
	}
}

void GLESTexture::download()
{
	Texture::download();
	if (cpu_.empty())
		throw std::logic_error("a texture without its CPU copy cannot be downloaded");
	GlFormat const format = gl_format(pixel_format_, usage_.srgb);
	if (format.format == 0)
		throw std::logic_error("a block-compressed texture cannot be downloaded");

	context_->make_current();
	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, id_, 0);
	GLenum const status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &fbo);
		throw std::runtime_error("GLES backend: a texture of pixel format " + to_string(pixel_format_)
			+ " cannot be read back (framebuffer status " + std::to_string(status) + ")");
	}
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	if (format.type == GL_HALF_FLOAT)
	{
		// GL hands a half-float target back as floats; the texels are the nearest halves
		std::vector<float> floats(static_cast<size_t>(width_) * height_ * 4);
		glReadPixels(0, 0, width_, height_, GL_RGBA, GL_FLOAT, floats.data());
		auto *halves = reinterpret_cast<uint16_t *>(cpu_.data());
		for (size_t i = 0; i < floats.size(); ++i)
			halves[i] = to_half(floats[i]);
	}
	else
	{
		glReadPixels(0, 0, width_, height_, format.format, format.type, cpu_.data());
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	check_gl("reading a texture back");
	if (format.swap_red_blue)
		swap_red_blue(cpu_.data(), cpu_.size() / 4);
}

}
