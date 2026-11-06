// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cmath>
#include <cstring>
#include <boost/test/unit_test.hpp>

#include "vertex.h"
#include "vertex_traits.h"

BOOST_AUTO_TEST_SUITE(vertex)

BOOST_AUTO_TEST_CASE(format_flags_pt)
{
	BOOST_CHECK(edge::VERTEX_PT::FORMAT & edge::GenericVertex::FORMAT_POSITION);
	BOOST_CHECK(edge::VERTEX_PT::FORMAT & edge::GenericVertex::FORMAT_TEXCOORDS);
	BOOST_CHECK(!(edge::VERTEX_PT::FORMAT & edge::GenericVertex::FORMAT_NORMAL));
	BOOST_CHECK(!(edge::VERTEX_PT::FORMAT & edge::GenericVertex::FORMAT_TANGENT));
}

BOOST_AUTO_TEST_CASE(format_flags_pnt)
{
	BOOST_CHECK(edge::VERTEX_PNT::FORMAT & edge::GenericVertex::FORMAT_POSITION);
	BOOST_CHECK(edge::VERTEX_PNT::FORMAT & edge::GenericVertex::FORMAT_NORMAL);
	BOOST_CHECK(edge::VERTEX_PNT::FORMAT & edge::GenericVertex::FORMAT_TEXCOORDS);
	BOOST_CHECK(!(edge::VERTEX_PNT::FORMAT & edge::GenericVertex::FORMAT_TANGENT));
	BOOST_CHECK(!(edge::VERTEX_PNT::FORMAT & edge::GenericVertex::FORMAT_JOINT_INDICES));
}

BOOST_AUTO_TEST_CASE(format_flags_ptbnt)
{
	BOOST_CHECK(edge::VERTEX_PTBNT::FORMAT & edge::GenericVertex::FORMAT_POSITION);
	BOOST_CHECK(edge::VERTEX_PTBNT::FORMAT & edge::GenericVertex::FORMAT_TANGENT);
	BOOST_CHECK(edge::VERTEX_PTBNT::FORMAT & edge::GenericVertex::FORMAT_BINORMAL);
	BOOST_CHECK(edge::VERTEX_PTBNT::FORMAT & edge::GenericVertex::FORMAT_NORMAL);
	BOOST_CHECK(edge::VERTEX_PTBNT::FORMAT & edge::GenericVertex::FORMAT_TEXCOORDS);
	BOOST_CHECK(!(edge::VERTEX_PTBNT::FORMAT & edge::GenericVertex::FORMAT_JOINT_INDICES));
}

BOOST_AUTO_TEST_CASE(format_flags_pnjwt)
{
	BOOST_CHECK(edge::VERTEX_PNJWT::FORMAT & edge::GenericVertex::FORMAT_POSITION);
	BOOST_CHECK(edge::VERTEX_PNJWT::FORMAT & edge::GenericVertex::FORMAT_NORMAL);
	BOOST_CHECK(edge::VERTEX_PNJWT::FORMAT & edge::GenericVertex::FORMAT_JOINT_INDICES);
	BOOST_CHECK(edge::VERTEX_PNJWT::FORMAT & edge::GenericVertex::FORMAT_JOINT_WEIGHTS);
	BOOST_CHECK(edge::VERTEX_PNJWT::FORMAT & edge::GenericVertex::FORMAT_TEXCOORDS);
	BOOST_CHECK(!(edge::VERTEX_PNJWT::FORMAT & edge::GenericVertex::FORMAT_TANGENT));
}

BOOST_AUTO_TEST_CASE(format_flags_ptbnjwt)
{
	BOOST_CHECK(edge::VERTEX_PTBNJWT::FORMAT & edge::GenericVertex::FORMAT_POSITION);
	BOOST_CHECK(edge::VERTEX_PTBNJWT::FORMAT & edge::GenericVertex::FORMAT_TANGENT);
	BOOST_CHECK(edge::VERTEX_PTBNJWT::FORMAT & edge::GenericVertex::FORMAT_BINORMAL);
	BOOST_CHECK(edge::VERTEX_PTBNJWT::FORMAT & edge::GenericVertex::FORMAT_NORMAL);
	BOOST_CHECK(edge::VERTEX_PTBNJWT::FORMAT & edge::GenericVertex::FORMAT_JOINT_INDICES);
	BOOST_CHECK(edge::VERTEX_PTBNJWT::FORMAT & edge::GenericVertex::FORMAT_JOINT_WEIGHTS);
	BOOST_CHECK(edge::VERTEX_PTBNJWT::FORMAT & edge::GenericVertex::FORMAT_TEXCOORDS);
}

BOOST_AUTO_TEST_CASE(generic_to_pnt_assignment)
{
	edge::GenericVertex gv;
	std::memset(&gv, 0, sizeof(gv));
	gv.position.set(1.0f, 2.0f, 3.0f);
	gv.normal.set(0.0f, 1.0f, 0.0f);
	gv.texcoords.set(0.5f, 0.75f);

	edge::VERTEX_PNT v;
	v = gv;

	BOOST_CHECK_SMALL(std::abs(v.position.x - 1.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(v.position.y - 2.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(v.position.z - 3.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(v.normal.y - 1.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(v.texcoords.x - 0.5f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(v.texcoords.y - 0.75f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(generic_to_ptbnt_assignment)
{
	edge::GenericVertex gv;
	std::memset(&gv, 0, sizeof(gv));
	gv.position.set(1.0f, 0.0f, 0.0f);
	gv.tangent.set(0.0f, 1.0f, 0.0f);
	gv.binormal.set(0.0f, 0.0f, 1.0f);
	gv.normal.set(1.0f, 0.0f, 0.0f);
	gv.texcoords.set(0.25f, 0.5f);

	edge::VERTEX_PTBNT v;
	v = gv;

	BOOST_CHECK_SMALL(std::abs(v.position.x - 1.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(v.tangent.y - 1.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(v.binormal.z - 1.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(v.normal.x - 1.0f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(generic_to_pnjwt_assignment)
{
	edge::GenericVertex gv;
	std::memset(&gv, 0, sizeof(gv));
	gv.position.set(5.0f, 6.0f, 7.0f);
	gv.normal.set(0.0f, 0.0f, 1.0f);
	gv.joints.i[0] = 300;
	gv.weights.i[0] = 1.0f;
	gv.texcoords.set(0.1f, 0.2f);

	edge::VERTEX_PNJWT v;
	v = gv;

	BOOST_CHECK_SMALL(std::abs(v.position.x - 5.0f), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(v.normal.z - 1.0f), 1e-6f);
	// Joint index truncated to uint8_t: 300 & 0xFF = 44
	BOOST_CHECK_EQUAL(static_cast<int>(v.joints.i[0]), 300 & 0xFF);
	BOOST_CHECK_SMALL(std::abs(v.weights.i[0] - 1.0f), 1e-6f);
}

BOOST_AUTO_TEST_CASE(generic_vertex_assign_partial_format)
{
	edge::GenericVertex gv;
	std::memset(&gv, 0, sizeof(gv));
	gv.position.set(10.0f, 20.0f, 30.0f);
	gv.normal.set(0.0f, 1.0f, 0.0f);

	edge::VERTEX_PNT v;
	std::memset(&v, 0, sizeof(v));
	// Only assign position component
	v.assign(gv, edge::GenericVertex::FORMAT_POSITION);

	BOOST_CHECK_SMALL(std::abs(v.position.x - 10.0f), 1e-6f);
	// Normal should remain zero since we only assigned position
	BOOST_CHECK_SMALL(std::abs(v.normal.x), 1e-6f);
	BOOST_CHECK_SMALL(std::abs(v.normal.y), 1e-6f);
}

BOOST_AUTO_TEST_CASE(format_flags_pntb)
{
	BOOST_CHECK(edge::VERTEX_PNTB::FORMAT & edge::GenericVertex::FORMAT_POSITION);
	BOOST_CHECK(edge::VERTEX_PNTB::FORMAT & edge::GenericVertex::FORMAT_NORMAL);
	BOOST_CHECK(edge::VERTEX_PNTB::FORMAT & edge::GenericVertex::FORMAT_TEXCOORDS);
	BOOST_CHECK(edge::VERTEX_PNTB::FORMAT & edge::GenericVertex::FORMAT_TEXTURE_BLEND_WEIGHTS);
	BOOST_CHECK(!(edge::VERTEX_PNTB::FORMAT & edge::GenericVertex::FORMAT_TANGENT));
}

BOOST_AUTO_TEST_CASE(format_flags_ptbntc_are_ptbnt_and_a_color)
{
	BOOST_CHECK_EQUAL(edge::VERTEX_PTBNTC::FORMAT, edge::VERTEX_PTBNT::FORMAT | edge::GenericVertex::FORMAT_COLOR);
	BOOST_CHECK(!(edge::VERTEX_PTBNT::FORMAT & edge::GenericVertex::FORMAT_COLOR));
	BOOST_CHECK(!(edge::VERTEX_PNJWT::FORMAT & edge::GenericVertex::FORMAT_COLOR));
	BOOST_CHECK(!(edge::VERTEX_PTBNJWT::FORMAT & edge::GenericVertex::FORMAT_COLOR));
	BOOST_CHECK(!(edge::VERTEX_PNTB::FORMAT & edge::GenericVertex::FORMAT_COLOR));
}

BOOST_AUTO_TEST_CASE(generic_to_ptbntc_assignment)
{
	edge::GenericVertex gv;
	std::memset(&gv, 0, sizeof(gv));
	gv.position.set(1.0f, 2.0f, 3.0f);
	gv.texcoords.set(0.25f, 0.5f);
	gv.color.set(0.1f, 0.2f, 0.3f, 4.0f);

	edge::VERTEX_PTBNTC v;
	v = gv;
	BOOST_CHECK_EQUAL(v.position.z, 3.0f);
	BOOST_CHECK_EQUAL(v.texcoords.y, 0.5f);
	BOOST_CHECK_EQUAL(v.color.i[0], 0.1f);
	BOOST_CHECK_EQUAL(v.color.i[1], 0.2f);
	BOOST_CHECK_EQUAL(v.color.i[2], 0.3f);
	BOOST_CHECK_EQUAL(v.color.i[3], 4.0f);
}

BOOST_AUTO_TEST_CASE(a_format_without_a_color_leaves_the_color_of_a_ptbntc_alone)
{
	edge::GenericVertex gv;
	std::memset(&gv, 0, sizeof(gv));
	gv.position.set(1.0f, 2.0f, 3.0f);
	gv.color.set(9.0f, 9.0f, 9.0f, 9.0f);

	edge::VERTEX_PTBNTC v;
	std::memset(&v, 0, sizeof(v));
	v.color.set(0.5f, 0.5f, 0.5f, 0.5f);
	v.assign(gv, edge::GenericVertex::FORMAT_POSITION);
	BOOST_CHECK_EQUAL(v.position.x, 1.0f);
	for (float c : v.color.i)
		BOOST_CHECK_EQUAL(c, 0.5f);
	v.assign(gv, edge::GenericVertex::FORMAT_POSITION | edge::GenericVertex::FORMAT_COLOR);
	for (float c : v.color.i)
		BOOST_CHECK_EQUAL(c, 9.0f);
}

BOOST_AUTO_TEST_CASE(the_components_string_names_the_color)
{
	BOOST_CHECK_EQUAL(edge::format_components_string(edge::GenericVertex::FORMAT_POSITION | edge::GenericVertex::FORMAT_COLOR), "position|color");
}

/// What the pipeline reads: one element for each component of the vertex, the color a FLOAT4 (type 3) of usage COLOR (10) at its offset.
BOOST_AUTO_TEST_CASE(the_ptbntc_traits_describe_the_color_as_a_float4_at_its_offset)
{
	edge::VertexTraits const *traits = edge::VertexTraits::of<edge::VERTEX_PTBNTC>();
	BOOST_CHECK_EQUAL(traits->format(), edge::VERTEX_PTBNTC::FORMAT);
	BOOST_CHECK_EQUAL(traits->size(), sizeof(edge::VERTEX_PTBNTC));
	BOOST_CHECK_EQUAL(sizeof(edge::VERTEX_PTBNTC), sizeof(edge::VERTEX_PTBNT) + sizeof(rove::vec<4>));
	int elements = 0, colors = 0;
	for (edge::VertexElement const &e : traits->elements())
	{
		++elements;
		if (e.usage == edge::VertexElement::Usage::COLOR)
		{
			++colors;
			BOOST_CHECK(e.type == edge::VertexElement::Type::FLOAT4);
			BOOST_CHECK_EQUAL(static_cast<int>(e.usage_index), 0);
			BOOST_CHECK_EQUAL(static_cast<size_t>(e.offset), offsetof(edge::VERTEX_PTBNTC, color));
		}
	}
	BOOST_CHECK_EQUAL(elements, 6);
	BOOST_CHECK_EQUAL(colors, 1);
}

BOOST_AUTO_TEST_SUITE_END()

// what the meshes, models and vertex arrays take is a vertex_layout, and a skinned one for the animated ones
static_assert(edge::vertex_layout<edge::VERTEX_P> && edge::vertex_layout<edge::VERTEX_PNTB> && edge::vertex_layout<edge::VERTEX_PTBNTC>);
static_assert(edge::skinned_vertex_layout<edge::VERTEX_PNJWT> && edge::skinned_vertex_layout<edge::VERTEX_PTBNJWTC>);
static_assert(!edge::skinned_vertex_layout<edge::VERTEX_PTBNT>);
static_assert(!edge::vertex_layout<edge::GenericVertex> && !edge::vertex_layout<rove::vec<3>>);
