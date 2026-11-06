// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// DynamicMesh: vertices and triangles the host makes again whenever it likes (cloth, a wake, a UI), drawn as the last upload
// left them, whatever came before (more of them, fewer, 16- or 32-bit indices).  An upload that is refused leaves the mesh as it
// was.  Drawn on every backend that draws; the null renderer for what is refused.

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "camera.h"
#include "draw_call.h"
#include "dynamic_mesh.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "scheduler.h"
#include "test_backends.h"
#include "texture.h"
#include "uber_draw_call.h"
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

int const SIZE = 8;

/// A vertex in front of the default camera (looking along +z from the origin), at (x, y) of a plane 2 m away that the target sees
/// from about -0.8 to 0.8.
VERTEX_PT at(float x, float y)
{
	VERTEX_PT v;
	v.position = rove::vec<3>(x, y, 2);
	v.texcoords = rove::vec<2>(0, 0);
	return v;
}

/// Two triangles over the whole target.
std::vector<VERTEX_PT> const QUAD = {at(-10, -10), at(10, -10), at(10, 10), at(-10, 10)};
std::vector<uint16_t> const QUAD_INDICES = {0, 1, 2, 0, 2, 3};

struct Scene
{
	RendererPtr renderer;
	SchedulerPtr scheduler;
	CameraPtr camera = std::make_shared<Camera>();
	MaterialPtr material;
	RenderTargetPtr target;

	explicit Scene(RendererPtr const &r):
		renderer(r),
		scheduler(std::make_shared<Scheduler>(r)),
		material(r->create_material()),
		target(r->create_render_target(SIZE, SIZE, PixelFormat::A8R8G8B8))
	{
		material->set_cull_mode(Material::CullMode::NONE);
	}

	/// How many of the target's pixels the mesh covers (white: the default material over a black clear).
	int covered(DynamicMesh &mesh)
	{
		RenderPassClearState clear;
		clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = 0;
		clear.clear_color_a = 1;
		mesh.draw(*scheduler, camera);
		renderer->begin_frame();
		renderer->begin_pass(target, clear);
		scheduler->flush();
		renderer->end_pass();
		renderer->finish_frame();
		TexturePtr const colour = target->color_texture();
		colour->download();
		auto colour_mapping = colour->read();
		auto const *px = colour_mapping.as<std::array<uint8_t, 4> const>().data();
		int white = 0;
		for (int i = 0; i < SIZE * SIZE; ++i)
			white += px[i][0] > 200 && px[i][1] > 200 && px[i][2] > 200;
		colour_mapping.unmap();
		return white;
	}
};

DynamicMesh mesh_of(Scene const &scene)
{
	return DynamicMesh(scene.renderer, scene.material, VertexTraits::of<VERTEX_PT>());
}

}

BOOST_AUTO_TEST_SUITE(dynamic_mesh)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_mesh_draws_what_its_last_upload_holds, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	Scene scene(fixture.renderer);
	DynamicMesh mesh = mesh_of(scene);
	mesh.upload(std::span<VERTEX_PT const>(QUAD), std::span<uint16_t const>(QUAD_INDICES));
	BOOST_CHECK_EQUAL(mesh.triangles(), 2u);
	BOOST_CHECK_EQUAL(scene.covered(mesh), SIZE * SIZE);
	// fewer vertices and triangles: one triangle, half the target (a diagonal's pixels either side)
	std::vector<uint16_t> const one = {0, 1, 2};
	mesh.upload(std::span<VERTEX_PT const>(QUAD.data(), 3), std::span<uint16_t const>(one));
	BOOST_CHECK_EQUAL(mesh.triangles(), 1u);
	int const half = scene.covered(mesh);
	BOOST_CHECK_GE(half, SIZE * SIZE / 2 - SIZE);
	BOOST_CHECK_LE(half, SIZE * SIZE / 2 + SIZE);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_mesh_grows_to_hold_more_than_it_held, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	Scene scene(fixture.renderer);
	DynamicMesh mesh = mesh_of(scene);
	std::vector<uint16_t> const nothing_seen = {0, 0, 0};
	mesh.upload(std::span<VERTEX_PT const>(QUAD.data(), 1), std::span<uint16_t const>(nothing_seen));
	BOOST_CHECK_EQUAL(scene.covered(mesh), 0);
	// a thousand vertices of which the last four are the quad
	std::vector<VERTEX_PT> many(1000, at(0, 0));
	std::copy(QUAD.begin(), QUAD.end(), many.end() - 4);
	std::vector<uint16_t> indices;
	for (uint16_t i : QUAD_INDICES)
		indices.push_back(uint16_t(996 + i));
	mesh.upload(std::span<VERTEX_PT const>(many), std::span<uint16_t const>(indices));
	BOOST_CHECK_EQUAL(scene.covered(mesh), SIZE * SIZE);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(indices_of_32_bits_reach_vertices_past_65535_and_the_two_widths_follow_each_other, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	Scene scene(fixture.renderer);
	DynamicMesh mesh = mesh_of(scene);
	// vertex 65536 + i is the quad's i; the ones 16 bits reach are at the origin, where nothing is seen
	std::vector<VERTEX_PT> many(65540, at(0, 0));
	std::copy(QUAD.begin(), QUAD.end(), many.begin() + 65536);
	std::vector<uint32_t> wide;
	for (uint16_t i : QUAD_INDICES)
		wide.push_back(65536u + i);
	mesh.upload(std::span<VERTEX_PT const>(many), std::span<uint32_t const>(wide));
	BOOST_CHECK_EQUAL(scene.covered(mesh), SIZE * SIZE);
	mesh.upload(std::span<VERTEX_PT const>(QUAD.data(), 3), std::span<uint16_t const>(std::vector<uint16_t>{0, 1, 2}));
	int const half = scene.covered(mesh);
	BOOST_CHECK_LT(half, SIZE * SIZE);
	BOOST_CHECK_GT(half, 0);
	mesh.upload(std::span<VERTEX_PT const>(many), std::span<uint32_t const>(wide));
	BOOST_CHECK_EQUAL(scene.covered(mesh), SIZE * SIZE);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(an_empty_mesh_is_not_drawn, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	Scene scene(fixture.renderer);
	DynamicMesh mesh = mesh_of(scene);
	BOOST_CHECK_EQUAL(mesh.triangles(), 0u);
	BOOST_CHECK_EQUAL(scene.covered(mesh), 0);              // never uploaded
	mesh.upload(std::span<VERTEX_PT const>(QUAD), std::span<uint16_t const>(QUAD_INDICES));
	mesh.upload(std::span<VERTEX_PT const>(), std::span<uint16_t const>());
	BOOST_CHECK_EQUAL(mesh.triangles(), 0u);
	BOOST_CHECK_EQUAL(scene.covered(mesh), 0);              // emptied
}

BOOST_AUTO_TEST_CASE_TEMPLATE(an_upload_that_is_refused_leaves_the_mesh_as_it_was, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	Scene scene(fixture.renderer);
	DynamicMesh mesh = mesh_of(scene);
	mesh.upload(std::span<VERTEX_PT const>(QUAD), std::span<uint16_t const>(QUAD_INDICES));
	std::vector<VERTEX_PT> const away(4, at(0, 0));
	// not whole triangles; an index past the vertices
	BOOST_CHECK_THROW(mesh.upload(std::span<VERTEX_PT const>(away), std::span<uint16_t const>(std::vector<uint16_t>{0, 1})),
		std::invalid_argument);
	BOOST_CHECK_THROW(mesh.upload(std::span<VERTEX_PT const>(away), std::span<uint32_t const>(std::vector<uint32_t>{0, 1, 4})),
		std::invalid_argument);
	BOOST_CHECK_EQUAL(mesh.triangles(), 2u);
	BOOST_CHECK_EQUAL(scene.covered(mesh), SIZE * SIZE);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_mesh_is_made_of_a_renderer_a_material_and_a_vertex_of_its_size, F, AllBackends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	RendererPtr const renderer = fixture.renderer;
	MaterialPtr const material = renderer->create_material();
	VertexTraits const *traits = VertexTraits::of<VERTEX_PT>();
	BOOST_CHECK_THROW(DynamicMesh(nullptr, material, traits), std::invalid_argument);
	BOOST_CHECK_THROW(DynamicMesh(renderer, nullptr, traits), std::invalid_argument);
	BOOST_CHECK_THROW(DynamicMesh(renderer, material, nullptr), std::invalid_argument);
	DynamicMesh mesh(renderer, material, traits);
	std::vector<VERTEX_PTBNT> const other(3);
	BOOST_CHECK_THROW(mesh.upload(std::span<VERTEX_PTBNT const>(other), std::span<uint16_t const>(std::vector<uint16_t>{0, 1, 2})),
		std::invalid_argument);
	// the draw call is the mesh's, for what it is drawn with
	BOOST_REQUIRE(mesh.draw_call());
	BOOST_CHECK(mesh.draw_call()->material() == material);
	DynamicMesh uber(renderer, material, traits, true);
	BOOST_CHECK(std::dynamic_pointer_cast<UberDrawCall>(uber.draw_call()));
}

BOOST_AUTO_TEST_SUITE_END()
