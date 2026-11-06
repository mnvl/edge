// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of the uber draw call: it draws a mesh of any vertex type the renderer has, with lighting on or off and whatever
// textures its material holds; which of the built-in shader's paths the draw takes is the material's and the vertex's business, never a
// table of combinations the draw call refuses what is missing from.  Every check renders through the renderer and reads the pixels back.

#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "camera.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "test_backends.h"
#include "texture.h"
#include "uber_draw_call.h"
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

int const TARGET_SIZE = 16;

struct Rgba
{
	float r, g, b, a;
};

TexturePtr white_texture(RendererPtr const &renderer)
{
	TexturePtr texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
	auto texture_mapping = texture->write(Contents::DISCARD);
	void *texels = texture_mapping.data();
	BOOST_REQUIRE(texels);
	std::memset(texels, 255, 4 * 4 * 4);
	texture_mapping.unmap();
	return texture;
}

/// A quad of vertex type V over the whole orthographic view, drawn through the uber draw call into a target cleared to -1: the pixel
/// in the middle, which is the quad's wherever the draw put its colour.
template <class V>
Rgba drawn_middle(RendererPtr const &renderer, MaterialPtr const &material)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<V>(), sizeof(V) * 4);
	auto vb_mapping = vb->write(Contents::DISCARD);
	V *v = vb_mapping.as<V>().data();
	BOOST_REQUIRE(v);
	float const xs[4] = {-1, 1, 1, -1}, ys[4] = {-1, -1, 1, 1};
	for (int k = 0; k < 4; ++k)
	{
		GenericVertex g;
		std::memset(&g, 0, sizeof(g));
		g.position = rove::vec<3>(xs[k], ys[k], 0.0f);
		g.normal = rove::vec<3>(0, 0, 1);
		g.tangent = rove::vec<3>(1, 0, 0);
		g.binormal = rove::vec<3>(0, 1, 0);
		g.texcoords = rove::vec<2>(0.5f, 0.5f);
		g.weights.i[0] = 1.0f;
		g.texture_blend_weights.set(1, 0, 0, 0);
		g.color.set(1, 1, 1, 1);
		std::memset(&v[k], 0, sizeof(V));
		v[k] = g;
	}
	vb_mapping.unmap();
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
	auto ib_mapping = ib->write(Contents::DISCARD);
	INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
	BOOST_REQUIRE(idx);
	idx[0] = 0; idx[1] = 1; idx[2] = 2; idx[3] = 0; idx[4] = 2; idx[5] = 3;
	ib_mapping.unmap();
	CameraPtr camera(new Camera());
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
	camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	RenderTargetPtr target = renderer->create_render_target(TARGET_SIZE, TARGET_SIZE, PixelFormat::R32G32B32A32F);

	UberDrawCallPtr draw_call = renderer->create_uber_draw_call();
	draw_call->set_vertex_buffer(vb);
	draw_call->set_index_buffer(ib);
	draw_call->set_material(material);
	draw_call->set_camera(camera);
	draw_call->set_triangles_count(2);
	draw_call->set_always_visible(true);
	if constexpr ((V::FORMAT & GenericVertex::FORMAT_JOINT_WEIGHTS) != 0)
	{
		std::vector<rove::matrix<4,4>> joints(1);
		joints[0].identity();                  // the one joint every vertex is weighted to, where the bind pose has it
		draw_call->set_joint_matrix_array(joints);
	}
	draw_call->commit();
	RenderPassClearState clear;
	clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = -1.0f;
	renderer->begin_frame();
	renderer->begin_pass(target, clear);
	draw_call->execute();
	renderer->end_pass();
	renderer->finish_frame();

	TexturePtr color = target->color_texture();
	color->download();
	auto color_mapping = color->read();
	Rgba const *pixels = color_mapping.as<Rgba const>().data();
	BOOST_REQUIRE(pixels);
	Rgba const middle = pixels[(TARGET_SIZE / 2) * TARGET_SIZE + TARGET_SIZE / 2];
	color_mapping.unmap();
	return middle;
}

template <class Backend>
struct Fixture: Backend
{
	using Backend::renderer;
	MaterialPtr material(bool weights_texture)
	{
		MaterialPtr m = this->renderer->create_material();
		m->set_cull_mode(Material::CullMode::NONE);
		TexturePtr const white = white_texture(this->renderer);
		for (size_t i = 0; i < 4; ++i)        // the four of texture blending
			m->set_diffuse_texture(white, i);
		m->set_normal_map_texture(white);
		if (weights_texture)
			m->set_texture_blending_weights_texture(white);
		return m;
	}

	template <class V>
	void drawn(std::string const &what)
	{
		for (bool const weights_texture : {false, true})
		{
			Rgba const p = drawn_middle<V>(this->renderer, material(weights_texture));
			BOOST_CHECK_MESSAGE(p.a >= 0.0f, what << (weights_texture ? ", weights texture" : "")
				<< ": the quad was not drawn (" << p.r << " " << p.g << " " << p.b << " " << p.a << ")");
		}
	}
};

}

BOOST_AUTO_TEST_SUITE(uber_draw_call)

BOOST_AUTO_TEST_CASE_TEMPLATE(every_vertex_type_is_drawn_whatever_textures_the_material_has, F, edge::Backends)
{
	Fixture<F> fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	fixture.template drawn<VERTEX_PT>("PT");
	fixture.template drawn<VERTEX_PNT>("PNT");
	fixture.template drawn<VERTEX_PTBNT>("PTBNT");
	fixture.template drawn<VERTEX_PTBNTC>("PTBNTC");
	fixture.template drawn<VERTEX_PNJWT>("PNJWT");
	fixture.template drawn<VERTEX_PTBNJWT>("PTBNJWT");
}

BOOST_AUTO_TEST_SUITE_END()
