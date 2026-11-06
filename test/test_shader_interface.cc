// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// shader_interface.h: what every renderer binds and fills, decided once.  (test_frame_resources.cc checks on the GPU that the shaders
// see these values where edge.glsl says.)

#include <boost/test/unit_test.hpp>

#include "material.h"
#include "null_renderer.h"
#include "shader_interface.h"
#include "shader_program.h"
#include "texture.h"
#include "vertex.h"
#include "vertex_traits.h"

using namespace edge;

namespace
{

struct Fixture
{
	RendererPtr renderer = std::make_shared<NullRenderer>(4, 4);
	MaterialPtr material = renderer->create_material();
	TexturePtr texture = renderer->create_texture(4, 4, PixelFormat::A8R8G8B8);
};

}

BOOST_AUTO_TEST_SUITE(shader_interface)

BOOST_AUTO_TEST_CASE(every_element_of_edge_s_vertices_has_its_location_and_nothing_else_has_one)
{
	BOOST_TEST(shader::attribute_location(VertexElement::Usage::POSITION, 0) == 0);
	BOOST_TEST(shader::attribute_location(VertexElement::Usage::TANGENT, 0) == 1);
	BOOST_TEST(shader::attribute_location(VertexElement::Usage::BINORMAL, 0) == 2);
	BOOST_TEST(shader::attribute_location(VertexElement::Usage::NORMAL, 0) == 3);
	BOOST_TEST(shader::attribute_location(VertexElement::Usage::BLEND_INDICES, 1) == 5);
	BOOST_TEST(shader::attribute_location(VertexElement::Usage::BLEND_WEIGHT, 0) == 6);
	BOOST_TEST(shader::attribute_location(VertexElement::Usage::TEXCOORD, 0) == 8);
	BOOST_TEST(shader::attribute_location(VertexElement::Usage::TEXCOORD, 1) == 9);
	BOOST_TEST(shader::attribute_location(VertexElement::Usage::COLOR, 0) == 10);
	BOOST_TEST(shader::attribute_location(VertexElement::Usage::TEXCOORD, 2) == -1);
	BOOST_TEST(shader::attribute_location(VertexElement::Usage::COLOR, 1) == -1);
	BOOST_TEST(shader::attribute_location(VertexElement::Usage::NORMAL, 1) == -1);
	// every vertex type's elements are somewhere
	for (VertexTraits const *traits : {VertexTraits::of<VERTEX_PT>(), VertexTraits::of<VERTEX_PTBNT>(), VertexTraits::of<VERTEX_PTBNTC>(),
		VertexTraits::of<VERTEX_PTBNJWT>()})
		for (VertexElement const &e : traits->elements())
		{
			int const location = shader::attribute_location(e.usage, e.usage_index);
			BOOST_TEST((location >= 0 && location < shader::ATTRIBUTE_LOCATIONS));
		}
}

BOOST_AUTO_TEST_CASE(the_built_in_shaders_follow_the_vertex_and_a_program_names_its_own)
{
	VertexTraits const &plain = *VertexTraits::of<VERTEX_PTBNT>(), &skin = *VertexTraits::of<VERTEX_PTBNJWT>();
	BOOST_TEST(shader::shaders(nullptr, plain).vertex == "vertex.spv");
	BOOST_TEST(shader::shaders(nullptr, plain).fragment == "fragment.spv");
	BOOST_TEST(shader::shaders(nullptr, skin).vertex == "vertex_skin.spv");
	BOOST_TEST(shader::skinned(skin));
	BOOST_TEST(!shader::skinned(plain));
	ShaderProgram const fragment_only("", "mine.frag.spv"), both("mine.vert.spv", "mine.frag.spv");
	BOOST_TEST(shader::shaders(&fragment_only, skin).vertex == "vertex_skin.spv");
	BOOST_TEST(shader::shaders(&fragment_only, skin).fragment == "mine.frag.spv");
	BOOST_TEST(shader::shaders(&both, skin).vertex == "mine.vert.spv");
}

BOOST_FIXTURE_TEST_CASE(a_plain_draw_has_the_material_s_flags_and_an_uber_draw_what_it_draws_too, Fixture)
{
	VertexTraits const &plain = *VertexTraits::of<VERTEX_PTBNT>();
	material->set_normal_map_texture(texture);
	material->set_texture_blending_weights_texture(texture);
	shader::PushConstants pc = shader::push_constants(*material, plain, false, true, rove::vec<3>(1, 2, 3));
	BOOST_TEST(pc.flags == shader::FLAG_NORMAL_MAP);            // a plain draw call leaves blending and skins to its shader
	BOOST_TEST(pc.camera_eye[1] == 2.0f);
	BOOST_TEST(pc.camera_eye[3] == 1.0f);
	pc = shader::push_constants(*material, plain, true, true, rove::vec<3>(1, 2, 3));
	BOOST_TEST(pc.flags == (shader::FLAG_NORMAL_MAP | shader::FLAG_TEXTURE_BLENDING_TEXTURE | shader::FLAG_SKELETAL));
	material->set_texture_blending_weights_texture(TexturePtr());
	BOOST_TEST(shader::push_constants(*material, plain, true, false, rove::vec<3>()).flags == shader::FLAG_NORMAL_MAP);
	// a weights texture or else the vertex's second set of coordinates (VERTEX_PNTB's), which the blending vertex shader reads
	VertexTraits const &blended = *VertexTraits::of<VERTEX_PNTB>();
	BOOST_REQUIRE(blended.has(VertexElement::Usage::TEXCOORD, 1));
	BOOST_TEST(shader::push_constants(*material, blended, true, false, rove::vec<3>()).flags ==
		(shader::FLAG_NORMAL_MAP | shader::FLAG_TEXTURE_BLENDING_VERTEX));
	BOOST_TEST(shader::push_constants(*material, blended, false, false, rove::vec<3>()).flags == shader::FLAG_NORMAL_MAP);
	BOOST_TEST(shader::shaders(nullptr, blended).vertex == "vertex_blend.spv");
}

BOOST_AUTO_TEST_SUITE_END()
