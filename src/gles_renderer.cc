// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "gles_buffer.h"
#include "gles_render_target.h"
#include "gles_renderer.h"
#include "gles_texture.h"
#include "material.h"
#include "pixel.h"
#include "shader_program.h"

namespace edge
{

namespace
{

std::string read_file(std::string const &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		throw std::runtime_error("cannot read shader " + path);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

GLuint compile(GLenum stage, std::string const &source, std::string const &path)
{
	GLuint const shader = glCreateShader(stage);
	char const *text = source.c_str();
	glShaderSource(shader, 1, &text, nullptr);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		GLint length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
		std::vector<char> log(std::max(length, 1));
		glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
		glDeleteShader(shader);
		throw std::runtime_error("shader " + path + " does not compile: " + log.data());
	}
	return shader;
}

}

/// What tools/shader_cross wrote about a shader.
struct GLESProgram::Map
{
	std::vector<std::pair<std::string, int>> ubos, samplers;
	std::vector<std::string> push;
};

namespace
{

bool is_spirv(std::string const &name)
{
	return name.size() > 4 && name.compare(name.size() - 4, 4, ".spv") == 0;
}

std::string stem(std::string const &spirv_name)
{
	return spirv_name.substr(0, spirv_name.size() - 4);
}

GLESProgram::Map read_map(std::string const &path)
{
	std::istringstream in(read_file(path));
	GLESProgram::Map map;
	for (std::string kind; in >> kind;)
	{
		std::string name;
		int binding = 0;
		if (kind == "stage")
			in >> name;
		else if (kind == "ubo")
			in >> name >> binding, map.ubos.emplace_back(name, binding);
		else if (kind == "sampler")
			in >> name >> binding, map.samplers.emplace_back(name, binding);
		else if (kind == "push")
			in >> name, map.push.push_back(name);
		else
			throw std::runtime_error(path + ": a line of " + kind + "?");
		if (!in)
			throw std::runtime_error(path + ": a line of " + kind + " is cut short");
	}
	return map;
}

}

GLESProgram::GLESProgram(GLESContextPtr const &context_, std::string const &shader_dir, std::string const &vertex_shader,
	std::string const &fragment_shader):
	context(context_)
{
	bool const translated = is_spirv(vertex_shader);
	if (translated != is_spirv(fragment_shader))
		throw std::invalid_argument("shaders " + vertex_shader + " and " + fragment_shader + ": a translated SPIR-V and a GLSL ES source");
	auto const path = [&](std::string const &name, char const *suffix)
	{
		std::filesystem::path const p(name);
		return (p.is_absolute() ? p : std::filesystem::path(shader_dir) / p).string() + suffix;
	};
	std::string const vertex_path = translated ? path(stem(vertex_shader), ".es.glsl") : path(vertex_shader, "");
	std::string const fragment_path = translated ? path(stem(fragment_shader), ".es.glsl") : path(fragment_shader, "");
	Map map;
	if (translated)
	{
		for (auto const &name : {vertex_shader, fragment_shader})
		{
			Map const part = read_map(path(stem(name), ".es.map"));
			for (auto const &ubo : part.ubos)
				if (std::ranges::find(map.ubos, ubo) == map.ubos.end())
					map.ubos.push_back(ubo);
			for (auto const &sampler : part.samplers)
				if (std::ranges::find(map.samplers, sampler) == map.samplers.end())
					map.samplers.push_back(sampler);
			for (auto const &push : part.push)
				if (std::ranges::find(map.push, push) == map.push.end())
					map.push.push_back(push);
		}
	}
	link(read_file(vertex_path), vertex_path, read_file(fragment_path), fragment_path);
	try
	{
		bind_resources(map);
	}
	catch (...)
	{
		glDeleteProgram(id);
		throw;
	}
}

void GLESProgram::bind_resources(Map const &map_in)
{
	Map map = map_in;

	// what the map says: uniform blocks at their bindings, samplers in units of their own (the lowest bindings first)
	glUseProgram(id);
	for (auto const &[name, binding] : map.ubos)
	{
		GLuint const block = glGetUniformBlockIndex(id, name.c_str());
		if (block != GL_INVALID_INDEX)       // (a block the compiler found unused is gone)
		{
			glUniformBlockBinding(id, block, binding);
			if (binding == 20)
				reads_material_parameters = true;
			else if (binding == 21)
				reads_previous_instances = true;
			else if (binding == 22)
				reads_previous_joint_matrices = true;
		}
	}
	std::ranges::sort(map.samplers, {}, &std::pair<std::string, int>::second);
	for (auto const &[name, binding] : map.samplers)
	{
		GLint const location = glGetUniformLocation(id, name.c_str());
		if (location < 0)
			continue;
		int const unit = static_cast<int>(samplers.size());
		glUniform1i(location, unit);
		samplers.push_back({name, binding, unit});
	}
	for (auto const &name : map.push)
	{
		GLint const location = glGetUniformLocation(id, name.c_str());
		if (name == "pc.extra_parameters") extra_parameters = location;
		else if (name == "pc.camera_eye") camera_eye = location;
		else if (name == "pc.base_color_factor") push_base_color_factor = location;
		else if (name == "pc.material_pbr") material_pbr = location;
		else if (name == "pc.emissive_factor") emissive_factor = location;
		else if (name == "pc.flags") flags = location;
		else
			throw std::runtime_error("a push constant " + name + " that GLESProgram has no place for");
	}
	check_gl_often("binding a program's resources");
}

GLESProgram::GLESProgram(GLESContextPtr const &context_, std::string const &vertex_source, std::string const &vertex_path,
	std::string const &fragment_source, std::string const &fragment_path):
	context(context_)
{
	link(vertex_source, vertex_path, fragment_source, fragment_path);
}

void GLESProgram::link(std::string const &vertex_source, std::string const &vertex_path, std::string const &fragment_source,
	std::string const &fragment_path)
{
	context->make_current();
	GLuint const vertex = compile(GL_VERTEX_SHADER, vertex_source, vertex_path);
	GLuint fragment = 0;
	try
	{
		fragment = compile(GL_FRAGMENT_SHADER, fragment_source, fragment_path);
	}
	catch (...)
	{
		glDeleteShader(vertex);
		throw;
	}
	id = glCreateProgram();
	glAttachShader(id, vertex);
	glAttachShader(id, fragment);
	glLinkProgram(id);
	glDeleteShader(vertex);
	glDeleteShader(fragment);
	GLint ok = 0;
	glGetProgramiv(id, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		GLint length = 0;
		glGetProgramiv(id, GL_INFO_LOG_LENGTH, &length);
		std::vector<char> log(std::max(length, 1));
		glGetProgramInfoLog(id, static_cast<GLsizei>(log.size()), nullptr, log.data());
		glDeleteProgram(id);
		throw std::runtime_error("shaders " + vertex_path + " and " + fragment_path + " do not link: " + log.data());
	}
	world_view_projection = glGetUniformLocation(id, "u_world_view_projection");
	world = glGetUniformLocation(id, "u_world");
	base_color_factor = glGetUniformLocation(id, "u_base_color_factor");
}

GLESProgram::~GLESProgram()
{
	context->make_current();
	glDeleteProgram(id);
}

GLESRenderer::GLESRenderer(int width, int height, std::string const &shader_dir, GLESContextPtr const &context):
	context_(context),
	shader_dir_(shader_dir),
	width_(width),
	height_(height)
{
	if (!context)
		throw std::invalid_argument("a GLES renderer needs a context");
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a renderer of " + std::to_string(width) + " x " + std::to_string(height));
	init();
}

void GLESRenderer::init()
{
	context_->make_current();
	context_->load_extensions();
	gpu_timer_ = std::make_unique<GLESGpuTimer>(*context_);
	// Attributes are set for each draw; a vertex array is only there because ES 3.0 wants one bound for the element buffer.
	glGenVertexArrays(1, &vertex_array_);
	glBindVertexArray(vertex_array_);
	// uniform blocks, zeroed to begin with
	for (auto const &[buffer, size] : {std::pair{&draw_constants_buffer_, 4 * 16 * sizeof(float)},
			std::pair{&frame_constants_buffer_, FRAME_CONSTANTS_SIZE},
			std::pair{&joint_matrices_buffer_, MAX_JOINT_MATRICES * 16 * sizeof(float)},
			std::pair{&instances_buffer_, MAX_INSTANCES * 16 * sizeof(float)},
			std::pair{&material_parameters_buffer_, Material::PARAMETERS_SIZE},
			std::pair{&previous_instances_buffer_, MAX_INSTANCES * 16 * sizeof(float)},
			std::pair{&previous_joint_matrices_buffer_, MAX_JOINT_MATRICES * 16 * sizeof(float)}})
	{
		std::vector<unsigned char> const zeros(size, 0);
		glGenBuffers(1, buffer);
		glBindBuffer(GL_UNIFORM_BUFFER, *buffer);
		glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(size), zeros.data(), GL_DYNAMIC_DRAW);
	}
	// what the Vulkan renderer's descriptors have: the material's sampler repeats, the global textures' is clamped
	glGenSamplers(1, &material_sampler_);
	glSamplerParameteri(material_sampler_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glSamplerParameteri(material_sampler_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glSamplerParameteri(material_sampler_, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glSamplerParameteri(material_sampler_, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glGenSamplers(1, &global_sampler_);
	glSamplerParameteri(global_sampler_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glSamplerParameteri(global_sampler_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glSamplerParameteri(global_sampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(global_sampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// a shadow texture: a depth compared with the reference, then filtered (which OpenGL ES 3.0 allows a depth only with a comparison)
	glGenSamplers(1, &compare_sampler_);
	glSamplerParameteri(compare_sampler_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glSamplerParameteri(compare_sampler_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glSamplerParameteri(compare_sampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(compare_sampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(compare_sampler_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glSamplerParameteri(compare_sampler_, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	check_gl("creating the renderer");
}

GLESRenderer::~GLESRenderer()
{
	// what holds the context goes first, while it is there to delete from
	programs_.clear();
	show_program_.reset();
	white_texture_.reset();
	for (auto &texture : global_textures_)
		texture.reset();
	default_render_target_.reset();
	context_->make_current();
	gpu_timer_.reset();
	glDeleteVertexArrays(1, &vertex_array_);
	glDeleteBuffers(1, &draw_constants_buffer_);
	glDeleteBuffers(1, &frame_constants_buffer_);
	glDeleteBuffers(1, &joint_matrices_buffer_);
	glDeleteBuffers(1, &instances_buffer_);
	glDeleteBuffers(1, &material_parameters_buffer_);
	glDeleteBuffers(1, &previous_instances_buffer_);
	glDeleteBuffers(1, &previous_joint_matrices_buffer_);
	glDeleteSamplers(1, &material_sampler_);
	glDeleteSamplers(1, &global_sampler_);
	glDeleteSamplers(1, &compare_sampler_);
	if (frame_texture_)
		glDeleteTextures(1, &frame_texture_);
	if (show_sampler_)
		glDeleteSamplers(1, &show_sampler_);
}

void GLESRenderer::resize(int width, int height)
{
	if (width <= 0 || height <= 0)
		throw std::invalid_argument("a renderer of " + std::to_string(width) + " x " + std::to_string(height));
	width_ = width;
	height_ = height;
	default_render_target_.reset();     // made again at the new size when asked for
}

void GLESRenderer::do_begin_frame()
{
	context_->make_current();
	gpu_timer_->begin_frame(frame_serial_ + 1);
	++frame_serial_;
}

void GLESRenderer::do_finish_frame()
{
	glFinish();
	check_gl("finishing a frame (or, in a browser, in a draw, an upload or a pass of it: check_gl_often)");
	gpu_timer_->end_frame();
}

void GLESRenderer::do_begin_pass(RenderTargetPtr const &target, RenderPassClearState const &clear_state, uint32_t)
{
	auto *gles_target = dynamic_cast<GLESRenderTarget *>(target.get());
	if (!gles_target || gles_target->context() != context_)
		throw std::invalid_argument("a GLES render pass into a target of another renderer");

	gpu_timer_->begin_pass();          // (the clear is the pass's)
	glBindFramebuffer(GL_FRAMEBUFFER, gles_target->framebuffer());
	glViewport(0, 0, target->width(), target->height());
	glDisable(GL_SCISSOR_TEST);
	// clears obey the masks
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	GLbitfield bits = 0;
	if (clear_state.clear_color)
	{
		glClearColor(clear_state.clear_color_r, clear_state.clear_color_g, clear_state.clear_color_b, clear_state.clear_color_a);
		bits |= GL_COLOR_BUFFER_BIT;
	}
	if (clear_state.clear_depth)
	{
		glClearDepthf(clear_state.clear_depth_value);
		bits |= GL_DEPTH_BUFFER_BIT;
	}
	if (bits)
		glClear(bits);
	check_gl_often("beginning a render pass");
}

void GLESRenderer::do_end_pass()
{
	gpu_timer_->end_pass();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLESRenderer::do_set_viewport(int x, int y, int width, int height)
{
	// The image is drawn upside down, so GL's window rows from the bottom are the target's rows from the top.
	glViewport(x, y, width, height);
	glEnable(GL_SCISSOR_TEST);
	glScissor(x, y, width, height);
}

VertexBufferPtr GLESRenderer::create_vertex_buffer(VertexTraits const *vertex_traits, size_t size, BufferUsage usage)
{
	auto buffer = std::make_shared<GLESVertexBuffer>(context_, vertex_traits, size, usage == BufferUsage::DYNAMIC);
	buffer->set_owner(this);
	return buffer;
}

IndexBufferPtr GLESRenderer::create_index_buffer(IndexFormat index_format, size_t size, BufferUsage usage)
{
	auto buffer = std::make_shared<GLESIndexBuffer>(context_, index_format, size, usage == BufferUsage::DYNAMIC);
	buffer->set_owner(this);
	return buffer;
}

TexturePtr GLESRenderer::create_texture(int width, int height, PixelFormat pixel_format, TextureUsage const &usage)
{
	auto texture = std::make_shared<GLESTexture>(context_, width, height, pixel_format, usage);
	texture->set_owner(this);
	return texture;
}

void GLESRenderer::do_set_frame_constants(void const *data, size_t size)
{
	// the whole block: what this one does not cover is zero, as on Vulkan, not what an earlier, longer one left
	std::vector<unsigned char> block(FRAME_CONSTANTS_SIZE, 0);
	if (size)
		std::memcpy(block.data(), data, size);
	context_->make_current();
	glBindBuffer(GL_UNIFORM_BUFFER, frame_constants_buffer_);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(block.size()), block.data());
	check_gl_often("setting the frame constants");
}

void GLESRenderer::do_set_global_texture(unsigned slot, TexturePtr const &texture, bool compare)
{
	if (texture)
	{
		auto *gles_texture = dynamic_cast<GLESTexture *>(texture.get());
		if (!gles_texture || gles_texture->context() != context_)
			throw std::invalid_argument("a GLES global texture must be a texture of this renderer");
	}
	global_textures_[slot] = texture;
	global_compare_[slot] = compare;
}

void GLESRenderer::bind_draw_constants(rove::matrix<4,4> const &world_view_projection, rove::matrix<4,4> const &world_matrix,
	rove::matrix<4,4> const &previous_world_view_projection, rove::matrix<4,4> const &previous_world_matrix)
{
	float constants[64];
	std::memcpy(constants, &world_view_projection.ij[0][0], 16 * sizeof(float));
	std::memcpy(constants + 16, &world_matrix.ij[0][0], 16 * sizeof(float));
	std::memcpy(constants + 32, &previous_world_view_projection.ij[0][0], 16 * sizeof(float));
	std::memcpy(constants + 48, &previous_world_matrix.ij[0][0], 16 * sizeof(float));
	glBindBuffer(GL_UNIFORM_BUFFER, draw_constants_buffer_);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(constants), constants);
	glBindBufferBase(GL_UNIFORM_BUFFER, 6, joint_matrices_buffer_);
	glBindBufferBase(GL_UNIFORM_BUFFER, 7, instances_buffer_);
	glBindBufferBase(GL_UNIFORM_BUFFER, 20, material_parameters_buffer_);
	glBindBufferBase(GL_UNIFORM_BUFFER, 21, previous_instances_buffer_);
	glBindBufferBase(GL_UNIFORM_BUFFER, 22, previous_joint_matrices_buffer_);
	glBindBufferBase(GL_UNIFORM_BUFFER, 8, draw_constants_buffer_);
	glBindBufferBase(GL_UNIFORM_BUFFER, 9, frame_constants_buffer_);
}

void GLESRenderer::bind_texture(Material const &material, int binding, int unit)
{
	TexturePtr texture;
	bool global = false;
	if (binding >= 0 && binding < 4)
		texture = static_cast<size_t>(binding) < material.diffuse_textures_count() ? material.diffuse_texture(binding) : nullptr;
	else if (binding == 4)
		texture = material.texture_blending_weights_texture();
	else if (binding == 5)
		texture = material.normal_map_texture();
	else if (binding == 10)
		texture = material.metallic_roughness_texture();
	else if (binding == 11)
		texture = material.emissive_texture();
	else if (binding >= 12 && binding < 12 + static_cast<int>(GLOBAL_TEXTURES_COUNT))
	{
		texture = global_textures_[binding - 12];
		global = true;
	}
	else
		throw std::logic_error("a texture at binding " + std::to_string(binding) + ", which has none");
	if (!texture)
	{
		if (!white_texture_)
		{
			white_texture_ = create_texture(1, 1, PixelFormat::A8R8G8B8);
			auto texels = white_texture_->write(Contents::DISCARD);
			*texels.as<uint32_t>().data() = 0xFFFFFFFFu;
			texels.unmap();
		}
		texture = white_texture_;
	}
	auto *gles_texture = dynamic_cast<GLESTexture *>(texture.get());
	if (!gles_texture || gles_texture->context() != context_)
		throw std::invalid_argument("a GLES draw needs textures of its renderer");
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(gles_texture->target(), gles_texture->get());
	// a depth texture is sampled with its own nearest filtering (a sampler's linear one would leave it incomplete, reading 0)
	bool const compare = global && global_compare_[binding - 12];
	glBindSampler(unit, compare ? compare_sampler_ : gles_texture->is_depth() ? 0 : global ? global_sampler_ : material_sampler_);
}

RenderTargetPtr GLESRenderer::default_render_target()
{
	if (!default_render_target_)
		default_render_target_ = create_render_target(width_, height_, PixelFormat::A8R8G8B8);
	return default_render_target_;
}

unsigned GLESRenderer::max_color_attachments() const
{
	context_->make_current();
	GLint draw_buffers = 0, color_attachments = 0;
	glGetIntegerv(GL_MAX_DRAW_BUFFERS, &draw_buffers);
	glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &color_attachments);
	check_gl("reading the colour attachments a target can have");
	return static_cast<unsigned>(std::min(draw_buffers, color_attachments));
}

RenderTargetPtr GLESRenderer::do_create_render_target(int width, int height, RenderTargetFormat const &format)
{
	context_->make_current();
	GLint max_layers = 0, max_cube = 0;
	glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
	glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &max_cube);
	if (format.layers > max_layers)
		throw std::invalid_argument("a render target of " + std::to_string(format.layers) + " layers: this device has at most "
			+ std::to_string(max_layers) + " layers (GL_MAX_ARRAY_TEXTURE_LAYERS)");
	if (format.cube && width > max_cube)
		throw std::invalid_argument("a cube render target of " + std::to_string(width) + " pixel faces: this device's are at most "
			+ std::to_string(max_cube) + " (GL_MAX_CUBE_MAP_TEXTURE_SIZE)");
	GLESTexture::Layers layers;
	layers.count = format.cube ? 6 : format.layers;
	layers.cube = format.cube;
	std::vector<std::shared_ptr<GLESTexture>> colors;
	for (PixelFormat pixel_format : format.color_formats)
		colors.push_back(layers.count ? std::make_shared<GLESTexture>(context_, width, height, pixel_format, layers)
			: std::make_shared<GLESTexture>(context_, width, height, pixel_format, TextureUsage()));
	auto depth = format.readable_depth ? std::make_shared<GLESTexture>(context_, width, height, GLESTexture::Depth(), layers) : nullptr;
	return std::make_shared<GLESRenderTarget>(context_, width, height, colors, depth);
}

MaterialPtr GLESRenderer::create_material()
{
	return std::make_shared<Material>(shared_from_this());
}

std::shared_ptr<GLESProgram> GLESRenderer::program(std::string const &vertex_shader_filename,
	std::string const &fragment_shader_filename)
{
	auto key = std::make_pair(vertex_shader_filename, fragment_shader_filename);
	auto found = programs_.find(key);
	if (found != programs_.end())
		return found->second;
	auto program = std::make_shared<GLESProgram>(context_, shader_dir_, vertex_shader_filename, fragment_shader_filename);
	programs_[key] = program;
	return program;
}

void GLESRenderer::show_frame(void const *frame, int width, int height, int window_width, int window_height)
{
	if (!context_->hosted())
		throw std::logic_error("GLES show_frame without a host window");
	if (in_frame())
		throw std::logic_error("GLES show_frame inside a frame");
	if (width <= 0 || height <= 0 || window_width <= 0 || window_height <= 0)
		throw std::invalid_argument("a frame of " + std::to_string(width) + " x " + std::to_string(height) + " shown in a window of "
			+ std::to_string(window_width) + " x " + std::to_string(window_height));
	context_->make_current();
	glActiveTexture(GL_TEXTURE0);       // (the frame's unit; a sampler object a draw left there would override the texture's own filtering)
	glBindSampler(0, 0);
	if (!show_program_)
	{
		// one triangle over the whole window; the frame's row 0 is the top, which is texture row 0
		show_program_ = std::make_shared<GLESProgram>(context_,
			"#version 300 es\n"
			"out vec2 uv;\n"
			"void main() {\n"
			"  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
			"  uv = vec2(p.x, 1.0 - p.y);\n"
			"  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
			"}\n", "show_frame.vert",
			"#version 300 es\n"
			"precision mediump float;\n"
			"uniform sampler2D frame;\n"
			"in vec2 uv;\n"
			"out vec4 out_color;\n"
			"void main() { out_color = vec4(texture(frame, uv).bgr, 1.0); }\n", "show_frame.frag");     // (B, G, R bytes, read as R, G, B)
	}
	if (!frame_texture_ || frame_texture_width_ != width || frame_texture_height_ != height)
	{
		if (frame_texture_)
			glDeleteTextures(1, &frame_texture_);
		glGenTextures(1, &frame_texture_);
		glBindTexture(GL_TEXTURE_2D, frame_texture_);
		glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		// the bytes are B, G, R, X: uploaded as they are, swapped by the shader (WebGL 2 has no texture swizzle)
		frame_texture_width_ = width;
		frame_texture_height_ = height;
	}
	glBindTexture(GL_TEXTURE_2D, frame_texture_);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, frame);
	show_bound_texture(*show_program_, window_width, window_height);
}

void GLESRenderer::show_frame(TexturePtr const &frame, int window_width, int window_height)
{
	if (!context_->hosted())
		throw std::logic_error("GLES show_frame without a host window");
	if (in_frame())
		throw std::logic_error("GLES show_frame inside a frame");
	auto *texture = dynamic_cast<GLESTexture *>(frame.get());
	if (!texture || texture->context() != context_)
		throw std::invalid_argument("GLES show_frame: a texture of this renderer's");
	if (window_width <= 0 || window_height <= 0)
		throw std::invalid_argument("a frame shown in a window of " + std::to_string(window_width) + " x " + std::to_string(window_height));
	context_->make_current();
	glActiveTexture(GL_TEXTURE0);
	if (!show_sampler_)
	{
		// (the texture's own parameters would be GL's defaults: a mipmap filter, and no mipmaps make it incomplete: sampled black)
		glGenSamplers(1, &show_sampler_);
		glSamplerParameteri(show_sampler_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glSamplerParameteri(show_sampler_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(show_sampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glSamplerParameteri(show_sampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	glBindSampler(0, show_sampler_);
	if (!show_texture_program_)
		show_texture_program_ = std::make_shared<GLESProgram>(context_,
			"#version 300 es\n"
			"out vec2 uv;\n"
			"void main() {\n"
			"  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
			"  uv = vec2(p.x, 1.0 - p.y);\n"
			"  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
			"}\n", "show_texture.vert",
			"#version 300 es\n"
			"precision mediump float;\n"
			"uniform sampler2D frame;\n"
			"in vec2 uv;\n"
			"out vec4 out_color;\n"
			"void main() { out_color = vec4(texture(frame, uv).rgb, 1.0); }\n", "show_texture.frag");     // (a texture's texels are R, G, B, A in GL)
	glBindTexture(GL_TEXTURE_2D, texture->get());
	show_bound_texture(*show_texture_program_, window_width, window_height);
}

void GLESRenderer::show_bound_texture(GLESProgram const &program, int window_width, int window_height)
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, window_width, window_height);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glUseProgram(program.id);
	for (int location = 0; location < 11; ++location)
		glDisableVertexAttribArray(location);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	check_gl("showing a frame");
}

void GLESRenderer::swap_window()
{
	if (!context_->hosted())
		throw std::logic_error("GLES swap_window without a host window");
	context_->make_current();
	context_->swap();
}

}
