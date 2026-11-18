// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// What a draw is given has to be there: a range past its buffers would read out of bounds on the GPU, and something of another renderer is
// another device's altogether.  Both are errors on every backend, before the backend sees them.

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

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

std::shared_ptr<Camera> a_camera()
{
	std::shared_ptr<Camera> camera(new Camera());
	camera->set_perspective_projection(1.0f, 1.0f, 0.1f, 100.0f);
	camera->look_at(rove::vec<3>(0, 0, 5), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	return camera;
}

std::shared_ptr<VertexBuffer> vertices_of(std::shared_ptr<Renderer> const &renderer, size_t count)
{
	std::shared_ptr<VertexBuffer> const buffer = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PNT>(), sizeof(VERTEX_PNT) * count);
	auto bytes = buffer->write(Contents::DISCARD);
	std::memset(bytes.data(), 0, bytes.size());
	bytes.unmap();
	return buffer;
}

template <class I>
std::shared_ptr<IndexBuffer> indices_of(std::shared_ptr<Renderer> const &renderer, std::vector<I> const &values)
{
	std::shared_ptr<IndexBuffer> const buffer = renderer->create_index_buffer(sizeof(I) == 2 ? IndexFormat::U16 : IndexFormat::U32, sizeof(I) * values.size());
	auto bytes = buffer->write(Contents::DISCARD);
	std::memcpy(bytes.data(), values.data(), bytes.size());
	bytes.unmap();
	return buffer;
}

std::shared_ptr<DrawCall> a_call(std::shared_ptr<Renderer> const &renderer, std::shared_ptr<VertexBuffer> const &vertices, std::shared_ptr<IndexBuffer> const &indices, size_t triangles)
{
	std::shared_ptr<DrawCall> const call = renderer->create_draw_call();
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
void the_draw_throws(std::shared_ptr<Renderer> const &renderer, std::shared_ptr<DrawCall> const &call)
{
	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	call->commit();
	BOOST_CHECK_THROW(call->execute(), E);
	renderer->end_pass();
	renderer->finish_frame();
}

void the_draw_works(std::shared_ptr<Renderer> const &renderer, std::shared_ptr<DrawCall> const &call)
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
	std::shared_ptr<DrawCall> const call = a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(f.renderer, indices), 2);
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
	std::shared_ptr<DrawCall> const call = a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(f.renderer, indices), 1);
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
	std::shared_ptr<IndexBuffer> const indices = indices_of(f.renderer, good);
	std::shared_ptr<DrawCall> const call = a_call(f.renderer, vertices_of(f.renderer, 3), indices, 1);
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
	std::shared_ptr<IndexBuffer> const indices = indices_of(f.renderer, values);
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

// A split pass's lists are recorded by several threads at once, and each draw is checked as it is recorded: two lists drawing meshes
// of one buffer (instances of one model in two lists) ask it of their ranges together.  The answers are kept, and keeping them was a
// std::map written by both: an arena died of it in its frame (2026-11-18, a segfault in the map's insert under check_ranges()).
BOOST_AUTO_TEST_CASE(threads_may_ask_one_buffer_of_its_ranges_at_once)
{
	std::shared_ptr<Renderer> const renderer(new NullRenderer(64, 64));
	std::size_t const triangles = 6000;
	std::vector<std::uint32_t> values(triangles * 3);
	for (std::size_t i = 0; i < values.size(); ++i)
		values[i] = std::uint32_t((i * 7919u) % 100003u);
	std::shared_ptr<IndexBuffer> const indices = indices_of(renderer, values);
	auto const expected = [&](std::size_t first) {
		return std::size_t(std::max({values[first], values[first + 1], values[first + 2]}));
	};
	std::size_t const threads = 8;
	std::vector<std::size_t> wrong(threads, 0);
	std::vector<std::thread> pool;
	for (std::size_t t = 0; t < threads; ++t)
		pool.emplace_back([&, t] {
			// every thread every range, each from its own end: most answers are new to the buffer while another thread adds its own
			for (std::size_t k = 0; k < triangles; ++k)
			{
				std::size_t const first = 3 * ((t % 2 ? triangles - 1 - k : k) * (t + 1) % triangles);
				if (indices->highest_index(first, 3) != expected(first))
					++wrong[t];
			}
		});
	for (std::thread &thread : pool)
		thread.join();
	for (std::size_t t = 0; t < threads; ++t)
		BOOST_TEST(wrong[t] == 0u);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(without_indices_the_vertices_are_the_range, F, AllBackends)
{
	F f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	std::shared_ptr<DrawCall> const call = a_call(f.renderer, vertices_of(f.renderer, 3), nullptr, 1);
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
	std::shared_ptr<DrawCall> const call = a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(f.renderer, indices), 1);
	call->set_minimal_vertex_index(100000000);
	the_draw_throws<std::invalid_argument>(f.renderer, call);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_draw_of_no_vertices_is_refused, F, AllBackends)
{
	F f;
	BOOST_REQUIRE_MESSAGE(f.renderer, f.error);
	std::shared_ptr<DrawCall> const call = a_call(f.renderer, nullptr, nullptr, 0);
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
	std::shared_ptr<DrawCall> const call = a_call(f.renderer, vertices_of(f.renderer, 3), indices_of(f.renderer, indices), 1);
	TextureUsage usage;
	call->material()->set_diffuse_texture(other.renderer->create_texture(4, 4, PixelFormat::A8R8G8B8, usage), 0);
	the_draw_throws<std::invalid_argument>(f.renderer, call);
}

BOOST_AUTO_TEST_SUITE_END()
