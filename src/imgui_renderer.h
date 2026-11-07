// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// The program's ImGui: this renderer is a header, compiled against it, as ImGui's own backends (imgui_impl_*.cpp) are.
#include <imgui.h>

#include "camera.h"
#include "draw_call.h"
#include "forward.h"
#include "index.h"
#include "index_buffer.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "renderer.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_buffer.h"
#include "vertex_traits.h"

static_assert(IMGUI_VERSION_NUM >= 19200, "edge's ImGuiRenderer needs Dear ImGui 1.92 or newer (ImGuiBackendFlags_RendererHasTextures)");

namespace edge
{

/// Dear ImGui drawn through edge, on any renderer: the renderer backend ImGui leaves to the application (edge_imgui: this
/// header, compiled against the program's ImGui, 1.92 or newer).  An ImGui frame's draw data is queue()d while the renderer is
/// idle (its buffers are written then), and draw()n inside a pass of a target of the size it was queued for.  The draw data is in the display's logical pixels; it is scaled
/// to the target's.  edge's built-in ui_vertex.spv and ui_fragment.spv; the colours blend with straight alpha.
class ImGuiRenderer
{
public:
	/// The renderer backend of the current ImGui context: builds its font atlas into a texture.  std::invalid_argument for no
	/// renderer; std::logic_error without a current context or for one that has a renderer backend already.
	explicit ImGuiRenderer(RendererPtr const &renderer);
	/// Gives the context its backend back (when it is current), so that another can take it.
	~ImGuiRenderer();
	ImGuiRenderer(ImGuiRenderer const &) = delete;
	ImGuiRenderer &operator=(ImGuiRenderer const &) = delete;

	/// Makes @p texture something ImGui draws (ImGui::Image, AddImage): the id to pass, another than the font's and each other's.
	/// Its texels are multiplied by the vertex colour, so a white tint draws them as they are.  std::invalid_argument for none.
	std::uint64_t add_texture(TexturePtr const &texture);

	/// Takes the draw data of the frame just rendered (ImGui::Render()) for a target of @p target_width x @p target_height pixels.
	/// std::logic_error inside a frame or with a frame queued and not drawn; std::invalid_argument, nothing queued, for no draw
	/// data, a display or a target of no area, a draw callback (nothing here makes one) or a texture that was not added.
	void queue(ImDrawData const *data, int target_width, int target_height);
	/// Draws what was queued, in the pass that is open; nothing queued, nothing drawn.  std::logic_error outside a pass, or in a
	/// pass of a target of another size than the frame was queued for (it stays queued).
	void draw();

private:
	/// One ImGui draw command: the target's rectangle it draws in, which of the queued indices, with which texture.
	struct Command
	{
		int x, y, width, height;
		std::size_t first_index, indices;
		std::uint64_t texture;
	};

	/// An atlas texture ImGui asked for: made, and the id its draws carry.
	struct Atlas
	{
		TexturePtr texture;
		std::uint64_t id;
	};

	/// ImGui's requests of the backend (ImDrawData::Textures): the atlas's textures made, sent again when they changed, let go.
	void update_textures(ImDrawData const *data);

	RendererPtr renderer_;
	ImGuiContext *context_;
	ShaderProgramPtr program_;
	std::map<std::uint64_t, MaterialPtr> materials_;      // by the texture id ImGui draws with
	std::map<int, Atlas> atlas_;                          // by ImTextureData::UniqueID
	std::uint64_t next_texture_ = 1;
	CameraPtr camera_;
	VertexBufferPtr vertices_;
	IndexBufferPtr indices_;
	std::size_t vertex_capacity_ = 0, index_capacity_ = 0;
	std::vector<DrawCallPtr> calls_;                      // one a command, kept for the next frames
	std::vector<Command> commands_;
	int target_width_ = 0, target_height_ = 0;
	bool queued_ = false;

	MaterialPtr make_material(TexturePtr const &texture) const;
};

}

namespace edge
{

namespace detail::imgui_renderer
{

inline constexpr std::size_t FIRST_VERTICES = 4096, FIRST_INDICES = 6144;
inline constexpr int DESTROY_AFTER_UNUSED_FRAMES = 3;      // an atlas texture ImGui no longer wants is let go when no frame in flight can be using it

/// What a draw takes the pixels of the rectangle x, y, w, h of the target to: clip space for the viewport of exactly that rectangle
/// (edge's viewport is upright: the rectangle's top is +1).
inline rove::matrix<4, 4> to_clip(int x, int y, int w, int h)
{
	rove::matrix<4, 4> scale, shift;
	scale.scaling(2.0f / float(w), -2.0f / float(h), 1.0f);
	shift.translation(-1.0f - 2.0f * float(x) / float(w), 1.0f + 2.0f * float(y) / float(h), 0.0f);
	return shift * scale;
}

inline RendererPtr const &not_null(RendererPtr const &renderer)
{
	if (!renderer)
		throw std::invalid_argument("ImGuiRenderer: no renderer");
	return renderer;
}

} // namespace detail::imgui_renderer

inline ImGuiRenderer::ImGuiRenderer(RendererPtr const &renderer):
	renderer_(detail::imgui_renderer::not_null(renderer)),
	context_(ImGui::GetCurrentContext()),
	camera_(std::make_shared<Camera>())
{
	if (!context_)
		throw std::logic_error("ImGuiRenderer: no ImGui context is current");
	ImGuiIO &io = ImGui::GetIO();
	if (io.BackendRendererUserData || io.BackendRendererName)
		throw std::logic_error(std::string("ImGuiRenderer: the ImGui context has a renderer backend already (") +
			(io.BackendRendererName ? io.BackendRendererName : "unnamed") + ")");

	program_ = renderer_->create_shader_program("ui_vertex.spv", "ui_fragment.spv");
	io.BackendRendererName = "edge";
	io.BackendRendererUserData = this;
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;     // the offsets are applied in queue(): a big mesh need not be split
	io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;      // the atlas is made, grown and updated as queue() is told (update_textures)
}

inline ImGuiRenderer::~ImGuiRenderer()
{
	if (ImGui::GetCurrentContext() != context_)
		return;
	// The atlas's textures go with the backend: ImGui is told, to ask for them again of the next one.
	for (ImTextureData *texture : ImGui::GetPlatformIO().Textures)
		if (texture->RefCount == 1 && atlas_.count(texture->UniqueID))
		{
			texture->SetTexID(ImTextureID_Invalid);
			texture->SetStatus(ImTextureStatus_Destroyed);
		}
	ImGuiIO &io = ImGui::GetIO();
	io.BackendRendererName = nullptr;
	io.BackendRendererUserData = nullptr;
	io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
}

inline MaterialPtr ImGuiRenderer::make_material(TexturePtr const &texture) const
{
	MaterialPtr material = renderer_->create_material();
	material->set_shader_program(program_);
	material->set_diffuse_texture(texture);
	material->set_cull_mode(Material::CullMode::NONE);
	material->set_zwrite_enabled(false);
	material->set_alpha_blend_enabled(true);
	material->set_src_blend_mode(Material::BlendMode::SRC_ALPHA);
	material->set_dest_blend_mode(Material::BlendMode::INV_SRC_ALPHA);
	return material;
}

inline std::uint64_t ImGuiRenderer::add_texture(TexturePtr const &texture)
{
	if (!texture)
		throw std::invalid_argument("ImGuiRenderer::add_texture: no texture");
	std::uint64_t const id = next_texture_++;
	materials_[id] = make_material(texture);
	return id;
}

inline void ImGuiRenderer::update_textures(ImDrawData const *data)
{
	if (!data->Textures)
		return;
	for (ImTextureData *texture : *data->Textures)
	{
		switch (texture->Status)
		{
		case ImTextureStatus_WantCreate:
		case ImTextureStatus_WantUpdates:
		{
			// A texture is made again for each update, whole, from ImGui's copy: the atlas is a few hundred pixels square, and
			// changes when a glyph is new (a texture is written once without a CPU copy of its own).
			TextureUsage usage;
			usage.keep_cpu_copy = false;
			TexturePtr made = renderer_->create_texture(texture->Width, texture->Height, PixelFormat::A8R8G8B8, usage);
			auto const known = atlas_.find(texture->UniqueID);
			std::uint64_t const id = known != atlas_.end() ? known->second.id : next_texture_++;
			materials_[id] = make_material(made);
			atlas_[texture->UniqueID] = {std::move(made), id};
			texture->SetTexID(ImTextureID(id));
			Atlas const &at = atlas_.at(texture->UniqueID);
			auto mapped = at.texture->write(Contents::DISCARD);
			auto *texels = mapped.as<unsigned char>().data();
			unsigned char const *pixels = texture->Pixels;
			for (int i = 0; i < texture->Width * texture->Height; ++i)       // R, G, B, A bytes to the texture's B, G, R, A
			{
				if (texture->Format == ImTextureFormat_Alpha8)
				{
					texels[4 * i + 0] = texels[4 * i + 1] = texels[4 * i + 2] = 255;
					texels[4 * i + 3] = pixels[i];
				}
				else
				{
					texels[4 * i + 0] = pixels[4 * i + 2];
					texels[4 * i + 1] = pixels[4 * i + 1];
					texels[4 * i + 2] = pixels[4 * i + 0];
					texels[4 * i + 3] = pixels[4 * i + 3];
				}
			}
			mapped.unmap();
			texture->SetStatus(ImTextureStatus_OK);
			break;
		}
		case ImTextureStatus_WantDestroy:
			if (texture->UnusedFrames >= detail::imgui_renderer::DESTROY_AFTER_UNUSED_FRAMES)
			{
				if (auto const found = atlas_.find(texture->UniqueID); found != atlas_.end())
				{
					materials_.erase(found->second.id);
					atlas_.erase(found);
				}
				texture->SetTexID(ImTextureID_Invalid);
				texture->SetStatus(ImTextureStatus_Destroyed);
			}
			break;
		default:
			break;
		}
	}
}

inline void ImGuiRenderer::queue(ImDrawData const *data, int target_width, int target_height)
{
	if (renderer_->in_frame())
		throw std::logic_error("ImGuiRenderer::queue inside a frame: the buffers are written while the renderer is idle");
	if (queued_)
		throw std::logic_error("ImGuiRenderer::queue: the frame queued before was not drawn");
	if (!data)
		throw std::invalid_argument("ImGuiRenderer::queue: no draw data");
	if (!(data->DisplaySize.x > 0 && data->DisplaySize.y > 0))
		throw std::invalid_argument("ImGuiRenderer::queue: a display of no area");
	if (target_width <= 0 || target_height <= 0)
		throw std::invalid_argument("ImGuiRenderer::queue: a target of " + std::to_string(target_width) + " x " +
			std::to_string(target_height) + " pixels");
	update_textures(data);
	float const sx = float(target_width) / data->DisplaySize.x, sy = float(target_height) / data->DisplaySize.y;

	// what is drawn, and whether all of it can be, before a buffer is written
	std::vector<Command> commands;
	std::size_t total_vertices = 0, total_indices = 0;
	for (int n = 0; n < data->CmdListsCount; ++n)
	{
		ImDrawList const *list = data->CmdLists[n];
		for (ImDrawCmd const &c : list->CmdBuffer)
		{
			if (c.UserCallback)
				throw std::invalid_argument("ImGuiRenderer::queue: a draw callback");
			// the clip rectangle in target pixels, within the target
			int const x0 = std::max(0, int(std::floor(c.ClipRect.x * sx))), y0 = std::max(0, int(std::floor(c.ClipRect.y * sy)));
			int const x1 = std::min(target_width, int(std::ceil(c.ClipRect.z * sx)));
			int const y1 = std::min(target_height, int(std::ceil(c.ClipRect.w * sy)));
			if (x1 <= x0 || y1 <= y0 || c.ElemCount == 0)
				continue;
			std::uint64_t const texture = std::uint64_t(c.GetTexID());
			if (!materials_.count(texture))
				throw std::invalid_argument("ImGuiRenderer::queue: a draw with the texture " + std::to_string(texture) + ", which was not added");
			commands.push_back({x0, y0, x1 - x0, y1 - y0, total_indices + c.IdxOffset, c.ElemCount, texture});
		}
		total_vertices += std::size_t(list->VtxBuffer.Size);
		total_indices += std::size_t(list->IdxBuffer.Size);
	}

	if (!vertices_ || total_vertices > vertex_capacity_ || total_indices > index_capacity_)
	{
		vertex_capacity_ = std::max({detail::imgui_renderer::FIRST_VERTICES, total_vertices, 2 * vertex_capacity_});
		index_capacity_ = std::max({detail::imgui_renderer::FIRST_INDICES, total_indices, 2 * index_capacity_});
		vertices_ = renderer_->create_vertex_buffer(VertexTraits::of<VERTEX_PTBNTC>(), sizeof(VERTEX_PTBNTC) * vertex_capacity_, BufferUsage::DYNAMIC);
		indices_ = renderer_->create_index_buffer(IndexFormat::U32, sizeof(std::uint32_t) * index_capacity_, BufferUsage::DYNAMIC);
		calls_.clear();
	}
	if (total_indices > 0)
	{
		auto mapped_vertices = vertices_->write(Contents::DISCARD);
		auto *v = mapped_vertices.as<VERTEX_PTBNTC>().data();
		auto mapped_indices = indices_->write(Contents::DISCARD);
		auto *idx = mapped_indices.as<std::uint32_t>().data();
		std::size_t base_vertex = 0, next_index = 0;
		for (int n = 0; n < data->CmdListsCount; ++n)
		{
			ImDrawList const *list = data->CmdLists[n];
			for (ImDrawVert const &s : list->VtxBuffer)
			{
				VERTEX_PTBNTC &d = *v++;
				d = VERTEX_PTBNTC();
				d.position = rove::vec<3>(s.pos.x * sx, s.pos.y * sy, 0.0f);
				d.texcoords = rove::vec<2>(s.uv.x, s.uv.y);
				d.color = rove::vec<4>(float(s.col & 0xFF) / 255.0f, float((s.col >> 8) & 0xFF) / 255.0f,
					float((s.col >> 16) & 0xFF) / 255.0f, float(s.col >> 24) / 255.0f);
			}
			for (ImDrawCmd const &c : list->CmdBuffer)
				for (unsigned i = 0; i < c.ElemCount; ++i)
					idx[next_index + c.IdxOffset + i] = std::uint32_t(base_vertex + c.VtxOffset + list->IdxBuffer[int(c.IdxOffset + i)]);
			next_index += std::size_t(list->IdxBuffer.Size);
			base_vertex += std::size_t(list->VtxBuffer.Size);
		}
		mapped_vertices.unmap();
		mapped_indices.unmap();
	}
	commands_ = std::move(commands);
	target_width_ = target_width;
	target_height_ = target_height;
	queued_ = true;
}

inline void ImGuiRenderer::draw()
{
	if (!renderer_->in_pass())
		throw std::logic_error("ImGuiRenderer::draw outside a pass");
	if (!queued_)
		return;
	RenderTargetPtr const &target = renderer_->pass_target();
	if (target->width() != target_width_ || target->height() != target_height_)
		throw std::logic_error("ImGuiRenderer::draw: a frame queued for " + std::to_string(target_width_) + " x " +
			std::to_string(target_height_) + " pixels in a pass of " + std::to_string(target->width()) + " x " +
			std::to_string(target->height()));
	queued_ = false;
	while (calls_.size() < commands_.size())
	{
		DrawCallPtr call = renderer_->create_draw_call();
		call->set_vertex_buffer(vertices_);
		call->set_index_buffer(indices_);
		call->set_camera(camera_);
		call->set_always_visible(true);
		calls_.push_back(call);
	}
	for (std::size_t i = 0; i < commands_.size(); ++i)
	{
		Command const &c = commands_[i];
		DrawCallPtr const &call = calls_[i];
		call->set_material(materials_.at(c.texture));
		call->set_first_index_offset(c.first_index);
		call->set_triangles_count(c.indices / 3);
		call->set_world_matrix(detail::imgui_renderer::to_clip(c.x, c.y, c.width, c.height));
		call->commit();
		renderer_->set_viewport(c.x, c.y, c.width, c.height);
		call->execute();
	}
	if (!commands_.empty())
		renderer_->set_viewport(0, 0, target_width_, target_height_);
}

} // namespace edge
