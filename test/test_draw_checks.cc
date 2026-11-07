// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// What a draw is given has to be there: a range past its buffers would read out of bounds on the GPU, and something of another renderer is
// another device's altogether.  Both are errors on every backend, before the backend sees them.

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "camera.h"
#include "draw_call.h"
#include "generic_vertex.h"
#include "index.h"
#include "material.h"
#include "null_renderer.h"
#include "renderer.h"
#include "test_backends.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

CameraPtr a_camera()
{
	CameraPtr camera(new Camera());
	camera->set_perspective_projection(1.0f, 1.0f, 0.1f, 100.0f);
	camera->look_at(rove::vec<3>(0, 0, 5), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	return camera;
}

VertexBufferPtr vertices_of(RendererPtr const &renderer, size_t count)
{
	VertexBufferPtr const buffer = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PNT>(), sizeof(VERTEX_PNT) * count);
	auto bytes = buffer->write(Contents::DISCARD);
	std::memset(bytes.data(), 0, bytes.size());
	bytes.unmap();
	return buffer;
}

template <class I>
IndexBufferPtr indices_of(RendererPtr const &renderer, std::vector<I> const &values)
{
	IndexBufferPtr const buffer = renderer->create_index_buffer(sizeof(I) == 2 ? IndexFormat::U16 : IndexFormat::U32, sizeof(I) * values.size());
	auto bytes = buffer->write(Contents::DISCARD);
	std::memcpy(bytes.data(), values.data(), bytes.size());
	bytes.unmap();
	return buffer;
}

DrawCallPtr a_call(RendererPtr const &renderer, VertexBufferPtr const &vertices, IndexBufferPtr const &indices, size_t triangles)
{
	DrawCallPtr const call = renderer->create_draw_call();
	call->set_vertex_buffer(vertices);
	call->set_index_buffer(indices);
	call->set_material(renderer->create_material());
	call->set_camera(a_camera());
	call->set_always_visible(true);
	call->set_triangles_count(triangles);
	return call;
}

/// One frame of one pass around the draw, which ends cleanly whatever the draw does
template <class E>
void the_draw_throws(RendererPtr const &renderer, DrawCallPtr const &call)
{
	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	call->commit();
	BOOST_CHECK_THROW(call->execute(), E);
	renderer->end_pass();
	renderer->finish_frame();
}

void the_draw_works(RendererPtr const &renderer, DrawCallPtr const &call)
{
	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	call->commit();
	BOOST_CHECK_NO_THROW(call->execute());
	renderer->end_pass();
	renderer->finish_frame();
}

}

BOOST_AUTO_TEST_SUITE(draw_checks)

BOOST_AUTO_TEST_CASE_TEMPLATE(a_draw_inside_its_buffers_is_fine, F, AllBackends)
{
	F f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	std::vector<std::uint16_t> const indices{0, 1, 2, 2, 1, 0};
	DrawCallPtr const call = a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(f.renderer, indices), 2);
	the_draw_works(f.renderer, call);
	call->set_first_index_offset(3);
	call->set_triangles_count(1);
	the_draw_works(f.renderer, call);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(more_triangles_than_the_indices_hold_are_refused, F, AllBackends)
{
	F f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	std::vector<std::uint16_t> const indices{0, 1, 2};
	the_draw_throws<std::invalid_argument>(f.renderer, a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(f.renderer, indices), 1000000));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_first_index_past_the_indices_is_refused, F, AllBackends)
{
	F f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	std::vector<std::uint16_t> const indices{0, 1, 2};
	DrawCallPtr const call = a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(f.renderer, indices), 1);
	call->set_first_index_offset(100000000);
	the_draw_throws<std::invalid_argument>(f.renderer, call);
	call->set_first_index_offset(1);
	the_draw_throws<std::invalid_argument>(f.renderer, call);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(an_index_past_the_vertices_is_refused, F, AllBackends)
{
	F f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	std::vector<std::uint16_t> const small{0, 1, 3};
	the_draw_throws<std::invalid_argument>(f.renderer, a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(f.renderer, small), 1));
	std::vector<std::uint32_t> const large{0, 1, 2000000};
	the_draw_throws<std::invalid_argument>(f.renderer, a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(f.renderer, large), 1));
	// past the draw's range is not this draw's business
	std::vector<std::uint16_t> const later{0, 1, 2, 500, 500, 500};
	the_draw_works(f.renderer, a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(f.renderer, later), 1));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(indices_written_again_are_looked_at_again, F, AllBackends)
{
	F f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	std::vector<std::uint16_t> const good{0, 1, 2};
	IndexBufferPtr const indices = indices_of(f.renderer, good);
	DrawCallPtr const call = a_call(f.renderer, vertices_of(f.renderer, 3), indices, 1);
	the_draw_works(f.renderer, call);
	{
		auto bytes = indices->write();
		std::uint16_t const bad = 9;
		std::memcpy(bytes.data(), &bad, sizeof bad);
		bytes.unmap();
	}
	the_draw_throws<std::invalid_argument>(f.renderer, call);
}

// A mesh's groups share one buffer: each range is its own question, and written indices forget every answer
BOOST_AUTO_TEST_CASE_TEMPLATE(ranges_of_one_buffer_are_answered_each_for_itself, F, AllBackends)
{
	F f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	std::vector<std::uint16_t> const values{0, 1, 2, 7, 7, 7};
	IndexBufferPtr const indices = indices_of(f.renderer, values);
	BOOST_TEST(indices->highest_index(0, 3) == 2u);
	BOOST_TEST(indices->highest_index(3, 3) == 7u);
	BOOST_TEST(indices->highest_index(0, 3) == 2u);
	{
		auto bytes = indices->write();
		std::uint16_t const nine = 9;
		std::memcpy(bytes.data(), &nine, sizeof nine);
		bytes.unmap();
	}
	BOOST_TEST(indices->highest_index(0, 3) == 9u);
	BOOST_TEST(indices->highest_index(3, 3) == 7u);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(without_indices_the_vertices_are_the_range, F, AllBackends)
{
	F f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	DrawCallPtr const call = a_call(f.renderer, vertices_of(f.renderer, 3), nullptr, 1);
	the_draw_works(f.renderer, call);
	call->set_triangles_count(2);
	the_draw_throws<std::invalid_argument>(f.renderer, call);
	call->set_triangles_count(1);
	call->set_minimal_vertex_index(100000000);
	the_draw_throws<std::invalid_argument>(f.renderer, call);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_vertex_offset_past_the_vertices_is_refused_with_indices_too, F, AllBackends)
{
	F f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	std::vector<std::uint16_t> const indices{0, 1, 2};
	DrawCallPtr const call = a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(f.renderer, indices), 1);
	call->set_minimal_vertex_index(100000000);
	the_draw_throws<std::invalid_argument>(f.renderer, call);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_draw_of_no_vertices_is_refused, F, AllBackends)
{
	F f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	DrawCallPtr const call = a_call(f.renderer, nullptr, nullptr, 0);
	f.renderer->begin_frame();
	f.renderer->begin_pass(f.renderer->default_render_target());
	BOOST_CHECK_THROW(call->commit(), std::logic_error);
	f.renderer->end_pass();
	f.renderer->finish_frame();
}

// --- what belongs to another renderer ------------------------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE_TEMPLATE(buffers_of_another_renderer_are_refused, F, AllBackends)
{
	F f, other;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	BOOST_REQUIRE_MESSAGE(other.renderer, other.error);
	std::vector<std::uint16_t> const indices{0, 1, 2};
	the_draw_throws<std::invalid_argument>(f.renderer, a_call(f.renderer, vertices_of(other.renderer, 3), indices_of(f.renderer, indices), 1));
	the_draw_throws<std::invalid_argument>(f.renderer, a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(other.renderer, indices), 1));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_texture_of_another_renderer_is_refused, F, AllBackends)
{
	F f, other;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	BOOST_REQUIRE_MESSAGE(other.renderer, other.error);
	std::vector<std::uint16_t> const indices{0, 1, 2};
	DrawCallPtr const call = a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(f.renderer, indices), 1);
	TextureUsage usage;
	call->material()->set_diffuse_texture(other.renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, usage), 0);
	the_draw_throws<std::invalid_argument>(f.renderer, call);
}

BOOST_AUTO_TEST_SUITE_END()
