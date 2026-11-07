// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <iterator>
#include <span>
#include <utility>
#include <vector>
#include "forward.h"
#include "image.h"
#include "mapping.h"
#include "pixel.h"

namespace edge
{

/// How a texture is created beyond its size and format.
struct TextureUsage
{
	/// The texels are sRGB-encoded colour: sampling returns linear values.
	/// For 8-bit and block-compressed colour formats only.
	bool srgb = false;
	/// A full mip chain, made from level 0 whenever the texture is uploaded.
	/// Not for block-compressed formats.
	bool mipmaps = false;
	/// Keep the CPU copy (read(), write(), pixels(), download()) after the first
	/// upload.  Without it the texture frees its CPU copy and staging memory
	/// once uploaded and can no longer be locked: what a loaded, never
	/// changed texture wants.
	bool keep_cpu_copy = true;
	/// The caller writes this many levels (largest first, each right after
	/// the one before, in write()'s mapping) instead of the texture making
	/// them: what a DDS file brings.  0: level 0 only (or mipmaps).
	int provided_mip_levels = 0;
	/// 0: a plain 2D texture.  N >= 1: a 2D array of N layers (an array
	/// view: sampler2DArray in the shader), even of one.  The CPU copy holds
	/// the layers one after the other, each with all its levels (as a DDS
	/// array has them); mipmaps are made for each.  An array is written
	/// through read() and write() and cannot be downloaded or viewed as one image.
	int array_layers = 0;
};

class Texture: public std::enable_shared_from_this<Texture> {
public:
	Texture(Texture const &) = delete;
	Texture &operator=(Texture const &) = delete;
	Texture(int width, int height, PixelFormat pixel_format,
		TextureUsage const &usage = TextureUsage());
	virtual ~Texture();

	// Synchronize texture contents from GPU to CPU-side storage.
	// Backends that keep CPU and GPU copies in sync may implement this as no-op.
	virtual void download();

	/// Whether this is a render target's readable depth (Renderer::create_render_target): what global_shadow_texture takes.
	virtual bool is_depth() const { return false; }

	/// The CPU copy of the texels (level 0, or the levels the texture was given, for every layer), to read: std::logic_error for a
	/// texture without one (TextureUsage::keep_cpu_copy).
	Mapping<Texture const> read() const
	{
		void const *texels = do_lock();
		return Mapping<Texture const>(*this, {static_cast<std::byte const *>(texels), cpu_size()});
	}
	/// The CPU copy of the texels to write: sent to the GPU when the mapping ends.  std::logic_error for a texture without one.
	Mapping<Texture> write(Contents contents = Contents::KEEP)
	{
		void *texels = do_lock(contents == Contents::DISCARD);
		return Mapping<Texture>(*this, {static_cast<std::byte *>(texels), cpu_size()});
	}

	/// The CPU's copy of the texels as it stands: what was last written to
	/// the texture or download()ed from it.  No GPU work and no copy; good
	/// until the texture is destroyed, its pixels changing under it with the
	/// next download() or write.  What is written through the view of a
	/// texture that is not const stays on the CPU: it is not uploaded.
	/// std::logic_error if the texture's format does not keep its texels as
	/// P (see pixel_formats), or the texture has no CPU copy.
	template <class P> requires requires { pixel_formats<P>::list; }
	ImageView<P const> pixels() const
	{
		void const *texels = cpu_copy(pixel_formats<P>::list, std::size(pixel_formats<P>::list));
		return ImageView<P const>(static_cast<P const *>(texels), width_, height_,
			static_cast<std::ptrdiff_t>(width_) * sizeof(P));
	}

	template <class P> requires requires { pixel_formats<P>::list; }
	ImageView<P> pixels()
	{
		ImageView<P const> const view = std::as_const(*this).template pixels<P>();
		return ImageView<P>(const_cast<P *>(view.pixels()), view.width(), view.height(), view.pitch());
	}

	int width() const { return width_; }
	int height() const { return height_; }
	PixelFormat pixel_format() const { return pixel_format_; }
	TextureUsage const &usage() const { return usage_; }
	bool is_srgb() const { return usage_.srgb; }
	/// Mip levels the GPU image has (1 without TextureUsage::mipmaps).
	int mip_levels() const { return mip_levels_; }
	/// Layers of an array texture; 0 for a plain one (TextureUsage::array_layers).  A cube's are its six faces.
	int array_layers() const { return usage_.array_layers; }
	/// Whether this is a cube (a cube render target's texture, RenderTargetFormat::cube): sampled as a samplerCube, its layers the faces.
	bool is_cube() const { return cube_; }
	std::string const &filename() const { return filename_; }
	void set_filename(std::string const &filename) { filename_ = filename; }

	static TexturePtr load_from_file(RendererPtr const &renderer, std::string const &filename,
		bool compress = true, TextureUsage const &usage = TextureUsage());
	static TexturePtr load_from_memory(RendererPtr const &renderer, void const *data, size_t size,
		bool compress = true, TextureUsage const &usage = TextureUsage());
	/// A DDS file's texture as it is: BC1, BC3, BC5 or BC7 (DXGI or FourCC
	/// header) with every mip level it holds.  sRGB when the file says so or
	/// @p usage asks for it (never for BC5).  std::runtime_error for any other
	/// format, a cube, or a size that does not add up.
	static TexturePtr load_dds(RendererPtr const &renderer, void const *data, size_t size,
		TextureUsage const &usage = TextureUsage());
	/// A DDS file with an array of N > 1 layers loads as an array texture
	/// (TextureUsage::array_layers N) of every layer and level it holds.
	/// One array texture of single 2D DDS files, a layer each in their
	/// order: what @p usage asks (sRGB) beside the files.  The files agree
	/// with the first in size, format, sRGB and levels; std::runtime_error
	/// naming the first that does not ("file 2: ..."), or one that is an
	/// array, short, or not a DDS file; and for no files at all.
	static TexturePtr load_dds_array(RendererPtr const &renderer, std::vector<std::span<std::byte const>> const &files,
		TextureUsage const &usage = TextureUsage());
	/// Whether these bytes start like a DDS file.
	static bool is_dds(void const *data, size_t size);
	/// Bytes of level @p level of a w x h texture of @p pixel_format, and
	/// of every level up to and including it.
	static size_t level_size(int width, int height, PixelFormat pixel_format, int level);
	static size_t levels_size(int width, int height, PixelFormat pixel_format, int levels);

	// Supports BMP and PNG formats (extension-based).
	static void save_to_file(TexturePtr const &texture, std::string const &filename);

	/// The renderer the texture was made by, where the backend says (null where not), to tell whether a draw may use it: only compared.
	void const *owner() const { return owner_; }
	void set_owner(void const *owner) { owner_ = owner; }

protected:
	int width_, height_;
	PixelFormat pixel_format_;
	std::string filename_;
	TextureUsage usage_;
	int mip_levels_ = 1;
	bool cube_ = false;

	/// Bytes of the CPU copy: level 0 or the provided levels, for every layer.
	size_t cpu_size() const;

	friend class Mapping<Texture>;
	friend class Mapping<Texture const>;
	friend class Renderer;              // its batch_upload_textures() sends what was written and not sent (Mapping::release())

	/// What a backend gives read() and write(): the CPU copy (std::logic_error for none), and the end of the mapping (a write's sends
	/// the texels).
	virtual void const *do_lock() const = 0;
	virtual void do_unlock() const = 0;
	virtual void *do_lock(bool discard) = 0;
	virtual void do_unlock() = 0;

private:
	void const *owner_ = nullptr;

	// The CPU copy, if the texture's format is one of these.
	void const *cpu_copy(PixelFormat const *pixel_formats, size_t count) const;
};

/// What the GPU has of the texture, downloaded and copied: std::logic_error
/// as for Texture::pixels().
template <class P>
Image<P> read_image(Texture &texture)
{
	texture.download();
	return Image<P>(std::as_const(texture).template pixels<P>());
}

}
