// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of a split pass and of downloads that ride in a frame.
//
// begin_split_pass() hands out lists; each is recorded by a thread of its own, at once with the others, and end_pass() draws them.
// What a split pass draws is what the same draws through the renderer draw: the threads share the renderer's uniform buffers
// (every draw takes a block of the draw constants), its pipelines and its materials' descriptor sets, and a draw that read
// another's block, or a material another thread was making the set of, shows another tile's shape or colour.
//
// download_in_frame() puts a target's download in the frame that drew it: no submit of its own and no wait.  With frames in
// flight, a frame's pixels are taken when that frame is drawn (wait_frame), whichever was submitted after it.
//
// Every check renders through the renderer and reads the pixels back.

#include <boost/test/unit_test.hpp>
#include "test_fixture.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "scheduler.h"
#include "shader_program.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"
#include "vulkan_renderer.h"

using namespace edge;

namespace
{

int const SIZE = 512;
int const TILE = 32;
int const TILES = (SIZE / TILE) * (SIZE / TILE);
int const REPEATS = 12;      // draws of each tile: the threads are in the renderer at once for long enough to collide
int const COLOURS = 4;

struct Rgba
{
	float r, g, b, a;
};

struct Rig
{
	VulkanRendererPtr renderer;
	CameraPtr camera;
	IndexBufferPtr indices;
	VertexBufferPtr quad;
	std::vector<TexturePtr> textures;
	RenderTargetPtr target, other;

	explicit Rig(int frames_in_flight = 1):
		renderer(new VulkanRenderer(64, 64, std::string(), std::vector<std::string>(), frames_in_flight))
	{
		camera = std::make_shared<Camera>();
		camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
		camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
		indices = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
		{
			auto mapping = indices->write(Contents::DISCARD);
			INDEX_U16 *idx = mapping.as<INDEX_U16>().data();
			BOOST_REQUIRE(idx);
			idx[0] = 0; idx[1] = 1; idx[2] = 2; idx[3] = 0; idx[4] = 2; idx[5] = 3;
		}
		quad = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
		{
			auto mapping = quad->write(Contents::DISCARD);
			VERTEX_PT *v = mapping.as<VERTEX_PT>().data();
			BOOST_REQUIRE(v);
			v[0].position = rove::vec<3>(-1, -1, 0); v[0].texcoords = rove::vec<2>(0, 0);
			v[1].position = rove::vec<3>( 1, -1, 0); v[1].texcoords = rove::vec<2>(1, 0);
			v[2].position = rove::vec<3>( 1,  1, 0); v[2].texcoords = rove::vec<2>(1, 1);
			v[3].position = rove::vec<3>(-1,  1, 0); v[3].texcoords = rove::vec<2>(0, 1);
		}
		uint32_t const colours[COLOURS] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFF00u};
		for (uint32_t colour : colours)
		{
			TexturePtr texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
			auto mapping = texture->write(Contents::DISCARD);
			uint32_t *p = mapping.as<uint32_t>().data();
			BOOST_REQUIRE(p);
			for (int i = 0; i < 16; ++i)
				p[i] = colour;
			mapping.unmap();
			textures.push_back(texture);
		}
		target = renderer->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F);
		other = renderer->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F);
	}

	/// A material of its own for every call: each is a descriptor set the renderer has not made when its first draw comes.
	MaterialPtr material(int colour) const
	{
		MaterialPtr m = renderer->create_material();
		m->set_cull_mode(Material::CullMode::NONE);
		m->set_shader_program(renderer->create_shader_program(edge::test_shader("test_frame.vert"), edge::test_shader("test_sample.frag")));
		m->set_diffuse_texture(textures[static_cast<size_t>(colour)]);
		return m;
	}

	/// Tile @p tile's quad: as wide a share of its viewport as the tile's column says and as high as its row says (each step two
	/// pixels), in the tile's colour; @p variant moves every tile's shape and colour on by one.  Its shape rides in the draw
	/// constants, its colour in the material's descriptor set.
	DrawCallPtr tile_call(int tile, int variant) const
	{
		int const side = SIZE / TILE;
		rove::matrix<4, 4> world;
		world.identity();
		world.scale(static_cast<float>((tile + variant) % side + 1) / static_cast<float>(side),
			static_cast<float>(tile / side + 1) / static_cast<float>(side), 1.0f);
		DrawCallPtr c = renderer->create_draw_call();
		c->set_world_matrix(world);
		c->set_vertex_buffer(quad);
		c->set_index_buffer(indices);
		c->set_material(material((tile + variant) % COLOURS));
		c->set_camera(camera);
		c->set_triangles_count(2);
		c->set_always_visible(true);
		c->commit();
		return c;
	}

	static void tile_rect(int tile, int &x, int &y)
	{
		x = (tile % (SIZE / TILE)) * TILE;
		y = (tile / (SIZE / TILE)) * TILE;
	}

	static RenderPassClearState clear()
	{
		RenderPassClearState state;
		state.clear_color_r = state.clear_color_g = state.clear_color_b = state.clear_color_a = -1.0f;
		return state;
	}

	/// Every tile through the renderer, one after another.
	void inline_pass(RenderTargetPtr const &into, int variant = 0) const
	{
		renderer->begin_pass(into, clear());
		for (int tile = 0; tile < TILES; ++tile)
		{
			int x, y;
			tile_rect(tile, x, y);
			renderer->set_viewport(x, y, TILE, TILE);
			DrawCallPtr const c = tile_call(tile, variant);
			for (int i = 0; i < REPEATS; ++i)
				c->execute();
		}
		renderer->end_pass();
	}

	/// Every tile in @p lists lists, each recorded by a thread of its own, all at once: list l draws the tiles l, l + lists, ...
	void split_pass(RenderTargetPtr const &into, int lists, int variant = 0) const
	{
		std::vector<DrawList *> const draw_lists = renderer->begin_split_pass(into, clear(), 0, static_cast<size_t>(lists));
		BOOST_REQUIRE_EQUAL(draw_lists.size(), static_cast<size_t>(lists));
		std::vector<std::exception_ptr> errors(static_cast<size_t>(lists));
		std::vector<std::thread> threads;
		for (int l = 0; l < lists; ++l)
		{
			threads.emplace_back([&, l] {
				try
				{
					// through a scheduler of the thread's own, as an application records a list
					Scheduler scheduler(renderer);
					for (int tile = l; tile < TILES; tile += lists)
					{
						int x, y;
						tile_rect(tile, x, y);
						draw_lists[static_cast<size_t>(l)]->set_viewport(x, y, TILE, TILE);
						DrawCallPtr const c = tile_call(tile, variant);
						for (int i = 0; i < REPEATS; ++i)
							scheduler.add(c);
						scheduler.flush(*draw_lists[static_cast<size_t>(l)]);
					}
				}
				catch (...)
				{
					errors[static_cast<size_t>(l)] = std::current_exception();
				}
			});
		}
		for (std::thread &thread : threads)
			thread.join();
		renderer->end_pass();
		for (std::exception_ptr const &error : errors)
			if (error)
				std::rethrow_exception(error);
	}

	std::vector<Rgba> pixels(RenderTargetPtr const &of) const
	{
		auto mapping = of->color_texture()->read();
		Rgba const *p = mapping.as<Rgba const>().data();
		BOOST_REQUIRE(p);
		return std::vector<Rgba>(p, p + SIZE * SIZE);
	}

	std::vector<Rgba> downloaded(RenderTargetPtr const &of) const
	{
		renderer->batch_download_textures({of->color_texture()});
		return pixels(of);
	}
};

bool same(std::vector<Rgba> const &a, std::vector<Rgba> const &b)
{
	return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Rgba)) == 0;
}

/// How many tiles' pictures differ from every other tile's: the reference must tell the tiles apart, or a draw that read
/// another's block would not show.
int distinct_tiles(std::vector<Rgba> const &picture)
{
	std::vector<std::string> tiles;
	for (int tile = 0; tile < TILES; ++tile)
	{
		int x, y;
		Rig::tile_rect(tile, x, y);
		std::string bytes;
		for (int row = 0; row < TILE; ++row)
			bytes.append(reinterpret_cast<char const *>(&picture[static_cast<size_t>(y + row) * SIZE + static_cast<size_t>(x)]),
				TILE * sizeof(Rgba));
		tiles.push_back(bytes);
	}
	int distinct = 0;
	for (size_t i = 0; i < tiles.size(); ++i)
	{
		bool unique = true;
		for (size_t j = 0; j < tiles.size(); ++j)
			unique = unique && (i == j || tiles[i] != tiles[j]);
		distinct += unique;
	}
	return distinct;
}

}

BOOST_AUTO_TEST_SUITE(vulkan_split_pass)

BOOST_AUTO_TEST_CASE(lists_recorded_at_once_draw_what_the_renderer_draws_alone)
{
	Rig rig;
	rig.renderer->begin_frame();
	rig.inline_pass(rig.target);
	rig.renderer->finish_frame();
	std::vector<Rgba> const reference = rig.downloaded(rig.target);
	BOOST_REQUIRE_EQUAL(distinct_tiles(reference), TILES);

	for (int lists : {1, 3, 8})
	{
		rig.renderer->begin_frame();
		rig.split_pass(rig.other, lists);
		rig.renderer->finish_frame();
		BOOST_CHECK_MESSAGE(same(rig.downloaded(rig.other), reference), lists << " lists drew another picture");
	}
}

BOOST_AUTO_TEST_CASE(split_passes_frame_after_frame_and_two_in_a_frame)
{
	// The lists' command buffers are the frame's share's: recorded over when the share comes round again, frames in flight or not.
	for (int frames_in_flight : {1, 3})
	{
		Rig rig(frames_in_flight);
		for (int frame = 0; frame < 7; ++frame)
		{
			int const variant = frame % 2;
			rig.renderer->begin_frame();
			rig.inline_pass(rig.target, variant);
			rig.renderer->finish_frame();
			std::vector<Rgba> const reference = rig.downloaded(rig.target);

			rig.renderer->begin_frame();
			rig.split_pass(rig.other, 4, 1 - variant);      // a first split pass, drawn over by the second
			rig.split_pass(rig.other, 5, variant);
			rig.renderer->submit_frame();
			BOOST_CHECK_MESSAGE(same(rig.downloaded(rig.other), reference), "frame " << frame << " of " << frames_in_flight << " in flight");
		}
	}
}

BOOST_AUTO_TEST_CASE(a_split_pass_is_drawn_through_its_lists_only)
{
	Rig rig;
	DrawCallPtr const call = rig.tile_call(TILES - 1, 0);        // the last tile's quad covers its viewport: here the target
	rig.renderer->begin_frame();
	BOOST_CHECK_THROW(rig.renderer->begin_split_pass(rig.target, Rig::clear(), 0, 0), std::invalid_argument);
	BOOST_CHECK_THROW(rig.renderer->begin_split_pass(RenderTargetPtr(), Rig::clear(), 0, 2), std::invalid_argument);
	BOOST_CHECK(!rig.renderer->in_pass());
	std::vector<DrawList *> const lists = rig.renderer->begin_split_pass(rig.target, Rig::clear(), 0, 2);
	BOOST_CHECK(rig.renderer->in_split_pass());
	BOOST_CHECK_THROW(rig.renderer->set_viewport(0, 0, TILE, TILE), std::logic_error);
	BOOST_CHECK_THROW(call->execute(), std::logic_error);
	BOOST_CHECK_THROW(rig.renderer->begin_split_pass(rig.target, Rig::clear(), 0, 2), std::logic_error);
	BOOST_CHECK_THROW(lists[0]->set_viewport(SIZE - TILE + 1, 0, TILE, TILE), std::out_of_range);
	BOOST_CHECK_THROW(lists[0]->set_viewport(0, 0, 0, TILE), std::out_of_range);
	DrawCallPtr const uncommitted = rig.renderer->create_draw_call();
	BOOST_CHECK_THROW(uncommitted->execute(*lists[1]), std::logic_error);
	call->execute(*lists[1]);
	rig.renderer->end_pass();
	BOOST_CHECK(!rig.renderer->in_split_pass());
	// and the renderer draws again after it
	rig.inline_pass(rig.other);
	rig.renderer->finish_frame();
	BOOST_CHECK_THROW(rig.renderer->begin_split_pass(rig.target, Rig::clear(), 0, 2), std::logic_error);     // outside a frame
	// list 1 drew the whole target (the last tile is yellow); list 0 drew nothing
	std::vector<Rgba> const picture = rig.downloaded(rig.target);
	for (size_t pixel : {size_t(0), size_t(SIZE / 2) * SIZE + SIZE / 2, size_t(SIZE) * SIZE - 1})
	{
		BOOST_CHECK_EQUAL(picture[pixel].r, 1.0f);
		BOOST_CHECK_EQUAL(picture[pixel].g, 1.0f);
		BOOST_CHECK_EQUAL(picture[pixel].b, 0.0f);
	}
}

BOOST_AUTO_TEST_CASE(a_download_in_the_frame_is_the_frame_s_picture)
{
	Rig rig;
	rig.renderer->begin_frame();
	rig.inline_pass(rig.target);
	rig.renderer->finish_frame();
	std::vector<Rgba> const reference = rig.downloaded(rig.target);

	uint64_t const submits_before = rig.renderer->single_time_submit_count();
	rig.renderer->begin_frame();
	rig.split_pass(rig.other, 4);
	rig.renderer->download_in_frame({rig.other->color_texture()});
	rig.renderer->submit_frame();
	uint64_t const frame = rig.renderer->frame_serial();
	rig.renderer->wait_frame(frame);
	BOOST_CHECK(rig.renderer->frame_drawn(frame));
	rig.renderer->finish_downloads({rig.other->color_texture()});
	// the frame's one submit, and none for the download
	BOOST_CHECK_EQUAL(rig.renderer->single_time_submit_count() - submits_before, 1u);
	BOOST_CHECK(same(rig.pixels(rig.other), reference));
}

BOOST_AUTO_TEST_CASE(frames_in_flight_are_downloaded_each_when_it_is_drawn)
{
	Rig rig(2);
	rig.renderer->begin_frame();
	rig.inline_pass(rig.target, 0);
	rig.renderer->finish_frame();
	std::vector<Rgba> const first = rig.downloaded(rig.target);
	rig.renderer->begin_frame();
	rig.inline_pass(rig.target, 1);
	rig.renderer->finish_frame();
	std::vector<Rgba> const second = rig.downloaded(rig.target);
	BOOST_REQUIRE(!same(first, second));

	// Two frames in flight, each into its own target; the later one is taken first.
	rig.renderer->begin_frame();
	rig.split_pass(rig.target, 4, 0);
	rig.renderer->download_in_frame({rig.target->color_texture()});
	rig.renderer->submit_frame();
	uint64_t const frame_a = rig.renderer->frame_serial();
	rig.renderer->begin_frame();
	rig.split_pass(rig.other, 4, 1);
	rig.renderer->download_in_frame({rig.other->color_texture()});
	rig.renderer->submit_frame();
	uint64_t const frame_b = rig.renderer->frame_serial();
	BOOST_REQUIRE_EQUAL(frame_b, frame_a + 1);

	rig.renderer->wait_frame(frame_b);
	rig.renderer->finish_downloads({rig.other->color_texture()});
	BOOST_CHECK(same(rig.pixels(rig.other), second));
	rig.renderer->wait_frame(frame_a);
	rig.renderer->wait_frame(frame_a);       // drawn already: nothing
	rig.renderer->finish_downloads({rig.target->color_texture()});
	BOOST_CHECK(same(rig.pixels(rig.target), first));
	BOOST_CHECK(rig.renderer->frame_drawn(frame_a) && rig.renderer->frame_drawn(frame_b));
	// and the renderer goes on: both shares were given back
	for (int i = 0; i < 3; ++i)
	{
		rig.renderer->begin_frame();
		rig.split_pass(rig.other, 2, 0);
		rig.renderer->submit_frame();
	}
	BOOST_CHECK(same(rig.downloaded(rig.other), first));
}

BOOST_AUTO_TEST_CASE(downloads_and_waits_out_of_place_are_refused)
{
	Rig rig;
	std::vector<TexturePtr> const textures{rig.target->color_texture()};
	BOOST_CHECK_THROW(rig.renderer->download_in_frame(textures), std::logic_error);       // outside a frame
	BOOST_CHECK_THROW(rig.renderer->wait_frame(0), std::logic_error);
	BOOST_CHECK_THROW(rig.renderer->wait_frame(1), std::logic_error);                     // not begun
	rig.renderer->begin_frame();
	// a texture nothing was uploaded to or drawn into has nothing on the GPU to download
	BOOST_CHECK_THROW(rig.renderer->download_in_frame({rig.renderer->create_texture(4, 4, PixelFormat::A8R8G8B8)}), std::logic_error);
	BOOST_CHECK_THROW(rig.renderer->download_in_frame({TexturePtr()}), std::invalid_argument);
	BOOST_CHECK_THROW(rig.renderer->wait_frame(rig.renderer->frame_serial()), std::logic_error);      // the open frame
	BOOST_CHECK_THROW(rig.renderer->frame_drawn(rig.renderer->frame_serial()), std::logic_error);
	rig.renderer->begin_pass(rig.target, Rig::clear());
	BOOST_CHECK_THROW(rig.renderer->download_in_frame(textures), std::logic_error);       // inside a pass
	rig.renderer->end_pass();
	rig.renderer->download_in_frame(textures);
	rig.renderer->finish_frame();
	rig.renderer->finish_downloads(textures);
	BOOST_CHECK_EQUAL(rig.pixels(rig.target)[0].r, -1.0f);
}

BOOST_AUTO_TEST_SUITE_END()
