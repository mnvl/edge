// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of the vertex color for a shader: a mesh of a vertex type with a color gives its vertex shader a vec4 at location 10, the
// color of each vertex exactly as the vertex holds it (channels in their places, a value above 1 as it is) and interpolated across a
// triangle; the vertex without a color does not get in the way of a shader that does not read one.  Every check renders through the
// renderer and reads the pixels back (test_vertex_color_vert.glsl and test_vertex_color_frag.glsl).

#include "test_fixture.h"
#include <array>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "shader_program.h"
#include "test_backends.h"
#include "test_vulkan_fixture.h"
#include "texture.h"
#include "uber_draw_call.h"
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

int const TARGET_SIZE = 64;

struct Rgba
{
	float r, g, b, a;
};

/// A quad over the whole orthographic view, as two triangles (0 1 2) and (0 2 3); vertex 0 bottom left, 1 bottom right, 2 top right,
/// 3 top left.  Image row 0 is where the target's first row is; which end is "top" is not this test's business, so each check names the
/// pixel by its corner of the image and the vertex that must be nearest it by the quad's own geometry.
struct ColoredQuad
{
	VertexBufferPtr vb;
	IndexBufferPtr ib;
	CameraPtr camera;
	RenderTargetPtr target;

	ColoredQuad(RendererPtr const &renderer, std::array<std::array<float, 4>, 4> const &colors)
	{
		vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PTBNTC>(), sizeof(VERTEX_PTBNTC) * 4);
		auto vb_mapping = vb->write(Contents::DISCARD);
		VERTEX_PTBNTC *v = vb_mapping.as<VERTEX_PTBNTC>().data();
		BOOST_REQUIRE(v);
		float const xs[4] = {-1, 1, 1, -1}, ys[4] = {-1, -1, 1, 1};
		for (int k = 0; k < 4; ++k)
		{
			std::memset(&v[k], 0, sizeof(v[k]));
			v[k].position = rove::vec<3>(xs[k], ys[k], 0.0f);
			v[k].normal = rove::vec<3>(0, 0, 1);
			v[k].tangent = rove::vec<3>(1, 0, 0);
			v[k].binormal = rove::vec<3>(0, 1, 0);
			v[k].texcoords = rove::vec<2>(0.5f, 0.5f);
			v[k].color.set(colors[k][0], colors[k][1], colors[k][2], colors[k][3]);
		}
		vb_mapping.unmap();
		ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
		auto ib_mapping = ib->write(Contents::DISCARD);
		INDEX_U16 *idx = ib_mapping.as<INDEX_U16>().data();
		BOOST_REQUIRE(idx);
		idx[0] = 0; idx[1] = 1; idx[2] = 2; idx[3] = 0; idx[4] = 2; idx[5] = 3;
		ib_mapping.unmap();
		camera = std::make_shared<Camera>();
		camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
		camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
		target = renderer->create_render_target(TARGET_SIZE, TARGET_SIZE, PixelFormat::R32G32B32A32F);
	}

	/// Through the plain draw call, or the uber one, which is what a model is drawn with (it looks the vertex format up in the uber
	/// shader, which must know it even when the material brings shaders of its own).
	std::vector<Rgba> render(RendererPtr const &renderer, bool uber)
	{
		MaterialPtr material = renderer->create_material();
		material->set_cull_mode(Material::CullMode::NONE);
		material->set_shader_program(renderer->create_shader_program(edge::test_shader("test_vertex_color.vert"), edge::test_shader("test_vertex_color.frag")));

		RenderPassClearState clear;
		clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = -1.0f;
		auto draw = [&](auto const &draw_call)
		{
			draw_call->set_vertex_buffer(vb);
			draw_call->set_index_buffer(ib);
			draw_call->set_material(material);
			draw_call->set_camera(camera);
			draw_call->set_triangles_count(2);
			draw_call->set_always_visible(true);
			draw_call->commit();
			renderer->begin_frame();
			renderer->begin_pass(target, clear);
			draw_call->execute();
			renderer->end_pass();
			renderer->finish_frame();
		};
		if (uber)
			draw(renderer->create_uber_draw_call());
		else
			draw(renderer->create_draw_call());

		TexturePtr color = target->color_texture();
		color->download();
		auto color_mapping = color->read();
		Rgba const *pixels = color_mapping.as<Rgba const>().data();
		BOOST_REQUIRE(pixels);
		std::vector<Rgba> out(pixels, pixels + TARGET_SIZE * TARGET_SIZE);
		color_mapping.unmap();
		return out;
	}
};

bool near(float a, float b, float tolerance)
{
	return std::fabs(a - b) <= tolerance;
}

}

BOOST_AUTO_TEST_SUITE(vertex_color)

/// All four vertices hold one color, with every channel different and an alpha well above 1: whatever the pipeline does with the
/// attribute, a pixel anywhere in the quad shows that color or the pipeline swapped, scaled or clamped something.
BOOST_AUTO_TEST_CASE_TEMPLATE(the_color_reaches_the_vertex_shader_exactly_channel_by_channel_and_above_one, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::array<float, 4> const c = {0.125f, 0.25f, 0.5f, 4.0f};
	ColoredQuad quad(renderer, {c, c, c, c});
	for (bool uber : {false, true})
	{
		std::vector<Rgba> px = quad.render(renderer, uber);
		for (int y : {2, TARGET_SIZE / 2, TARGET_SIZE - 3})
			for (int x : {2, TARGET_SIZE / 2, TARGET_SIZE - 3})
			{
				Rgba const &p = px[y * TARGET_SIZE + x];
				BOOST_CHECK_MESSAGE(near(p.r, c[0], 1e-4f) && near(p.g, c[1], 1e-4f) && near(p.b, c[2], 1e-4f) && near(p.a, c[3], 1e-4f),
					(uber ? "uber " : "plain ") << "pixel (" << x << ", " << y << ") = " << p.r << " " << p.g << " " << p.b << " " << p.a);
			}
	}
}

/// Four different colors: each corner of the quad is nearest its vertex's color, and the middle, on the diagonal from vertex 0 to
/// vertex 2, is the mean of those two (the other triangle's vertices have no say there).  Which corner of the image a vertex lands
/// on does not matter: the four colors are found, each in one corner, and the two diagonal ones are opposite.
BOOST_AUTO_TEST_CASE_TEMPLATE(the_colors_of_the_vertices_are_interpolated_across_a_triangle, F, edge::Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	std::array<float, 4> const c0 = {1, 0, 0, 0}, c1 = {0, 1, 0, 1}, c2 = {0, 0, 1, 4}, c3 = {1, 1, 0, 2};
	ColoredQuad quad(renderer, {c0, c1, c2, c3});
	std::vector<Rgba> px = quad.render(renderer, false);

	Rgba const centre = px[(TARGET_SIZE / 2) * TARGET_SIZE + TARGET_SIZE / 2];
	BOOST_CHECK_MESSAGE(near(centre.r, 0.5f, 0.06f) && near(centre.g, 0.0f, 0.06f) && near(centre.b, 0.5f, 0.06f) && near(centre.a, 2.0f, 0.3f),
		"centre = " << centre.r << " " << centre.g << " " << centre.b << " " << centre.a);

	int const near_edge = 1, far_edge = TARGET_SIZE - 2;
	Rgba const corners[4] = {px[near_edge * TARGET_SIZE + near_edge], px[near_edge * TARGET_SIZE + far_edge],
		px[far_edge * TARGET_SIZE + far_edge], px[far_edge * TARGET_SIZE + near_edge]};
	std::array<float, 4> const *const colors[4] = {&c0, &c1, &c2, &c3};
	for (auto const *want : colors)
	{
		int found = 0;
		for (Rgba const &p : corners)
			if (near(p.r, (*want)[0], 0.12f) && near(p.g, (*want)[1], 0.12f) && near(p.b, (*want)[2], 0.12f) && near(p.a, (*want)[3], 0.5f))
				++found;
		BOOST_CHECK_MESSAGE(found == 1, "the color " << (*want)[0] << " " << (*want)[1] << " " << (*want)[2] << " " << (*want)[3]
			<< " is at " << found << " corners");
	}
}

BOOST_AUTO_TEST_SUITE_END()
