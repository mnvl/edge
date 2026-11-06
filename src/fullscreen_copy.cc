// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "fullscreen_copy.h"

#include <memory>
#include <stdexcept>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "renderer.h"
#include "vertex.h"
#include "vertex_buffer.h"
#include "vertex_traits.h"

namespace edge
{

FullscreenCopy::FullscreenCopy(RendererPtr const &renderer):
	renderer_(renderer)
{
	if (!renderer_)
		throw std::invalid_argument("FullscreenCopy: no renderer");
	// one triangle over the clip square, v = 0 at the top row
	triangle_ = renderer_->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 3);
	auto vertices = triangle_->write(Contents::DISCARD);
	auto *v = vertices.as<VERTEX_PT>().data();
	float const xy[3][2] = {{-1, -1}, {3, -1}, {-1, 3}};
	for (int i = 0; i < 3; ++i)
	{
		v[i].position = rove::vec<3>(xy[i][0], xy[i][1], 0);
		v[i].texcoords = rove::vec<2>((xy[i][0] + 1) / 2, (1 - xy[i][1]) / 2);
	}
	vertices.unmap();

	material_ = renderer_->create_material();
	material_->set_shader_program(renderer_->create_shader_program("copy_vertex.spv", "copy_fragment.spv"));
	material_->set_cull_mode(Material::CullMode::NONE);
	material_->set_zwrite_enabled(false);
	draw_ = renderer_->create_draw_call();
	draw_->set_vertex_buffer(triangle_);
	draw_->set_material(material_);
	draw_->set_camera(std::make_shared<Camera>());
	draw_->set_triangles_count(1);
	draw_->set_always_visible(true);
}

void FullscreenCopy::draw(TexturePtr const &source)
{
	if (!source)
		throw std::invalid_argument("FullscreenCopy::draw: no texture");
	// (a draw takes what its material has when it is executed: the next texture does not change it)
	material_->set_diffuse_texture(source);
	draw_->commit();
	draw_->execute();
}

}
