// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of the frames in flight.  submit_frame() hands a frame to the GPU and returns without waiting for it, so the host
// builds the next frame while the GPU draws this one.  A renderer made with N frames in flight keeps N frames' worth of what the
// host writes for a frame (the frame constants, the draw constants, the bytes staged for uploads) apart, and waits for a frame
// only when its share is needed again: a frame the GPU has not drawn yet is never overwritten.  finish_frame() still waits for
// the frame, and what is read after it (a download, the GPU's times) is that frame's: what every reader of pixels relies on.
//
// The frames here are heavy (hundreds of draws over the whole target), so the GPU is still drawing one when the host has built
// the next: a frame that read another's bytes shows the other's colour or position.  Each frame draws into a target of its own,
// read back once all of them were submitted.

#include <boost/test/unit_test.hpp>
#include "test_fixture.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "shader_program.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"
#include "vulkan_renderer.h"

using namespace edge;

namespace
{

int const SIZE = 128;
int const FRAMES = 8;
int const LOAD = 400;        // draws over the whole target in every frame

struct Rgba
{
	float r, g, b, a;
};

bool near(float a, float b)
{
	return std::fabs(a - b) < 1.0e-3f;
}

VulkanRendererPtr renderer_with(int frames_in_flight)
{
	VulkanRendererPtr renderer(new VulkanRenderer(64, 64, std::string(), std::vector<std::string>(), frames_in_flight));
	return renderer;
}

/// A quad over x0..x1 of the target and all of its height, UVs 0..1 across it.
VertexBufferPtr quad(VulkanRendererPtr const &renderer, float x0, float x1)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * 4);
	auto vb_mapping = vb->write(Contents::DISCARD);
	VERTEX_PT *v = vb_mapping.as<VERTEX_PT>().data();
	BOOST_REQUIRE(v);
	v[0].position = rove::vec<3>(x0, -1.0f, 0.0f); v[0].texcoords = rove::vec<2>(0, 0);
	v[1].position = rove::vec<3>(x1, -1.0f, 0.0f); v[1].texcoords = rove::vec<2>(1, 0);
	v[2].position = rove::vec<3>(x1,  1.0f, 0.0f); v[2].texcoords = rove::vec<2>(1, 1);
	v[3].position = rove::vec<3>(x0,  1.0f, 0.0f); v[3].texcoords = rove::vec<2>(0, 1);
	vb_mapping.unmap();
	return vb;
}

/// The band of the target frame k of FRAMES draws its marker in.
float band_x0(int k) { return -1.0f + 2.0f * float(k) / FRAMES; }
float band_x1(int k) { return -1.0f + 2.0f * float(k + 1) / FRAMES; }
int band_column(int k) { return (2 * k + 1) * SIZE / (2 * FRAMES); }

struct Rig
{
	VulkanRendererPtr renderer;
	CameraPtr camera;
	IndexBufferPtr indices;
	VertexBufferPtr whole;
	MaterialPtr constants;       // the frame constants' first vec4, plus the second's x on red
	MaterialPtr red;             // a red texture
	TexturePtr red_texture;
	std::vector<RenderTargetPtr> targets;
	RenderTargetPtr scratch;     // where busy() keeps the GPU drawing

	explicit Rig(int frames_in_flight):
		renderer(renderer_with(frames_in_flight))
	{
		camera = std::make_shared<Camera>();
		camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
		camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
		indices = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
		auto indices_mapping = indices->write(Contents::DISCARD);
		INDEX_U16 *idx = indices_mapping.as<INDEX_U16>().data();
		BOOST_REQUIRE(idx);
		idx[0] = 0; idx[1] = 1; idx[2] = 2; idx[3] = 0; idx[4] = 2; idx[5] = 3;
		indices_mapping.unmap();
		whole = quad(renderer, -1.0f, 1.0f);
		constants = material(edge::test_shader("test_frame_constants.frag"));
		red_texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
		auto red_texture_mapping = red_texture->write(Contents::DISCARD);
		uint32_t *p = red_texture_mapping.as<uint32_t>().data();
		BOOST_REQUIRE(p);
		for (int i = 0; i < 16; ++i)
			p[i] = 0xFFFF0000u;
		red_texture_mapping.unmap();
		red = material(edge::test_shader("test_sample.frag"));
		red->set_diffuse_texture(red_texture);
		for (int k = 0; k < FRAMES; ++k)
			targets.push_back(renderer->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F));
		scratch = renderer->create_render_target(1024, 1024, PixelFormat::R32G32B32A32F);
	}

	/// A pass of the load alone, into the scratch target: what is recorded after it runs late in the GPU's frame.
	void busy() const
	{
		renderer->begin_pass(scratch);
		DrawCallPtr const load = call(whole, constants);
		for (int i = 0; i < LOAD; ++i)
			load->execute();
		renderer->end_pass();
	}

	MaterialPtr material(std::string const &fragment) const
	{
		MaterialPtr m = renderer->create_material();
		m->set_cull_mode(Material::CullMode::NONE);
		m->set_shader_program(renderer->create_shader_program(edge::test_shader("test_frame.vert"), fragment));
		return m;
	}

	DrawCallPtr call(VertexBufferPtr const &vb, MaterialPtr const &m, rove::matrix<4, 4> const *world = nullptr) const
	{
		DrawCallPtr c = renderer->create_draw_call();
		if (world)
			c->set_world_matrix(*world);
		c->set_vertex_buffer(vb);
		c->set_index_buffer(indices);
		c->set_material(m);
		c->set_camera(camera);
		c->set_triangles_count(2);
		c->set_always_visible(true);
		c->commit();
		return c;
	}

	/// Frame k's pass into its target, after a busy() one: the load, in the frame constants' colour, then what `then` draws over it.
	void pass(int k, std::vector<DrawCallPtr> const &then = {}) const
	{
		busy();
		RenderPassClearState clear;
		clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = -1.0f;
		renderer->begin_pass(targets[k], clear);
		DrawCallPtr const load = call(whole, constants);
		for (int i = 0; i < LOAD; ++i)
			load->execute();
		for (DrawCallPtr const &c : then)
			c->execute();
		renderer->end_pass();
	}

	/// Every target's pixels, downloaded together (which waits for every frame submitted).
	std::vector<std::vector<Rgba>> pictures() const
	{
		std::vector<TexturePtr> textures;
		for (RenderTargetPtr const &t : targets)
			textures.push_back(t->color_texture());
		renderer->batch_download_textures(textures);
		std::vector<std::vector<Rgba>> out;
		for (TexturePtr const &t : textures)
		{
			auto t_mapping = t->read();
			Rgba const *pixels = t_mapping.as<Rgba const>().data();
			BOOST_REQUIRE(pixels);
			out.emplace_back(pixels, pixels + SIZE * SIZE);
			t_mapping.unmap();
		}
		return out;
	}
};

Rgba at(std::vector<Rgba> const &picture, int x, int y)
{
	return picture[size_t(y) * SIZE + size_t(x)];
}

bool is_red(Rgba const &c)
{
	return c.r > 0.9f && c.g < 0.1f && c.b < 0.1f;
}

/// Frame k's band in every target, red where (and only where) that target is frame j's: the shape every geometry/draw-constants/
/// dropped-resource check below ends on, a renderer's targets having been drawn in some order that put each frame's marker (and
/// no other frame's) in its own band.
void check_each_targets_band_is_only_its_own(std::vector<std::vector<Rgba>> const &pictures, int n, char const *what)
{
	for (int k = 0; k < FRAMES; ++k)
		for (int j = 0; j < FRAMES; ++j)
			BOOST_CHECK_MESSAGE(is_red(at(pictures[k], band_column(j), SIZE / 2)) == (j == k),
				n << " in flight, " << what << ": frame " << k << (j == k ? " lacks its marker in band " : " has a marker in band ") << j);
}

/// Every frame k set its own frame constants right before being recorded: the picture it ends up in must show exactly those,
/// however the frame was ended.
void check_frame_sees_its_own_constants(Rig const &rig, int n, bool submit)
{
	for (int k = 0; k < FRAMES; ++k)
	{
		float constants[8] = {0.1f * float(k), 1.0f - 0.1f * float(k), 0.5f, 1.0f, 0, 0, 0, 0};
		rig.renderer->set_frame_constants(constants, sizeof(constants));
		rig.renderer->begin_frame();
		rig.pass(k);
		submit ? rig.renderer->submit_frame() : rig.renderer->finish_frame();
	}
	std::vector<std::vector<Rgba>> const pictures = rig.pictures();
	for (int k = 0; k < FRAMES; ++k)
	{
		Rgba const c = at(pictures[k], SIZE / 2, SIZE / 2);
		BOOST_CHECK_MESSAGE(near(c.r, 0.1f * float(k)) && near(c.g, 1.0f - 0.1f * float(k)) && near(c.b, 0.5f),
			n << " in flight, " << (submit ? "submitted" : "finished") << ": frame " << k << " shows " << c.r << ", " << c.g << ", " << c.b);
	}
}

/// Constants set before the first frame only: every frame after, whichever share of the host's memory it is built in, still
/// sees them.
void check_constants_set_once_persist(Rig const &rig, int n)
{
	float constants[8] = {0.25f, 0.5f, 0.75f, 1.0f, 0.125f, 0, 0, 0};
	rig.renderer->set_frame_constants(constants, sizeof(constants));
	for (int k = 0; k < FRAMES; ++k)
	{
		rig.renderer->begin_frame();
		rig.pass(k);
		rig.renderer->submit_frame();
	}
	std::vector<std::vector<Rgba>> const pictures = rig.pictures();
	for (int k = 0; k < FRAMES; ++k)
	{
		Rgba const c = at(pictures[k], SIZE / 2, SIZE / 2);
		BOOST_CHECK_MESSAGE(near(c.r, 0.375f) && near(c.g, 0.5f) && near(c.b, 0.75f),
			n << " in flight: frame " << k << " shows " << c.r << ", " << c.g << ", " << c.b);
	}
}

/// One vertex buffer, moved to frame k's band inside frame k after a heavy pass: the copy runs late in the GPU's frame, while
/// the host stages the next frames' bytes.
void check_geometry_written_between_frames_is_isolated(Rig const &rig, int n)
{
	VertexBufferPtr const marker = quad(rig.renderer, band_x0(0), band_x1(0));
	// staged first, larger each frame: a frame's bulk reaches over where the frames before it put their marker
	std::vector<VertexBufferPtr> bulks;
	for (int k = 0; k < FRAMES; ++k)
	{
		size_t const bulk_size = sizeof(VERTEX_PT) * 256 * size_t(k + 1);
		bulks.push_back(rig.renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), bulk_size));
		rig.renderer->begin_frame();
		rig.busy();
		auto back_mapping = bulks.back()->write(Contents::DISCARD);
		void *b = back_mapping.data();
		BOOST_REQUIRE(b);
		std::memset(b, 0, bulk_size);
		back_mapping.unmap();
		auto marker_mapping = marker->write(Contents::DISCARD);
		VERTEX_PT *v = marker_mapping.as<VERTEX_PT>().data();
		BOOST_REQUIRE(v);
		v[0].position = rove::vec<3>(band_x0(k), -1.0f, 0.0f); v[0].texcoords = rove::vec<2>(0, 0);
		v[1].position = rove::vec<3>(band_x1(k), -1.0f, 0.0f); v[1].texcoords = rove::vec<2>(1, 0);
		v[2].position = rove::vec<3>(band_x1(k),  1.0f, 0.0f); v[2].texcoords = rove::vec<2>(1, 1);
		v[3].position = rove::vec<3>(band_x0(k),  1.0f, 0.0f); v[3].texcoords = rove::vec<2>(0, 1);
		marker_mapping.unmap();
		rig.pass(k, {rig.call(marker, rig.red)});
		rig.renderer->submit_frame();
	}
	check_each_targets_band_is_only_its_own(rig.pictures(), n, "geometry written between frames");
}

/// The draw constants are written while the frame is recorded: frame k's must not land on those of a frame still drawn.
void check_each_draw_keeps_its_world_matrix(Rig const &rig, int n)
{
	VertexBufferPtr const marker = quad(rig.renderer, band_x0(0), band_x1(0));
	for (int k = 0; k < FRAMES; ++k)
	{
		rove::matrix<4, 4> world;
		world.translation(2.0f * float(k) / FRAMES, 0.0f, 0.0f);
		rig.renderer->begin_frame();
		rig.pass(k, {rig.call(marker, rig.red, &world)});
		rig.renderer->submit_frame();
	}
	check_each_targets_band_is_only_its_own(rig.pictures(), n, "world matrix");
}

/// A buffer, a texture and a material used by frame k, let go of while the GPU may still draw it; the frames after draw on.
void check_dropped_resources_do_not_affect_frames_in_flight(Rig const &rig, int n)
{
	for (int k = 0; k < FRAMES; ++k)
	{
		VertexBufferPtr marker = quad(rig.renderer, band_x0(k), band_x1(k));
		TexturePtr texture = rig.renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
		auto texture_mapping = texture->write(Contents::DISCARD);
		uint32_t *p = texture_mapping.as<uint32_t>().data();
		BOOST_REQUIRE(p);
		for (int i = 0; i < 16; ++i)
			p[i] = 0xFFFF0000u;
		texture_mapping.unmap();
		MaterialPtr m = rig.material(edge::test_shader("test_sample.frag"));
		m->set_diffuse_texture(texture);
		rig.renderer->begin_frame();
		rig.pass(k, {rig.call(marker, m)});
		rig.renderer->submit_frame();
		marker.reset();
		m.reset();
		texture.reset();
	}
	check_each_targets_band_is_only_its_own(rig.pictures(), n, "dropped resources");
}

}

BOOST_AUTO_TEST_SUITE(vulkan_frames_in_flight)

BOOST_AUTO_TEST_CASE(a_renderer_keeps_one_to_four_frames_in_flight)
{
	for (int n : {1, 2, 3, 4})
		BOOST_CHECK_EQUAL(renderer_with(n)->frames_in_flight(), n);
	BOOST_CHECK_EQUAL(std::make_shared<VulkanRenderer>(64, 64)->frames_in_flight(), 1);       // the default: as before
	for (int n : {0, -1, 5})
		BOOST_CHECK_THROW(renderer_with(n), std::invalid_argument);
}

// Building a VulkanRenderer (a Rig's share of it) is itself most of what used to make this suite slow: a real device and
// queue, not a mock.  The checks below used to each build one afresh per frames-in-flight count (and, for some, per submit
// mode too); they build one Rig per count instead and run every check that count needs against it, since nothing any of them
// does leaves a mark the next one cares about (every one of them clears and redraws every target it reads, and downloads what
// it drew before the next check touches those targets again).
BOOST_AUTO_TEST_CASE(a_renderer_keeps_each_frames_share_apart)
{
	for (int n : {1, 2, 3})
	{
		Rig rig(n);
		for (bool submit : {true, false})
			check_frame_sees_its_own_constants(rig, n, submit);
		check_constants_set_once_persist(rig, n);
		check_geometry_written_between_frames_is_isolated(rig, n);
		check_each_draw_keeps_its_world_matrix(rig, n);
		check_dropped_resources_do_not_affect_frames_in_flight(rig, n);
	}
	// frame_constants_set_once_reach_every_later_frame alone also covers 4 frames in flight, the most this renderer allows,
	// not just the 1-3 every check above does.
	Rig rig(4);
	check_constants_set_once_persist(rig, 4);
}

BOOST_AUTO_TEST_CASE(a_submitted_frame_costs_one_submit_as_a_finished_one_does)
{
	// a frame and the download of what it drew: two submits, however the frame was ended (a lockstep simulation counts them)
	Rig rig(3);
	for (bool submit : {true, false})
	{
		uint64_t const before = rig.renderer->single_time_submit_count();
		rig.renderer->begin_frame();
		rig.pass(0);
		submit ? rig.renderer->submit_frame() : rig.renderer->finish_frame();
		BOOST_CHECK_EQUAL(rig.renderer->single_time_submit_count(), before + 1);
		rig.renderer->batch_download_textures({rig.targets[0]->color_texture()});
		BOOST_CHECK_EQUAL(rig.renderer->single_time_submit_count(), before + 2);
	}
}

BOOST_AUTO_TEST_CASE(the_gpu_times_name_the_frame_they_belong_to)
{
	// frame k has 2 (k % 3 + 1) passes (each pass() is a busy() one and its own): times read after a later frame was submitted
	// are those of the frame they say
	Rig rig(3);
	rig.renderer->set_gpu_trace_enabled(true);
	BOOST_CHECK_EQUAL(rig.renderer->last_pass_times_frame(), 0u);
	std::vector<size_t> passes(1, 0);         // by frame number, from 1
	for (int k = 0; k < FRAMES; ++k)
	{
		rig.renderer->begin_frame();
		BOOST_CHECK_EQUAL(rig.renderer->frame_serial(), uint64_t(k + 1));
		for (int p = 0; p <= k % 3; ++p)
			rig.pass(k);
		passes.push_back(size_t(2 * (k % 3 + 1)));
		rig.renderer->submit_frame();
		uint64_t const timed = rig.renderer->last_pass_times_frame();
		BOOST_CHECK_LE(timed, uint64_t(k + 1));
		if (timed)
			BOOST_CHECK_EQUAL(rig.renderer->last_pass_times_us().size(), passes[timed]);
	}
	rig.renderer->wait_frames();
	BOOST_CHECK_EQUAL(rig.renderer->last_pass_times_frame(), uint64_t(FRAMES));
	BOOST_CHECK_EQUAL(rig.renderer->last_pass_times_us().size(), passes[FRAMES]);
	// any of the last frames drawn, by its number: an application that renders a scene and a screen asks for the scene's
	for (uint64_t frame = 1; frame <= uint64_t(FRAMES); ++frame)
	{
		std::optional<std::vector<double>> const times = rig.renderer->pass_times_us(frame);
		BOOST_REQUIRE_MESSAGE(times, "frame " << frame << " has no times");
		BOOST_CHECK_EQUAL(times->size(), passes[frame]);
	}
	BOOST_CHECK(!rig.renderer->pass_times_us(0));
	BOOST_CHECK(!rig.renderer->pass_times_us(uint64_t(FRAMES) + 1));      // not even begun
	// a finished frame's times are its own, at once
	rig.renderer->begin_frame();
	rig.pass(0);
	rig.pass(0);
	rig.renderer->finish_frame();
	BOOST_CHECK_EQUAL(rig.renderer->last_pass_times_frame(), rig.renderer->frame_serial());
	BOOST_CHECK_EQUAL(rig.renderer->last_pass_times_us().size(), 4u);
}

BOOST_AUTO_TEST_CASE(the_sets_of_dropped_materials_are_freed_once_their_frames_are_drawn)
{
	// a new material every frame, dropped after it: the sets held stay bounded by the frames in flight, not by the frames drawn
	Rig rig(3);
	size_t const base = rig.renderer->material_descriptor_set_count();
	for (int k = 0; k < 200; ++k)
	{
		MaterialPtr m = rig.material(edge::test_shader("test_sample.frag"));
		m->set_diffuse_texture(rig.red_texture);
		rig.renderer->begin_frame();
		RenderPassClearState clear;
		rig.renderer->begin_pass(rig.targets[0], clear);
		rig.call(rig.whole, m)->execute();
		rig.renderer->end_pass();
		rig.renderer->submit_frame();
		BOOST_CHECK_LE(rig.renderer->material_descriptor_set_count(), base + 3 + 2);
	}
}

BOOST_AUTO_TEST_CASE(wait_frames_waits_for_every_frame_submitted)
{
	Rig rig(3);
	rig.renderer->wait_frames();          // nothing submitted: returns
	for (int k = 0; k < 3; ++k)
	{
		rig.renderer->begin_frame();
		rig.pass(k);
		rig.renderer->submit_frame();
	}
	rig.renderer->wait_frames();
	// every frame drawn: a download submits once and reads them
	uint64_t const before = rig.renderer->single_time_submit_count();
	std::vector<std::vector<Rgba>> const pictures = rig.pictures();
	BOOST_CHECK_EQUAL(rig.renderer->single_time_submit_count(), before + 1);
	BOOST_CHECK_EQUAL(pictures.size(), size_t(FRAMES));
	// inside a frame there is one not submitted yet to wait for
	rig.renderer->begin_frame();
	BOOST_CHECK_THROW(rig.renderer->wait_frames(), std::logic_error);
	rig.renderer->finish_frame();
}

BOOST_AUTO_TEST_SUITE_END()
