// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of render targets beyond one colour and a depth (Renderer::create_render_target with a RenderTargetFormat): several colour
// attachments, each written by its own fragment output and each a texture of its own; a target of no colour, only a readable depth; targets
// of layers, an array or the six faces of a cube, each layer drawn by a pass of its own into RenderTarget::layer(i) and sampled as an array
// or a cube (with a comparison, for a readable depth); and what a renderer refuses to make.  Every pixel claim renders on Vulkan and on
// OpenGL ES and reads the pixels back, the textures sampled by a later pass as a game's shader samples them.

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "camera.h"
#include "draw_call.h"
#include "image.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "shader_program.h"
#include "test_backends.h"
#include "test_fixture.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"
#ifdef EDGE_USE_VULKAN
#include "vulkan_render_target.h"
#include "vulkan_texture.h"
#endif
#ifdef EDGE_USE_GLES
#include "gles_render_target.h"
#endif

using namespace edge;

namespace
{

int const SIZE = 32;
float const PI = 3.14159265f;

using Rgba = rove::rgba<float>;

/// Eye at z = 1 looking down -z, orthographic over [-1, 1]^2, near 0, far 2: a point's depth is half its distance from the eye.
CameraPtr flat_view()
{
	CameraPtr camera(new Camera());
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
	camera->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	return camera;
}

MaterialPtr material(RendererPtr const &renderer, std::string const &vertex, std::string const &fragment)
{
	MaterialPtr m = renderer->create_material();
	m->set_cull_mode(Material::CullMode::NONE);
	m->set_shader_program(renderer->create_shader_program(test_shader(vertex), test_shader(fragment)));
	return m;
}

DrawCallPtr call(RendererPtr const &renderer, MaterialPtr const &material, CameraPtr const &camera, std::vector<VERTEX_PT> const &vertices,
	std::vector<INDEX_U16> const &indices)
{
	VertexBufferPtr vb = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PT>(), sizeof(VERTEX_PT) * vertices.size());
	auto vb_mapping = vb->write(Contents::DISCARD);
	std::copy(vertices.begin(), vertices.end(), vb_mapping.as<VERTEX_PT>().data());
	vb_mapping.unmap();
	IndexBufferPtr ib = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * indices.size());
	auto ib_mapping = ib->write(Contents::DISCARD);
	std::copy(indices.begin(), indices.end(), ib_mapping.as<INDEX_U16>().data());
	ib_mapping.unmap();
	DrawCallPtr draw_call = renderer->create_draw_call();
	draw_call->set_vertex_buffer(vb);
	draw_call->set_index_buffer(ib);
	draw_call->set_material(material);
	draw_call->set_camera(camera);
	draw_call->set_triangles_count(static_cast<unsigned>(indices.size() / 3));
	draw_call->set_always_visible(true);
	draw_call->commit();
	return draw_call;
}

/// A quad across the whole width of flat_view(), `half_height` clip units above and below the middle, at z (depth (1 - z) / 2).
DrawCallPtr quad(RendererPtr const &renderer, MaterialPtr const &material, CameraPtr const &camera, float half_height, float z)
{
	std::vector<VERTEX_PT> v(4);
	float const xy[4][2] = {{-1, -half_height}, {1, -half_height}, {1, half_height}, {-1, half_height}};
	for (int i = 0; i < 4; ++i)
	{
		v[i].position = rove::vec<3>(xy[i][0], xy[i][1], z);
		v[i].texcoords = rove::vec<2>(0.5f, 0.5f);
	}
	return call(renderer, material, camera, v, {0, 1, 2, 0, 2, 3});
}

/// The six faces of the box of half side `half` about the origin.
DrawCallPtr box(RendererPtr const &renderer, MaterialPtr const &material, CameraPtr const &camera, float half)
{
	std::vector<VERTEX_PT> v(8);
	for (int i = 0; i < 8; ++i)
	{
		v[i].position = rove::vec<3>(i & 1 ? half : -half, i & 2 ? half : -half, i & 4 ? half : -half);
		v[i].texcoords = rove::vec<2>(0, 0);
	}
	std::vector<INDEX_U16> const faces = {0, 1, 3, 0, 3, 2,  4, 5, 7, 4, 7, 6,  0, 1, 5, 0, 5, 4,  2, 3, 7, 2, 7, 6,  0, 2, 6, 0, 6, 4,
		1, 3, 7, 1, 7, 5};
	return call(renderer, material, camera, v, faces);
}

/// A pass into `target`, cleared as `clear` says, that draws `drawn`.
void pass(RendererPtr const &renderer, RenderTargetPtr const &target, DrawCallPtr const &drawn,
	RenderPassClearState const &clear = RenderPassClearState())
{
	renderer->begin_pass(target, clear);
	drawn->execute();
	renderer->end_pass();
}

/// The direction of pixel (x, y) of test_sample_cube.frag's latitude-longitude map of w x h pixels.
rove::vec<3> map_direction(int x, int y, int w, int h)
{
	float const longitude = ((x + 0.5f) / w * 2 - 1) * PI, latitude = (0.5f - (y + 0.5f) / h) * PI;
	return rove::vec<3>(std::cos(latitude) * std::sin(longitude), std::sin(latitude), std::cos(latitude) * std::cos(longitude));
}

bool mentions(std::exception const &e, std::string const &what)
{
	return std::string(e.what()).find(what) != std::string::npos;
}

}

BOOST_AUTO_TEST_SUITE(render_target_attachments)

/// The target of one colour format and a depth (the create_render_target every client calls) is as it was: one colour texture, which is
/// colour attachment 0, no layers, no cube, drawn into as a whole.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_target_of_one_colour_format_is_what_it_always_was, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	for (bool readable : {false, true})
	{
		RenderTargetPtr const target = renderer->create_render_target(24, 16, PixelFormat::R32G32B32A32F, readable);
		BOOST_REQUIRE_EQUAL(target->color_textures_count(), 1u);
		TexturePtr const color = target->color_texture();
		BOOST_REQUIRE(color);
		BOOST_CHECK(target->color_texture(0) == color);
		BOOST_CHECK_EQUAL(color->pixel_format(), PixelFormat::R32G32B32A32F);
		BOOST_CHECK_EQUAL(color->width(), 24);
		BOOST_CHECK_EQUAL(color->height(), 16);
		BOOST_CHECK_EQUAL(color->array_layers(), 0);
		BOOST_CHECK(!color->is_cube());
		BOOST_CHECK_THROW(target->color_texture(1), std::out_of_range);
		BOOST_CHECK_EQUAL(bool(target->depth_texture()), readable);
		BOOST_CHECK_EQUAL(target->layers(), 0);
		BOOST_CHECK(!target->is_cube());
		BOOST_CHECK_EQUAL(target->layer_index(), -1);
		BOOST_CHECK_THROW(target->layer(0), std::out_of_range);
	}
}

/// Three attachments of three formats, written at once by test_mrt.frag over the middle rows: each holds its own output at every pixel
/// drawn (none another's, none shifted), and the clear colour around it, read back in one batch.
BOOST_AUTO_TEST_CASE_TEMPLATE(each_colour_attachment_holds_what_its_own_output_wrote, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetFormat format;
	format.color_formats = {PixelFormat::R32G32B32A32F, PixelFormat::A8R8G8B8, PixelFormat::R32F};
	RenderTargetPtr const target = renderer->create_render_target(SIZE, SIZE, format);
	BOOST_REQUIRE_EQUAL(target->color_textures_count(), 3u);
	CameraPtr const camera = flat_view();
	MaterialPtr const m = material(renderer, "test_frame.vert", "test_mrt.frag");
	m->set_extra_parameter(0, rove::vec<4>(0.75f, float(SIZE), float(SIZE), 0.25f));
	RenderPassClearState clear;
	clear.clear_color_r = 0.2f;
	clear.clear_color_g = 0.4f;
	clear.clear_color_b = 0.6f;
	clear.clear_color_a = 0.8f;
	renderer->begin_frame();
	pass(renderer, target, quad(renderer, m, camera, 0.5f, 0.0f), clear);
	renderer->finish_frame();
	renderer->batch_download_textures(target->color_textures());

	Image<Rgba> const first(target->color_texture(0)->pixels<Rgba>());
	Image<rove::bgra<uint8_t>> const second(target->color_texture(1)->pixels<rove::bgra<uint8_t>>());
	Image<float> const third(target->color_texture(2)->pixels<float>());
	int wrong = 0;
	for (int y = 0; y < SIZE; ++y)
		for (int x = 0; x < SIZE; ++x)
		{
			bool const drawn = y >= SIZE / 4 + 1 && y < SIZE * 3 / 4 - 1, clear_row = y < SIZE / 4 - 1 || y >= SIZE * 3 / 4 + 1;
			if (!drawn && !clear_row)
				continue;       // the quad's own edge rows
			float const cx = x + 0.5f, cy = y + 0.5f;
			Rgba const a = first.at(x, y);
			rove::bgra<uint8_t> const b = second.at(x, y);
			float const c = third.at(x, y);
			bool ok;
			if (drawn)
				ok = a.r == cx && a.g == cy && a.b == 0.75f && a.a == 1.0f
					&& std::abs(b.r - cx / SIZE * 255) <= 1 && std::abs(b.g - cy / SIZE * 255) <= 1 && std::abs(b.b - 0.25f * 255) <= 1
					&& b.a == 255 && c == 1000 * cx + cy;
			else
				ok = a.r == 0.2f && a.g == 0.4f && a.b == 0.6f && a.a == 0.8f
					&& std::abs(b.r - 0.2f * 255) <= 1 && std::abs(b.g - 0.4f * 255) <= 1 && std::abs(b.b - 0.6f * 255) <= 1
					&& std::abs(b.a - 0.8f * 255) <= 1 && c == 0.2f;
			if (!ok && wrong++ < 5)
				BOOST_ERROR("pixel (" << x << ", " << y << ") " << (drawn ? "drawn" : "cleared") << ": " << a.r << " " << a.g << " " << a.b
					<< " " << a.a << " | " << int(b.r) << " " << int(b.g) << " " << int(b.b) << " " << int(b.a) << " | " << c);
		}
	BOOST_CHECK_EQUAL(wrong, 0);
}

/// Each attachment is a texture a later pass of the same frame samples (test_depth.frag's texelFetch, red into red).
BOOST_AUTO_TEST_CASE_TEMPLATE(a_later_pass_samples_each_attachment, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetFormat format;
	format.color_formats = {PixelFormat::R32G32B32A32F, PixelFormat::A8R8G8B8, PixelFormat::R32F};
	RenderTargetPtr const target = renderer->create_render_target(SIZE, SIZE, format);
	CameraPtr const camera = flat_view();
	MaterialPtr const m = material(renderer, "test_frame.vert", "test_mrt.frag");
	m->set_extra_parameter(0, rove::vec<4>(0.75f, float(SIZE), float(SIZE), 0.25f));
	std::vector<RenderTargetPtr> copies;
	std::vector<DrawCallPtr> covers;
	for (int i = 0; i < 3; ++i)
	{
		copies.push_back(renderer->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F));
		MaterialPtr const reader = material(renderer, "test_frame.vert", "test_depth.frag");
		reader->set_diffuse_texture(target->color_texture(i));
		covers.push_back(quad(renderer, reader, camera, 1.0f, 0.0f));
	}
	renderer->begin_frame();
	pass(renderer, target, quad(renderer, m, camera, 1.0f, 0.0f));
	for (int i = 0; i < 3; ++i)
		pass(renderer, copies[i], covers[i]);
	renderer->finish_frame();
	for (int i = 0; i < 3; ++i)
	{
		Image<Rgba> const copy = read_image<Rgba>(*copies[i]->color_texture());
		for (int y : {0, 7, SIZE - 1})
			for (int x : {0, 13, SIZE - 1})
			{
				float const want = i == 0 ? x + 0.5f : i == 1 ? std::round((x + 0.5f) / SIZE * 255) / 255 : 1000 * (x + 0.5f) + y + 0.5f;
				BOOST_CHECK_MESSAGE(std::abs(copy.at(x, y).r - want) <= (i == 1 ? 1.0f / 255 : 0.0f),
					"attachment " << i << " at (" << x << ", " << y << "): " << copy.at(x, y).r << ", want " << want);
			}
	}
}

/// What is asked is what is made: a texture of each format, of the target's size, each its own; and what cannot be honoured is refused,
/// naming why: no colour without a readable depth to draw, a block-compressed attachment, more attachments than the device draws.
BOOST_AUTO_TEST_CASE_TEMPLATE(the_attachments_are_made_as_asked_or_refused, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	unsigned const most = renderer->max_color_attachments();
	BOOST_CHECK_GE(most, 4u);          // what Vulkan, OpenGL ES 3.0 and WebGL 2 all promise
	BOOST_TEST_MESSAGE("colour attachments at most: " << most);

	RenderTargetFormat format;
	format.color_formats = {PixelFormat::R16G16B16A16F, PixelFormat::A8R8G8B8};
	format.readable_depth = true;
	RenderTargetPtr const two = renderer->create_render_target(20, 12, format);
	BOOST_REQUIRE_EQUAL(two->color_textures_count(), 2u);
	BOOST_CHECK_EQUAL(two->color_texture(0)->pixel_format(), PixelFormat::R16G16B16A16F);
	BOOST_CHECK_EQUAL(two->color_texture(1)->pixel_format(), PixelFormat::A8R8G8B8);
	BOOST_CHECK(two->color_texture(0) != two->color_texture(1));
	BOOST_CHECK(two->color_texture() == two->color_texture(0));
	for (TexturePtr const &t : two->color_textures())
	{
		BOOST_CHECK_EQUAL(t->width(), 20);
		BOOST_CHECK_EQUAL(t->height(), 12);
		BOOST_CHECK(!t->is_depth());
	}
	BOOST_REQUIRE(two->depth_texture());
	BOOST_CHECK(two->depth_texture()->is_depth());
	BOOST_CHECK_THROW(two->color_texture(2), std::out_of_range);

	format.color_formats.assign(most, PixelFormat::A8R8G8B8);
	BOOST_CHECK_EQUAL(renderer->create_render_target(8, 8, format)->color_textures_count(), most);
	format.color_formats.push_back(PixelFormat::A8R8G8B8);
	try
	{
		renderer->create_render_target(8, 8, format);
		BOOST_ERROR("a target of " << most + 1 << " colour attachments was made");
	}
	catch (std::invalid_argument const &e)
	{
		BOOST_CHECK_MESSAGE(mentions(e, std::to_string(most)), "the refusal does not name the limit: " << e.what());
	}

	format.color_formats = {PixelFormat::A8R8G8B8, PixelFormat::BC7};
	BOOST_CHECK_THROW(renderer->create_render_target(8, 8, format), std::invalid_argument);
	format.color_formats = {};
	format.readable_depth = false;
	BOOST_CHECK_THROW(renderer->create_render_target(8, 8, format), std::invalid_argument);     // nothing to draw into
	format.color_formats = {PixelFormat::A8R8G8B8};
	BOOST_CHECK_THROW(renderer->create_render_target(0, 8, format), std::invalid_argument);
	BOOST_CHECK_THROW(renderer->create_render_target(8, -1, format), std::invalid_argument);
}

/// A target of no colour and a readable depth (a shadow map): its pass keeps the depth of each pixel, which a later pass reads and compares.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_target_of_depth_alone_keeps_the_depth_its_pass_drew, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetFormat format;
	format.readable_depth = true;
	RenderTargetPtr const shadow = renderer->create_render_target(SIZE, SIZE, format);
	BOOST_CHECK_EQUAL(shadow->color_textures_count(), 0u);
	BOOST_CHECK(!shadow->color_texture());
	BOOST_REQUIRE(shadow->depth_texture());
	CameraPtr const camera = flat_view();
	DrawCallPtr const drawn = quad(renderer, material(renderer, "test_frame.vert", "test_no_output.frag"), camera, 0.5f, 0.2f);
	RenderTargetPtr const copy = renderer->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F);
	MaterialPtr const reader = material(renderer, "test_frame.vert", "test_depth.frag");
	reader->set_diffuse_texture(shadow->depth_texture());
	RenderPassClearState clear;
	clear.clear_depth_value = 0.9f;
	renderer->begin_frame();
	pass(renderer, shadow, drawn, clear);
	pass(renderer, copy, quad(renderer, reader, camera, 1.0f, 0.0f));
	renderer->finish_frame();
	Image<Rgba> const depth = read_image<Rgba>(*copy->color_texture());
	BOOST_CHECK_CLOSE(depth.at(5, SIZE / 2).r, 0.4f, 1e-3);          // the quad, at z 0.2
	BOOST_CHECK_CLOSE(depth.at(5, 2).r, 0.9f, 1e-3);                 // the clear above it

	// and compared, as global_shadow_texture samples it
	MaterialPtr const compare = material(renderer, "test_frame.vert", "test_shadow_compare.frag");
	DrawCallPtr const cover = quad(renderer, compare, camera, 1.0f, 0.0f);
	renderer->set_global_shadow_texture(0, shadow->depth_texture());
	for (float reference : {0.3f, 0.5f})
	{
		compare->set_extra_parameter(0, rove::vec<4>(reference, float(SIZE), float(SIZE), 0));
		renderer->begin_frame();
		pass(renderer, shadow, drawn, clear);
		pass(renderer, copy, cover);
		renderer->finish_frame();
		Image<Rgba> const lit = read_image<Rgba>(*copy->color_texture());
		BOOST_CHECK_EQUAL(lit.at(5, SIZE / 2).r, reference <= 0.4f ? 1.0f : 0.0f);
		BOOST_CHECK_EQUAL(lit.at(5, 2).r, 1.0f);
	}
	renderer->set_global_texture(0, TexturePtr());
}

#ifdef EDGE_USE_VULKAN
/// Vulkan: a pass leaves every colour attachment and the readable depth in the layout the passes after it sample them in (NVIDIA draws
/// and samples whatever the layout, so the pixels alone would not tell).
BOOST_AUTO_TEST_CASE(a_vulkan_pass_leaves_every_attachment_to_be_sampled)
{
	VulkanFixture fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	RendererPtr const renderer = fixture.renderer;
	CameraPtr const camera = flat_view();
	DrawCallPtr const drawn = quad(renderer, material(renderer, "test_frame.vert", "test_mrt.frag"), camera, 1.0f, 0.0f);
	for (int layers : {0, 2})
	{
		RenderTargetFormat format;
		format.color_formats = {PixelFormat::R32G32B32A32F, PixelFormat::A8R8G8B8, PixelFormat::R32F};
		format.readable_depth = true;
		format.layers = layers;
		RenderTargetPtr const target = renderer->create_render_target(SIZE, SIZE, format);
		std::vector<TexturePtr> textures = target->color_textures();
		textures.push_back(target->depth_texture());
		renderer->begin_frame();
		pass(renderer, layers ? target->layer(1) : target, drawn);
		renderer->finish_frame();
		for (size_t i = 0; i < textures.size(); ++i)
			BOOST_CHECK_MESSAGE(static_cast<VulkanTexture const &>(*textures[i]).current_layout() == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				"attachment " << i << " of a target of " << layers << " layers is left in layout "
				<< static_cast<VulkanTexture const &>(*textures[i]).current_layout());
	}
}
#endif

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(render_target_layers)

/// A target of layers is drawn a layer at a time: begin_pass takes RenderTarget::layer(i), a target of the parent's size and textures (all
/// their layers) that draws into layer i, and refuses the target itself.  The views are made with it and are the same each time asked.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_target_of_layers_is_drawn_a_layer_at_a_time, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetFormat format;
	format.color_formats = {PixelFormat::R32G32B32A32F, PixelFormat::R32F};
	format.readable_depth = true;
	format.layers = 3;
	RenderTargetPtr const target = renderer->create_render_target(16, 8, format);
	BOOST_CHECK_EQUAL(target->layers(), 3);
	BOOST_CHECK(!target->is_cube());
	BOOST_CHECK_EQUAL(target->layer_index(), -1);
	std::vector<TexturePtr> textures = target->color_textures();
	textures.push_back(target->depth_texture());
	for (TexturePtr const &t : textures)
	{
		BOOST_REQUIRE(t);
		BOOST_CHECK_EQUAL(t->array_layers(), 3);
		BOOST_CHECK(!t->is_cube());
		BOOST_CHECK_THROW(t->download(), std::logic_error);       // an array is sampled, not read back
	}
	BOOST_CHECK(!target->stencil_texture());
	for (int i = 0; i < 3; ++i)
	{
		RenderTargetPtr const layer = target->layer(i);
		BOOST_REQUIRE(layer);
		BOOST_CHECK(layer == target->layer(i));
		BOOST_CHECK_EQUAL(layer->layer_index(), i);
		BOOST_CHECK_EQUAL(layer->layers(), 3);
		BOOST_CHECK_EQUAL(layer->width(), 16);
		BOOST_CHECK_EQUAL(layer->height(), 8);
		BOOST_CHECK(layer->color_textures() == target->color_textures());
		BOOST_CHECK(layer->depth_texture() == target->depth_texture());
		BOOST_CHECK_THROW(layer->layer(0), std::logic_error);
	}
	BOOST_CHECK(target->layer(0) != target->layer(1));
	BOOST_CHECK_THROW(target->layer(3), std::out_of_range);
	BOOST_CHECK_THROW(target->layer(-1), std::out_of_range);

	renderer->begin_frame();
	BOOST_CHECK_THROW(renderer->begin_pass(target), std::invalid_argument);
	BOOST_CHECK(renderer->in_frame() && !renderer->in_pass());       // as it was
	renderer->begin_pass(target->layer(2));
	BOOST_CHECK(renderer->pass_target() == target->layer(2));
	renderer->set_viewport(8, 4, 8, 4);
	BOOST_CHECK_THROW(renderer->set_viewport(8, 4, 9, 4), std::out_of_range);
	renderer->end_pass();
	renderer->finish_frame();

	format.layers = -1;
	BOOST_CHECK_THROW(renderer->create_render_target(16, 8, format), std::invalid_argument);
	format.layers = 1;          // an array of one layer is an array still
	RenderTargetPtr const one = renderer->create_render_target(16, 8, format);
	BOOST_CHECK_EQUAL(one->color_texture()->array_layers(), 1);
	BOOST_CHECK_THROW(one->layer(1), std::out_of_range);
	format.layers = 1 << 20;
	try
	{
		renderer->create_render_target(4, 4, format);
		BOOST_ERROR("a target of 2^20 layers was made");
	}
	catch (std::invalid_argument const &e)
	{
		BOOST_CHECK_MESSAGE(mentions(e, "layers"), "the refusal does not name the limit: " << e.what());
	}
}

/// Three layers of two colours and a depth, each drawn by a pass of its own: a later pass reads each layer's own colours and depth, and a
/// later frame that draws one layer again leaves the others as they were.  The layers are drawn through their views alone, the target
/// itself let go.
BOOST_AUTO_TEST_CASE_TEMPLATE(each_layer_keeps_what_its_own_pass_drew, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetFormat format;
	format.color_formats = {PixelFormat::R32G32B32A32F, PixelFormat::R32F};
	format.readable_depth = true;
	format.layers = 3;
	std::vector<RenderTargetPtr> layers;
	TexturePtr colors, values, depths;
	{
		RenderTargetPtr const target = renderer->create_render_target(SIZE, SIZE, format);
		for (int i = 0; i < 3; ++i)
			layers.push_back(target->layer(i));
		colors = target->color_texture(0);
		values = target->color_texture(1);
		depths = target->depth_texture();
	}
	CameraPtr const camera = flat_view();
	auto const painter = [&](int i, float shade)
	{
		MaterialPtr const m = material(renderer, "test_frame.vert", "test_two_constants.frag");
		m->set_extra_parameter(0, rove::vec<4>(float(i), shade, 0.5f + i, 1.0f));
		m->set_extra_parameter(1, rove::vec<4>(10.0f * i + shade, 0, 0, 0));
		return quad(renderer, m, camera, 1.0f, 0.25f * i);          // depth 0.5, 0.375, 0.25
	};
	RenderTargetPtr const copy = renderer->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F);
	auto const fetch = [&](TexturePtr const &array, int layer)
	{
		MaterialPtr const reader = material(renderer, "test_frame.vert", "test_fetch_array.frag");
		reader->set_diffuse_texture(array);
		reader->set_extra_parameter(0, rove::vec<4>(float(layer), 0, 0, 0));
		return quad(renderer, reader, camera, 1.0f, 0.0f);
	};
	auto const read = [&](TexturePtr const &array, int layer)
	{
		renderer->begin_frame();
		pass(renderer, copy, fetch(array, layer));
		renderer->finish_frame();
		return read_image<Rgba>(*copy->color_texture()).at(SIZE / 3, SIZE / 2);
	};

	renderer->begin_frame();
	for (int i = 0; i < 3; ++i)
		pass(renderer, layers[i], painter(i, 0.25f));
	renderer->finish_frame();
	for (int i = 0; i < 3; ++i)
	{
		Rgba const c = read(colors, i);
		BOOST_CHECK_MESSAGE(c.r == i && c.g == 0.25f && c.b == 0.5f + i && c.a == 1.0f,
			"layer " << i << ": " << c.r << " " << c.g << " " << c.b << " " << c.a);
		BOOST_CHECK_EQUAL(read(values, i).r, 10.0f * i + 0.25f);
		BOOST_CHECK_CLOSE(read(depths, i).r, 0.5f * (1.0f - 0.25f * i), 1e-3);
	}

	// layer 1 again, the others not
	renderer->begin_frame();
	pass(renderer, layers[1], painter(1, 0.75f));
	renderer->finish_frame();
	for (int i = 0; i < 3; ++i)
	{
		float const shade = i == 1 ? 0.75f : 0.25f;
		Rgba const c = read(colors, i);
		BOOST_CHECK_MESSAGE(c.r == i && c.g == shade, "layer " << i << " after layer 1 is drawn again: " << c.r << " " << c.g);
		BOOST_CHECK_EQUAL(read(values, i).r, 10.0f * i + shade);
		BOOST_CHECK_CLOSE(read(depths, i).r, 0.5f * (1.0f - 0.25f * i), 1e-3);
	}
}

/// The depth of a target of layers that keeps none is one buffer its layers share (a pass draws one layer and throws its depth away), not
/// one of the target's size for each layer: the backends' own depth attachment of each layer is the same.
#ifdef EDGE_USE_VULKAN
BOOST_AUTO_TEST_CASE(a_vulkan_target_of_layers_draws_every_layer_with_one_depth_image)
{
	VulkanFixture fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	for (bool cube : {false, true})
	{
		RenderTargetFormat format;
		format.color_formats = {PixelFormat::R32G32B32A32F};
		format.layers = cube ? 0 : 4;
		format.cube = cube;
		RenderTargetPtr const target = fixture.renderer->create_render_target(SIZE, SIZE, format);
		VkImage const depth = static_cast<VulkanRenderTarget &>(*target->layer(0)).depth_image();
		BOOST_REQUIRE(depth != VK_NULL_HANDLE);
		for (int i = 1; i < target->layers(); ++i)
			BOOST_CHECK_MESSAGE(static_cast<VulkanRenderTarget &>(*target->layer(i)).depth_image() == depth,
				"layer " << i << " of a " << (cube ? "cube" : "target of 4 layers") << " has a depth image of its own");
		// another target's is its own
		RenderTargetPtr const other = fixture.renderer->create_render_target(SIZE, SIZE, format);
		BOOST_CHECK(static_cast<VulkanRenderTarget &>(*other->layer(0)).depth_image() != depth);
	}
}
#endif

#ifdef EDGE_USE_GLES
BOOST_AUTO_TEST_CASE(a_gles_target_of_layers_draws_every_layer_with_one_depth_renderbuffer)
{
	GlesFixture fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	for (bool cube : {false, true})
	{
		RenderTargetFormat format;
		format.color_formats = {PixelFormat::R32G32B32A32F};
		format.layers = cube ? 0 : 4;
		format.cube = cube;
		RenderTargetPtr const target = fixture.renderer->create_render_target(SIZE, SIZE, format);
		GLuint const depth = static_cast<GLESRenderTarget &>(*target->layer(0)).depth_renderbuffer();
		BOOST_REQUIRE(depth != 0);
		for (int i = 1; i < target->layers(); ++i)
			BOOST_CHECK_MESSAGE(static_cast<GLESRenderTarget &>(*target->layer(i)).depth_renderbuffer() == depth,
				"layer " << i << " of a " << (cube ? "cube" : "target of 4 layers") << " has a depth renderbuffer of its own");
		RenderTargetPtr const other = fixture.renderer->create_render_target(SIZE, SIZE, format);
		BOOST_CHECK(static_cast<GLESRenderTarget &>(*other->layer(0)).depth_renderbuffer() != depth);
	}
}
#endif

/// The layers that share a depth do not see each other's: each pass clears it, so a layer drawn after a nearer one is not depth-tested
/// against it, whichever order the layers are drawn in.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_layer_is_not_hidden_by_what_another_layer_drew, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetFormat format;
	format.color_formats = {PixelFormat::R32G32B32A32F, PixelFormat::R32F};
	format.layers = 3;
	RenderTargetPtr const target = renderer->create_render_target(SIZE, SIZE, format);
	CameraPtr const camera = flat_view();
	auto const painter = [&](int i, float z)
	{
		MaterialPtr const m = material(renderer, "test_frame.vert", "test_two_constants.frag");
		m->set_extra_parameter(0, rove::vec<4>(float(i + 1), 0, 0, 1));
		m->set_extra_parameter(1, rove::vec<4>(float(i + 1), 0, 0, 0));
		return quad(renderer, m, camera, 1.0f, z);
	};
	RenderTargetPtr const copy = renderer->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F);
	// the nearest first, then farther ones (depth 0.1, 0.5, 0.9), and the other way round
	for (bool nearest_first : {true, false})
	{
		renderer->begin_frame();
		for (int k = 0; k < 3; ++k)
		{
			int const i = nearest_first ? k : 2 - k;
			pass(renderer, target->layer(i), painter(i, 0.8f - 0.8f * i));
		}
		renderer->finish_frame();
		for (int i = 0; i < 3; ++i)
		{
			MaterialPtr const reader = material(renderer, "test_frame.vert", "test_fetch_array.frag");
			reader->set_diffuse_texture(target->color_texture());
			reader->set_extra_parameter(0, rove::vec<4>(float(i), 0, 0, 0));
			renderer->begin_frame();
			pass(renderer, copy, quad(renderer, reader, camera, 1.0f, 0.0f));
			renderer->finish_frame();
			BOOST_CHECK_MESSAGE(read_image<Rgba>(*copy->color_texture()).at(SIZE / 2, SIZE / 2).r == float(i + 1),
				"layer " << i << (nearest_first ? " (nearest first)" : " (farthest first)") << " reads "
				<< read_image<Rgba>(*copy->color_texture()).at(SIZE / 2, SIZE / 2).r);
		}
	}
}

/// Shadow cascades: a target of no colour and a readable depth of three layers, each a depth of its own, set as a shadow texture and
/// compared layer by layer (sampler2DArrayShadow).
BOOST_AUTO_TEST_CASE_TEMPLATE(a_depth_array_is_compared_layer_by_layer, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetFormat format;
	format.readable_depth = true;
	format.layers = 3;
	RenderTargetPtr const cascades = renderer->create_render_target(SIZE, SIZE, format);
	CameraPtr const camera = flat_view();
	MaterialPtr const depth_only = material(renderer, "test_frame.vert", "test_no_output.frag");
	float const depth[3] = {0.2f, 0.4f, 0.6f};
	std::vector<DrawCallPtr> drawn;
	for (float d : depth)
		drawn.push_back(quad(renderer, depth_only, camera, 1.0f, 1.0f - 2.0f * d));
	RenderTargetPtr const copy = renderer->create_render_target(SIZE, SIZE, PixelFormat::R32G32B32A32F);
	MaterialPtr const compare = material(renderer, "test_frame.vert", "test_shadow_array.frag");
	DrawCallPtr const cover = quad(renderer, compare, camera, 1.0f, 0.0f);
	renderer->set_global_shadow_texture(0, cascades->depth_texture());
	renderer->begin_frame();
	for (int i = 0; i < 3; ++i)
		pass(renderer, cascades->layer(i), drawn[i]);
	renderer->finish_frame();
	for (float reference : {0.1f, 0.3f, 0.5f, 0.7f})
		for (int i = 0; i < 3; ++i)
		{
			compare->set_extra_parameter(0, rove::vec<4>(reference, float(i), float(SIZE), float(SIZE)));
			renderer->begin_frame();
			pass(renderer, copy, cover);
			renderer->finish_frame();
			float const lit = read_image<Rgba>(*copy->color_texture()).at(SIZE / 2, SIZE / 3).r;
			BOOST_CHECK_MESSAGE(lit == (reference <= depth[i] ? 1.0f : 0.0f),
				"layer " << i << " (depth " << depth[i] << ") against " << reference << ": " << lit);
		}
	renderer->set_global_texture(0, TexturePtr());
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(render_target_cube)

/// A cube target is six square layers, its faces; it is refused of another shape, or with layers of its own.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_cube_is_six_square_faces, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetFormat format;
	format.color_formats = {PixelFormat::R16G16B16A16F};
	format.readable_depth = true;
	format.cube = true;
	RenderTargetPtr const cube = renderer->create_render_target(16, 16, format);
	BOOST_CHECK(cube->is_cube());
	BOOST_CHECK_EQUAL(cube->layers(), 6);
	for (TexturePtr const &t : {cube->color_texture(), cube->depth_texture()})
	{
		BOOST_REQUIRE(t);
		BOOST_CHECK(t->is_cube());
		BOOST_CHECK_EQUAL(t->array_layers(), 6);
	}
	for (int face = 0; face < 6; ++face)
	{
		BOOST_CHECK_EQUAL(cube->layer(face)->layer_index(), face);
		BOOST_CHECK(cube->layer(face)->is_cube());
	}
	BOOST_CHECK_THROW(cube->layer(6), std::out_of_range);
	renderer->begin_frame();
	BOOST_CHECK_THROW(renderer->begin_pass(cube), std::invalid_argument);
	renderer->finish_frame();

	BOOST_CHECK_THROW(renderer->create_render_target(16, 8, format), std::invalid_argument);
	format.layers = 2;
	BOOST_CHECK_THROW(renderer->create_render_target(16, 16, format), std::invalid_argument);
	format.layers = 0;
	try
	{
		renderer->create_render_target(1 << 16, 1 << 16, format);
		BOOST_ERROR("a cube of 65536 x 65536 faces was made");
	}
	catch (std::invalid_argument const &e)
	{
		BOOST_CHECK_MESSAGE(mentions(e, "cube"), "the refusal does not name the limit: " << e.what());
	}
}

/// Camera::set_cube_face(eye, i, near, far): a square view of a right angle from eye along the axis of face i (+x, -x, +y, -y, +z, -z),
/// mirrored in y as the faces hold the world (face 2 looks along -y, face 3 along +y).
BOOST_AUTO_TEST_CASE(a_cube_face_camera_looks_through_its_face)
{
	Camera camera;
	rove::vec<3> const eye(1, 2, 3);
	for (int face = 0; face < 6; ++face)
	{
		camera.set_cube_face(eye, face, 0.5f, 20.0f);
		BOOST_CHECK_SMALL(rove::length(camera.eye_point() - eye), 1e-5f);
		// the corners of the face (x, y = +-1 in clip space) are its directions one away along the axis and one across each way
		rove::matrix<4,4> const to_world = camera.inverted_view_matrix() * camera.inverted_projection_matrix();
		rove::vec<4> const centre = to_world * rove::vec<4>(0, 0, 0.5f, 1);
		rove::vec<3> const forward = rove::normalize(rove::vec<3>(centre.x, centre.y, centre.z) / centre.w - eye);
		float const sign = (face % 2 ? -1.0f : 1.0f) * (face / 2 == 1 ? -1.0f : 1.0f);
		BOOST_CHECK_CLOSE(forward[face / 2], sign, 1e-3);
		for (int cx : {-1, 1})
			for (int cy : {-1, 1})
			{
				rove::vec<4> const corner = to_world * rove::vec<4>(float(cx), float(cy), 0.5f, 1);
				rove::vec<3> const d = rove::vec<3>(corner.x, corner.y, corner.z) / corner.w - eye;
				float const along = d[face / 2];
				for (int axis = 0; axis < 3; ++axis)
					BOOST_CHECK_CLOSE(std::abs(d[axis] / along), 1.0f, 1e-2);
			}
	}
	BOOST_CHECK_THROW(camera.set_cube_face(eye, 6, 0.5f, 20.0f), std::out_of_range);
	BOOST_CHECK_THROW(camera.set_cube_face(eye, -1, 0.5f, 20.0f), std::out_of_range);
}

/// Each face drawn by its pass with Camera::cube_face's camera, from a point inside a box, the box's colour its direction from that
/// point (test_world_direction.frag): sampled as a cube in the direction edge_cube_direction() makes of a world direction, it is that
/// direction, all round (a latitude-longitude map of the sphere, test_sample_cube.frag).
BOOST_AUTO_TEST_CASE_TEMPLATE(a_cube_drawn_face_by_face_samples_as_the_world_seen_from_its_centre, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetFormat format;
	format.color_formats = {PixelFormat::R16G16B16A16F};
	format.cube = true;
	RenderTargetPtr const cube = renderer->create_render_target(SIZE, SIZE, format);
	rove::vec<3> const eye(0.25f, -0.5f, 0.125f);
	MaterialPtr const direction = material(renderer, "test_world_position.vert", "test_world_direction.frag");
	direction->set_extra_parameter(0, rove::vec<4>(eye.x, eye.y, eye.z, 0));
	CameraPtr const camera(new Camera());
	int const W = 64, H = 32;
	RenderTargetPtr const map = renderer->create_render_target(W, H, PixelFormat::R32G32B32A32F);
	MaterialPtr const sampler = material(renderer, "test_frame.vert", "test_sample_cube.frag");
	sampler->set_diffuse_texture(cube->color_texture());
	sampler->set_extra_parameter(0, rove::vec<4>(float(W), float(H), 0, 0));
	CameraPtr const flat = flat_view();
	DrawCallPtr const cover = quad(renderer, sampler, flat, 1.0f, 0.0f);

	renderer->begin_frame();
	for (int face = 0; face < 6; ++face)
	{
		camera->set_cube_face(eye, face, 0.1f, 10.0f);
		pass(renderer, cube->layer(face), box(renderer, direction, camera, 2.0f));
	}
	pass(renderer, map, cover);
	renderer->finish_frame();
	Image<Rgba> const seen = read_image<Rgba>(*map->color_texture());
	int wrong = 0;
	float worst = 1;
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			rove::vec<3> const want = map_direction(x, y, W, H);
			Rgba const c = seen.at(x, y);
			rove::vec<3> const got(c.r, c.g, c.b);
			float const agree = rove::dot(got, want) / std::max(1e-6f, rove::length(got));
			worst = std::min(worst, agree);
			if (agree < 0.995f && wrong++ < 5)
				BOOST_ERROR("direction (" << want.x << ", " << want.y << ", " << want.z << ") samples (" << got.x << ", " << got.y << ", "
					<< got.z << ")");
		}
	BOOST_TEST_MESSAGE("worst agreement " << worst);
	BOOST_CHECK_EQUAL(wrong, 0);
}

/// A point light's shadow: a cube of no colour and a readable depth, drawn from the light at its centre (a box about it, its faces at 0.5
/// along each axis), compared with samplerCubeShadow at points in every direction, their depth edge_cube_depth()'s: lit nearer than the
/// box, shadowed past it.
BOOST_AUTO_TEST_CASE_TEMPLATE(a_depth_cube_compares_a_point_with_what_its_face_saw, F, Backends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	RenderTargetFormat format;
	format.readable_depth = true;
	format.cube = true;
	RenderTargetPtr const cube = renderer->create_render_target(SIZE, SIZE, format);
	float const near = 0.1f, far = 10.0f;
	CameraPtr const camera(new Camera());
	MaterialPtr const depth_only = material(renderer, "test_frame.vert", "test_no_output.frag");
	int const W = 64, H = 32;
	RenderTargetPtr const map = renderer->create_render_target(W, H, PixelFormat::R32G32B32A32F);
	MaterialPtr const compare = material(renderer, "test_frame.vert", "test_shadow_cube.frag");
	DrawCallPtr const cover = quad(renderer, compare, flat_view(), 1.0f, 0.0f);
	renderer->set_global_shadow_texture(0, cube->depth_texture());
	renderer->begin_frame();
	for (int face = 0; face < 6; ++face)
	{
		camera->set_cube_face(rove::vec<3>(0, 0, 0), face, near, far);
		pass(renderer, cube->layer(face), box(renderer, depth_only, camera, 0.5f));
	}
	renderer->finish_frame();
	for (float distance : {0.4f, 0.6f})
	{
		compare->set_extra_parameter(0, rove::vec<4>(float(W), float(H), distance, 0));
		compare->set_extra_parameter(1, rove::vec<4>(near, far, 0, 0));
		renderer->begin_frame();
		pass(renderer, map, cover);
		renderer->finish_frame();
		Image<Rgba> const lit = read_image<Rgba>(*map->color_texture());
		int wrong = 0;
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x)
				if (lit.at(x, y).r != (distance < 0.5f ? 1.0f : 0.0f) && wrong++ < 5)
					BOOST_ERROR("at " << distance << " in direction of pixel (" << x << ", " << y << "): " << lit.at(x, y).r);
		BOOST_CHECK_EQUAL(wrong, 0);
	}
	renderer->set_global_texture(0, TexturePtr());
}

BOOST_AUTO_TEST_SUITE_END()
