// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// A pass of several lists on Vulkan: each list a secondary command buffer, recorded by a thread of its own at once with the others,
// and end_pass() draws them in their order.  What the lists draw is what the same draws in one list draw: the threads share the
// renderer's uniform buffers (every draw takes a block of the draw constants), its pipelines and its materials' descriptor sets,
// and a draw that read another's block, or a material another thread was making the set of, shows another tile's shape or colour.
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
	std::shared_ptr<VulkanRenderer> renderer;
	std::shared_ptr<Camera> camera;
	std::shared_ptr<IndexBuffer> indices;
	std::shared_ptr<VertexBuffer> quad;
	std::vector<std::shared_ptr<Texture>> textures;
	std::shared_ptr<RenderTarget> target, other;

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
			std::shared_ptr<Texture> texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
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
	std::shared_ptr<Material> material(int colour) const
	{
		std::shared_ptr<Material> m = renderer->create_material();
		m->set_cull_mode(Material::CullMode::NONE);
		m->set_shader_program(renderer->create_shader_program(edge::test_shader("test_frame.vert"), edge::test_shader("test_sample.frag")));
		m->set_diffuse_texture(textures[static_cast<size_t>(colour)]);
		return m;
	}

	/// Tile @p tile's quad: as wide a share of its viewport as the tile's column says and as high as its row says (each step two
	/// pixels), in the tile's colour; @p variant moves every tile's shape and colour on by one.  Its shape rides in the draw
	/// constants, its colour in the material's descriptor set.
	std::shared_ptr<DrawCall> tile_call(int tile, int variant) const
	{
		int const side = SIZE / TILE;
		rove::matrix<4, 4> world;
		world.identity();
		world.scale(static_cast<float>((tile + variant) % side + 1) / static_cast<float>(side),
			static_cast<float>(tile / side + 1) / static_cast<float>(side), 1.0f);
		std::shared_ptr<DrawCall> c = renderer->create_draw_call();
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

	/// Every tile through the renderer (a pass of one list), one after another.
	void inline_pass(std::shared_ptr<RenderTarget> const &into, int variant = 0) const
	{
		renderer->begin_pass(into, clear());
		for (int tile = 0; tile < TILES; ++tile)
		{
			int x, y;
			tile_rect(tile, x, y);
			renderer->set_viewport(x, y, TILE, TILE);
			std::shared_ptr<DrawCall> const c = tile_call(tile, variant);
			for (int i = 0; i < REPEATS; ++i)
				c->execute();
		}
		renderer->end_pass();
	}

	/// Every tile in @p lists lists, each recorded by a thread of its own, all at once: list l draws the tiles l, l + lists, ...
	void split_pass(std::shared_ptr<RenderTarget> const &into, int lists, int variant = 0) const
	{
		renderer->begin_pass(into, clear(), 0, static_cast<size_t>(lists));
		BOOST_REQUIRE_EQUAL(renderer->draw_lists_count(), static_cast<size_t>(lists));
		std::vector<DrawList *> draw_lists;
		for (int l = 0; l < lists; ++l)
			draw_lists.push_back(&renderer->draw_list(static_cast<size_t>(l)));
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
						std::shared_ptr<DrawCall> const c = tile_call(tile, variant);
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

	std::vector<Rgba> pixels(std::shared_ptr<RenderTarget> const &of) const
	{
		auto mapping = of->color_texture()->read();
		Rgba const *p = mapping.as<Rgba const>().data();
		BOOST_REQUIRE(p);
		return std::vector<Rgba>(p, p + SIZE * SIZE);
	}

	std::vector<Rgba> downloaded(std::shared_ptr<RenderTarget> const &of) const
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

BOOST_AUTO_TEST_SUITE(vulkan_draw_lists)

BOOST_AUTO_TEST_CASE(lists_recorded_at_once_draw_what_one_list_draws_alone)
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

BOOST_AUTO_TEST_CASE(passes_of_lists_frame_after_frame_and_two_in_a_frame)
{
	// The lists' command buffers are the frame's share's: recorded over when the share comes round again, frames in flight or not.
	// A pass of one list (inline_pass) and of several follow each other in a frame.
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

BOOST_AUTO_TEST_CASE(lists_are_drawn_in_their_order_not_the_order_they_were_recorded_in)
{
	Rig rig;
	BOOST_REQUIRE_GE(rig.renderer->max_draw_lists(), 8u);
	// the last tile's quad covers its viewport, here the target: yellow, and the same quad red
	std::shared_ptr<DrawCall> const yellow = rig.tile_call(TILES - 1, 0);
	std::shared_ptr<DrawCall> const red = rig.tile_call(TILES - 1, 0);
	red->set_material(rig.material(0));
	red->commit();
	rig.renderer->begin_frame();
	rig.renderer->begin_pass(rig.target, Rig::clear(), 0, 2);
	yellow->execute(rig.renderer->draw_list(1));       // list 1 first ...
	red->execute();                                    // ... then the renderer's own, list 0: drawn under list 1's
	rig.renderer->end_pass();
	rig.renderer->finish_frame();
	std::vector<Rgba> const picture = rig.downloaded(rig.target);
	for (size_t pixel : {size_t(0), size_t(SIZE / 2) * SIZE + SIZE / 2, size_t(SIZE) * SIZE - 1})
	{
		BOOST_CHECK_EQUAL(picture[pixel].r, 1.0f);
		BOOST_CHECK_EQUAL(picture[pixel].g, 1.0f);
		BOOST_CHECK_EQUAL(picture[pixel].b, 0.0f);
	}
}

BOOST_AUTO_TEST_CASE(a_list_of_a_pass_draws_nothing_of_what_it_was_not_given)
{
	// list 1 draws the whole target, lists 0 and 2 nothing: the target is list 1's colour, and a list left empty is no error
	Rig rig;
	rig.renderer->begin_frame();
	rig.renderer->begin_pass(rig.target, Rig::clear(), 0, 3);
	rig.tile_call(TILES - 1, 0)->execute(rig.renderer->draw_list(1));
	rig.renderer->end_pass();
	rig.renderer->finish_frame();
	std::vector<Rgba> const picture = rig.downloaded(rig.target);
	BOOST_CHECK_EQUAL(picture[0].g, 1.0f);
	BOOST_CHECK_EQUAL(picture[size_t(SIZE) * SIZE - 1].g, 1.0f);
}

BOOST_AUTO_TEST_CASE(a_material_changed_inside_a_pass_draws_as_it_is_now)
{
	// A list remembers what a material draws with (its pipeline and descriptor set) for its next draws of it; a material changed
	// between two draws of one list is drawn the second time with its new texture, in a pass of one list or of several.
	for (size_t lists : {size_t(1), size_t(2)})
	{
		Rig rig;
		std::shared_ptr<Material> const material = rig.material(0);       // red
		std::shared_ptr<DrawCall> const call = rig.tile_call(TILES - 1, 0);
		call->set_material(material);
		call->commit();
		rig.renderer->begin_frame();
		rig.renderer->begin_pass(rig.target, Rig::clear(), 0, lists);
		DrawList &list = rig.renderer->draw_list(lists - 1);
		list.set_viewport(0, 0, SIZE / 2, SIZE);
		call->execute(list);
		material->set_diffuse_texture(rig.textures[1]);                    // green
		list.set_viewport(SIZE / 2, 0, SIZE / 2, SIZE);
		call->execute(list);
		rig.renderer->end_pass();
		rig.renderer->finish_frame();
		std::vector<Rgba> const picture = rig.downloaded(rig.target);
		Rgba const left = picture[size_t(SIZE / 2) * SIZE + SIZE / 4], right = picture[size_t(SIZE / 2) * SIZE + 3 * SIZE / 4];
		BOOST_CHECK_MESSAGE(left.r == 1.0f && left.g == 0.0f, lists << " lists: the left half is not red");
		BOOST_CHECK_MESSAGE(right.r == 0.0f && right.g == 1.0f, lists << " lists: the right half is not green");
	}
}

BOOST_AUTO_TEST_CASE(a_list_kept_for_the_next_pass_looks_its_materials_up_again)
{
	// The renderer keeps its lists from pass to pass, and what a list remembered a material drew with was the last pass's: another
	// render pass, another pass type.  One material, unchanged, samples its red texture in pass type 0 and is white in pass type 1.
	for (size_t lists : {size_t(1), size_t(2)})
	{
		Rig rig;
		std::shared_ptr<Material> const material = rig.material(0);
		material->set_shader_program(1, rig.renderer->create_shader_program(edge::test_shader("test_frame.vert"),
			edge::test_shader("test_vertex_texture.frag")));
		std::shared_ptr<DrawCall> const call = rig.tile_call(TILES - 1, 0);
		call->set_material(material);
		call->commit();
		rig.renderer->begin_frame();
		rig.renderer->begin_pass(rig.target, Rig::clear(), 0, lists);
		call->execute(rig.renderer->draw_list(lists - 1));
		rig.renderer->end_pass();
		rig.renderer->begin_pass(rig.other, Rig::clear(), 1, lists);
		call->execute(rig.renderer->draw_list(lists - 1));
		rig.renderer->end_pass();
		rig.renderer->finish_frame();
		size_t const centre = size_t(SIZE / 2) * SIZE + SIZE / 2;
		Rgba const sampled = rig.downloaded(rig.target)[centre], white = rig.downloaded(rig.other)[centre];
		BOOST_CHECK_MESSAGE(sampled.r == 1.0f && sampled.g == 0.0f && sampled.b == 0.0f, lists << " lists: pass type 0 is not red");
		BOOST_CHECK_MESSAGE(white.r == 1.0f && white.g == 1.0f && white.b == 1.0f, lists << " lists: pass type 1 is not white");
	}
}

BOOST_AUTO_TEST_SUITE_END()
