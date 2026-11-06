// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "imgui_renderer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

#include <imgui.h>

#include "camera.h"
#include "draw_call.h"
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

namespace edge
{

namespace
{

std::size_t const FIRST_VERTICES = 4096, FIRST_INDICES = 6144;
int const DESTROY_AFTER_UNUSED_FRAMES = 3;      // an atlas texture ImGui no longer wants is let go when no frame in flight can be using it

/// What a draw takes the pixels of the rectangle x, y, w, h of the target to: clip space for the viewport of exactly that rectangle
/// (edge's viewport is upright: the rectangle's top is +1).
rove::matrix<4, 4> to_clip(int x, int y, int w, int h)
{
	rove::matrix<4, 4> scale, shift;
	scale.scaling(2.0f / float(w), -2.0f / float(h), 1.0f);
	shift.translation(-1.0f - 2.0f * float(x) / float(w), 1.0f + 2.0f * float(y) / float(h), 0.0f);
	return shift * scale;
}

RendererPtr const &not_null(RendererPtr const &renderer)
{
	if (!renderer)
		throw std::invalid_argument("ImGuiRenderer: no renderer");
	return renderer;
}

}

ImGuiRenderer::ImGuiRenderer(RendererPtr const &renderer):
	renderer_(not_null(renderer)),
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

ImGuiRenderer::~ImGuiRenderer()
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

MaterialPtr ImGuiRenderer::make_material(TexturePtr const &texture) const
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

std::uint64_t ImGuiRenderer::add_texture(TexturePtr const &texture)
{
	if (!texture)
		throw std::invalid_argument("ImGuiRenderer::add_texture: no texture");
	std::uint64_t const id = next_texture_++;
	materials_[id] = make_material(texture);
	return id;
}

void ImGuiRenderer::update_textures(ImDrawData const *data)
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
			if (texture->UnusedFrames >= DESTROY_AFTER_UNUSED_FRAMES)
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

void ImGuiRenderer::queue(ImDrawData const *data, int target_width, int target_height)
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
		vertex_capacity_ = std::max({FIRST_VERTICES, total_vertices, 2 * vertex_capacity_});
		index_capacity_ = std::max({FIRST_INDICES, total_indices, 2 * index_capacity_});
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

void ImGuiRenderer::draw()
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
		call->set_world_matrix(to_clip(c.x, c.y, c.width, c.height));
		call->commit();
		renderer_->set_viewport(c.x, c.y, c.width, c.height);
		call->execute();
	}
	if (!commands_.empty())
		renderer_->set_viewport(0, 0, target_width_, target_height_);
}

}
