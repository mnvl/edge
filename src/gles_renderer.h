// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <GLES3/gl3.h>

#include "gles_context.h"
#include "material.h"
#include "renderer.h"

namespace edge
{

class GLESGpuTimer;      // (the renderer's own: its header is not installed)

class GLESRenderer;
using GLESRendererPtr = std::shared_ptr<GLESRenderer>;

/// A linked GL program, and what the renderer binds for it.
///
/// A shader named "x.spv" is the translation of that SPIR-V (tools/shader_cross): x.es.glsl in the shader directory and x.es.map beside
/// it, the map telling what Vulkan's set 0 and its binding numbers become here.  Any other name is a GLSL ES source as it is, with
/// nothing to bind but the uniforms of GLESProgram's members below.
struct GLESProgram
{
	/// The shaders in @p shader_dir; std::runtime_error naming the file and the log if one cannot be read, does not compile, or the
	/// program does not link.
	GLESProgram(GLESContextPtr const &context, std::string const &shader_dir, std::string const &vertex_shader,
		std::string const &fragment_shader);
	/// From source text, with nothing to bind (the paths only name it in an error).
	GLESProgram(GLESContextPtr const &context, std::string const &vertex_source, std::string const &vertex_path,
		std::string const &fragment_source, std::string const &fragment_path);
	~GLESProgram();
	struct Map;
	void bind_resources(Map const &map);
	void link(std::string const &vertex_source, std::string const &vertex_path, std::string const &fragment_source,
		std::string const &fragment_path);
	GLESProgram(GLESProgram const &) = delete;
	GLESProgram &operator=(GLESProgram const &) = delete;

	/// A sampler of the shaders: the texture of Vulkan binding @p binding, sampled in texture unit @p unit.
	struct Sampler
	{
		std::string name;
		int binding;
		int unit;
	};

	GLESContextPtr context;
	GLuint id = 0;
	std::vector<Sampler> samplers;
	// -1 where the shader does not declare it
	GLint world_view_projection = -1;    // mat4 u_world_view_projection (a hand-written source only)
	GLint world = -1;                    // mat4 u_world
	GLint base_color_factor = -1;        // vec4 u_base_color_factor
	// the members of the push constants, uniforms of their own (pc.camera_eye...)
	GLint extra_parameters = -1, camera_eye = -1, push_base_color_factor = -1, material_pbr = -1, emissive_factor = -1, flags = -1;
	bool reads_material_parameters = false;    // a block at binding 20 (Material::parameters) the compiler kept
	bool reads_previous_instances = false, reads_previous_joint_matrices = false;     // ... at 21 and 22 (edge_motion.glsl)
};

/// edge's Renderer on OpenGL ES 3.0 (the phones whose Vulkan drivers cost a frame ~50 ms; see docs/opengles.md).  Offscreen as
/// VulkanRenderer is: it draws into render targets, which the host shows (show_frame).  What is not implemented is a std::logic_error
/// saying so: array textures and stencil writes, and a readable depth has no stencil texture (OpenGL ES 3.0 cannot sample one).
///
/// Shaders are GLSL ES 3.00 sources, named to create_shader_program() relative to the shader directory, and write the clip space of
/// edge's cameras (y up, z from 0 to w) turned into GL's: y flipped, so that row 0 of a target is its top as in Vulkan, and z to -w..w
/// (test_gles_triangle.es.vert).  Attributes sit at the locations the Vulkan renderer gives them (position 0, tangent 1, binormal 2,
/// normal 3, joint indices 4-5, weights 6-7, texcoords 8-9, colour 10); the uniforms are the members of GLESProgram.
class GLESRenderer: public Renderer {
public:
	GLESRendererPtr shared_from_this()
	{
		return std::static_pointer_cast<GLESRenderer>(Renderer::shared_from_this());
	}

	/// In @p context, which the host made and keeps for as long as the renderer lives (any window system's; if it is hosted(), show_frame()
	/// shows frames in its window, and otherwise it is a std::logic_error).  std::invalid_argument for no context or a size that is not
	/// positive; std::runtime_error when the context cannot be made current.
	GLESRenderer(int width, int height, std::string const &shader_dir, GLESContextPtr const &context);
	~GLESRenderer() override;


	int screen_width() const override { return width_; }
	int screen_height() const override { return height_; }
	void resize(int width, int height) override;

	VertexBufferPtr create_vertex_buffer(VertexTraits const *vertex_traits, size_t size,
		BufferUsage usage = BufferUsage::STATIC) override;
	IndexBufferPtr create_index_buffer(IndexFormat index_format, size_t size, BufferUsage usage = BufferUsage::STATIC) override;
	TexturePtr create_texture(int width, int height, PixelFormat pixel_format, TextureUsage const &usage = TextureUsage()) override;

	RenderTargetPtr default_render_target() override;
	/// The GL's GL_MAX_DRAW_BUFFERS, or its GL_MAX_COLOR_ATTACHMENTS where that is fewer.
	unsigned max_color_attachments() const override;
	MaterialPtr create_material() override;

	/// Draws a frame of the host (B, G, R, X bytes, rows tight, row 0 the top, width x height) into the window's framebuffer, stretched
	/// to window_width x window_height pixels, the window's size; swap_window() shows it.  std::logic_error inside a frame or without
	/// a host window; std::invalid_argument for a size that is not positive.
	void show_frame(void const *frame, int width, int height, int window_width, int window_height);
	/// The same of a texture of this renderer's (a render target's colour texture: the frame drawn stays on the GPU, nothing is
	/// downloaded), sampled as it is.  std::invalid_argument for no texture, one of another renderer, or a size that is not positive;
	/// std::logic_error as above.
	void show_frame(TexturePtr const &frame, int window_width, int window_height);
	void swap_window();

	GLESContextPtr const &context() const { return context_; }
	uint64_t frame_serial() const override { return frame_serial_; }
	/// Joint matrices of the skeleton a draw is skinned by, at most this many (std::length_error for more).
	static constexpr size_t MAX_JOINT_MATRICES = 256;

	/// What every draw sees at its bindings (see Renderer): the draw constants (binding 8), this frame's matrices and the frame
	/// before's, are written for the draw by bind_draw_constants(); the frame constants (9) are set by set_frame_constants().
	void bind_draw_constants(rove::matrix<4,4> const &world_view_projection, rove::matrix<4,4> const &world_matrix,
		rove::matrix<4,4> const &previous_world_view_projection, rove::matrix<4,4> const &previous_world_matrix);
	void bind_frame_constants();
	/// The texture of Vulkan binding @p binding for a draw of @p material (its own at 0-5, 10 and 11; a global one at 12-19; the
	/// 1 x 1 white one where there is none) bound in @p unit with the sampler of that binding.
	void bind_texture(Material const &material, int binding, int unit);

	/// The blocks of the previous frame's instances and joint matrices (bindings 21 and 22, edge_motion.glsl) written since the renderer
	/// was made: one for each batch of an instanced draw and each skinned draw whose program reads them.
	uint64_t previous_block_writes() const { return previous_block_writes_; }

	/// The program of these shaders, compiled and linked the first time it is asked for: std::runtime_error naming the file and the log
	/// if a shader does not compile or the program does not link, or a file cannot be read.
	std::shared_ptr<GLESProgram> program(std::string const &vertex_shader_filename, std::string const &fragment_shader_filename);

private:
	GLESContextPtr context_;
	std::string shader_dir_;
	int width_, height_;
	RenderTargetPtr default_render_target_;
	GLuint vertex_array_ = 0;
	std::map<std::pair<std::string, std::string>, std::shared_ptr<GLESProgram>> programs_;
	std::shared_ptr<GLESProgram> show_program_, show_texture_program_;
	/// The texture in unit 0 drawn over the window's framebuffer with @p program (show_frame's tail).
	void show_bound_texture(GLESProgram const &program, int window_width, int window_height);
	GLuint draw_constants_buffer_ = 0, frame_constants_buffer_ = 0, joint_matrices_buffer_ = 0;
	GLuint instances_buffer_ = 0;           // an instanced draw's batch of matrices, at binding 7
	GLuint material_parameters_buffer_ = 0; // the drawn material's own block, at binding 20
	GLuint previous_instances_buffer_ = 0;  // the batch's instances the frame before, at binding 21
	GLuint previous_joint_matrices_buffer_ = 0;   // the joint matrices the frame before, at binding 22
	uint64_t previous_block_writes_ = 0;
	GLuint material_sampler_ = 0, global_sampler_ = 0, show_sampler_ = 0;          // (show_sampler_: a shown texture's, linear, no mipmaps)
	GLuint compare_sampler_ = 0;          // a shadow texture's: compared, then filtered
	TexturePtr white_texture_;
	TexturePtr global_textures_[GLOBAL_TEXTURES_COUNT];
	bool global_compare_[GLOBAL_TEXTURES_COUNT] = {};      // global_shadow_texture: sampled with a comparison
	GLuint frame_texture_ = 0;
	int frame_texture_width_ = 0, frame_texture_height_ = 0;
	uint64_t frame_serial_ = 0;
	std::unique_ptr<GLESGpuTimer> gpu_timer_;

	void init();

protected:
	void do_begin_frame() override;
	/// glFinish(): the frame is drawn when it returns.
	void do_finish_frame() override;
	void do_begin_pass(RenderTargetPtr const &target, RenderPassClearState const &clear_state, uint32_t pass_type) override;
	void do_end_pass() override;
	void do_set_viewport(int x, int y, int width, int height) override;
	void do_set_frame_constants(void const *data, size_t size) override;
	void do_set_global_texture(unsigned slot, TexturePtr const &texture, bool compare) override;
	/// std::invalid_argument for a vertex or index buffer that is not this renderer's.
	void do_draw(DrawCall const &call) override;
	/// std::invalid_argument for more layers than GL_MAX_ARRAY_TEXTURE_LAYERS, a cube larger than GL_MAX_CUBE_MAP_TEXTURE_SIZE;
	/// std::runtime_error for a colour format the device cannot draw into (the framebuffer is incomplete).
	RenderTargetPtr do_create_render_target(int width, int height, RenderTargetFormat const &format) override;
	/// A GLESGpuTimer: a frame's durations are read when the renderer begins or ends a frame and the GL has them.
	GpuTimer &gpu_timer() const override;
};

}
