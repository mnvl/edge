// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>
#include <boost/test/unit_test.hpp>
#include <rove/matrix.h>

#include "draw_call.h"
#include "index.h"
#include "mesh.h"
#include "uber_draw_call.h"
#include "camera.h"
#include "material.h"
#include "null_renderer.h"

namespace
{

struct DrawCallFixture
{
	edge::RendererPtr renderer;

	DrawCallFixture()
	{
		renderer = std::make_shared<edge::NullRenderer>(320, 200);
	}

	edge::CameraPtr make_camera()
	{
		edge::CameraPtr camera(new edge::Camera());
		camera->set_perspective_projection(1.0f, 1.0f, 0.1f, 100.0f);
		camera->look_at(
			rove::vec<3>(0, 0, 5),
			rove::vec<3>(0, 0, 0),
			rove::vec<3>(0, 1, 0));
		return camera;
	}
};

/// What the renderer read from each call it was given, read as it was given it
struct Given
{
	edge::DrawCall const *call;
	edge::VertexBufferPtr vertex_buffer;
	edge::IndexBufferPtr index_buffer;
	edge::MaterialPtr material;
	edge::CameraPtr camera;
	rove::matrix<4,4> world_view_projection, world;
	size_t minimal_vertex_index, first_index_offset, triangles_count;
	bool uber, instanced;
	std::vector<rove::matrix<4,4>> joint_matrices;
	bool skinned;
};

class ReadingRenderer: public edge::NullRenderer
{
public:
	ReadingRenderer(): NullRenderer(320, 200) {}
	std::vector<Given> given;

protected:
	void do_draw(edge::DrawCall const &call) override
	{
		std::vector<rove::matrix<4,4>> const *joints = call.joint_matrices();
		given.push_back({&call, call.vertex_buffer(), call.index_buffer(), call.material(), call.camera(),
			call.shader_world_view_projection(), call.shader_world(), call.minimal_vertex_index(),
			call.first_index_offset(), call.triangles_count(), call.is_uber(), call.is_instanced(),
			joints ? *joints : std::vector<rove::matrix<4,4>>(), joints != nullptr});
	}
};

rove::matrix<4,4> moved(float x, float y, float z)
{
	rove::matrix<4,4> m;
	m.identity();
	m.ij[3][0] = x;
	m.ij[3][1] = y;
	m.ij[3][2] = z;
	return m;
}

/// One pass of @p call's execute()
void execute_in_a_pass(edge::RendererPtr const &renderer, edge::DrawCall &call)
{
	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	call.execute();
	renderer->end_pass();
	renderer->finish_frame();
}

}

BOOST_AUTO_TEST_SUITE(draw_call)

BOOST_FIXTURE_TEST_CASE(commit_requires_vertex_buffer, DrawCallFixture)
{
	edge::DrawCallPtr dc = renderer->create_draw_call();
	dc->set_material(renderer->create_material());
	dc->set_camera(make_camera());
	BOOST_CHECK_THROW(dc->commit(), std::logic_error);
}

BOOST_FIXTURE_TEST_CASE(commit_requires_material, DrawCallFixture)
{
	edge::DrawCallPtr dc = renderer->create_draw_call();
	dc->set_vertex_buffer(renderer->create_vertex_buffer(nullptr, 1));
	dc->set_camera(make_camera());
	BOOST_CHECK_THROW(dc->commit(), std::logic_error);
}

BOOST_FIXTURE_TEST_CASE(commit_requires_camera, DrawCallFixture)
{
	edge::DrawCallPtr dc = renderer->create_draw_call();
	dc->set_vertex_buffer(renderer->create_vertex_buffer(nullptr, 1));
	dc->set_material(renderer->create_material());
	BOOST_CHECK_THROW(dc->commit(), std::logic_error);
}

BOOST_FIXTURE_TEST_CASE(commit_succeeds_with_all_set, DrawCallFixture)
{
	edge::DrawCallPtr dc = renderer->create_draw_call();
	dc->set_vertex_buffer(renderer->create_vertex_buffer(nullptr, 1));
	dc->set_material(renderer->create_material());
	dc->set_camera(make_camera());
	BOOST_CHECK_NO_THROW(dc->commit());
}

BOOST_FIXTURE_TEST_CASE(always_visible_is_visible, DrawCallFixture)
{
	edge::DrawCallPtr dc = renderer->create_draw_call();
	dc->set_always_visible(true);
	dc->set_vertex_buffer(renderer->create_vertex_buffer(nullptr, 1));
	dc->set_material(renderer->create_material());
	dc->set_camera(make_camera());
	dc->commit();
	BOOST_CHECK(dc->is_visible());
}

BOOST_FIXTURE_TEST_CASE(not_always_visible_default_bounds_at_origin, DrawCallFixture)
{
	edge::DrawCallPtr dc = renderer->create_draw_call();
	dc->set_always_visible(false);
	dc->set_vertex_buffer(renderer->create_vertex_buffer(nullptr, 1));
	dc->set_material(renderer->create_material());
	dc->set_camera(make_camera());
	dc->commit();
	// Default OBB at origin, camera looking at origin from (0,0,5) — should be visible
	BOOST_CHECK(dc->is_visible());
}

BOOST_FIXTURE_TEST_CASE(an_uber_draw_call_takes_no_more_joints_than_a_draw_has_matrices_for, DrawCallFixture)
{
	// (not the first 256 of them, silently)
	edge::UberDrawCallPtr draw_call = renderer->create_uber_draw_call();
	std::vector<rove::matrix<4,4>> joints(edge::MAX_JOINT_MATRICES);
	draw_call->set_joint_matrix_array(joints);
	joints.resize(edge::MAX_JOINT_MATRICES + 1);
	BOOST_CHECK_THROW(draw_call->set_joint_matrix_array(joints), std::length_error);
}

BOOST_FIXTURE_TEST_CASE(a_draw_call_is_committed_before_it_draws, DrawCallFixture)
{
	// (its matrices are worked out at commit(): before it there are none to draw with)
	edge::DrawCallPtr dc = renderer->create_draw_call();
	dc->set_vertex_buffer(renderer->create_vertex_buffer(nullptr, 1));
	dc->set_material(renderer->create_material());
	dc->set_camera(make_camera());
	dc->set_triangles_count(1);
	renderer->begin_frame();
	renderer->begin_pass(renderer->default_render_target());
	BOOST_CHECK_THROW(dc->execute(), std::logic_error);
	dc->commit();
	BOOST_CHECK_NO_THROW(dc->execute());
	renderer->end_pass();
	// and in a pass: the null renderer refuses a draw outside one, as the others do
	BOOST_CHECK_THROW(dc->execute(), std::logic_error);
	renderer->finish_frame();
}

BOOST_AUTO_TEST_CASE(the_renderer_is_given_the_call_and_reads_what_was_set_on_it)
{
	auto const renderer = std::make_shared<ReadingRenderer>();
	DrawCallFixture fixture;
	edge::CameraPtr const camera = fixture.make_camera();
	edge::VertexBufferPtr const vertices = renderer->create_vertex_buffer(nullptr, 1);
	edge::IndexBufferPtr const indices = renderer->create_index_buffer(edge::IndexFormat::U16, 2 * 30);
	edge::MaterialPtr const material = renderer->create_material();
	rove::matrix<4,4> const world = moved(0.5f, -0.25f, 1);

	edge::DrawCallPtr const dc = renderer->create_draw_call();
	dc->set_vertex_buffer(vertices);
	dc->set_index_buffer(indices);
	dc->set_material(material);
	dc->set_camera(camera);
	dc->set_world_matrix(world);
	dc->set_minimal_vertex_index(3);
	dc->set_first_index_offset(6);
	dc->set_triangles_count(7);
	dc->set_always_visible(true);
	dc->commit();
	execute_in_a_pass(renderer, *dc);

	BOOST_REQUIRE_EQUAL(renderer->given.size(), 1u);
	Given const &g = renderer->given[0];
	BOOST_CHECK(g.call == dc.get());
	BOOST_CHECK(g.vertex_buffer == vertices);
	BOOST_CHECK(g.index_buffer == indices);
	BOOST_CHECK(g.material == material);
	BOOST_CHECK(g.camera == camera);
	BOOST_CHECK_EQUAL(g.minimal_vertex_index, 3u);
	BOOST_CHECK_EQUAL(g.first_index_offset, 6u);
	BOOST_CHECK_EQUAL(g.triangles_count, 7u);
	BOOST_CHECK(rove::equal(g.world, world, 1e-6f));
	BOOST_CHECK(rove::equal(g.world_view_projection, camera->projection_matrix() * camera->view_matrix() * world, 1e-6f));
	BOOST_CHECK(!g.uber);
	BOOST_CHECK(!g.instanced);
	BOOST_CHECK(!g.skinned);
}

BOOST_AUTO_TEST_CASE(an_uber_call_is_drawn_as_one_and_skinned_only_with_joints)
{
	auto const renderer = std::make_shared<ReadingRenderer>();
	DrawCallFixture fixture;
	edge::UberDrawCallPtr const dc = renderer->create_uber_draw_call();
	dc->set_vertex_buffer(renderer->create_vertex_buffer(nullptr, 1));
	dc->set_material(renderer->create_material());
	dc->set_camera(fixture.make_camera());
	dc->set_triangles_count(1);
	dc->set_always_visible(true);
	dc->commit();
	execute_in_a_pass(renderer, *dc);

	std::vector<rove::matrix<4,4>> const joints{moved(1, 2, 3), moved(-4, 5, -6)};
	dc->set_joint_matrix_array(joints);
	execute_in_a_pass(renderer, *dc);

	BOOST_REQUIRE_EQUAL(renderer->given.size(), 2u);
	BOOST_CHECK(renderer->given[0].uber);
	BOOST_CHECK(!renderer->given[0].skinned);         // an uber call without a skin
	BOOST_CHECK(renderer->given[1].uber);
	BOOST_REQUIRE(renderer->given[1].skinned);
	BOOST_REQUIRE_EQUAL(renderer->given[1].joint_matrices.size(), 2u);
	BOOST_CHECK(rove::equal(renderer->given[1].joint_matrices[0], joints[0], 0.0f));
	BOOST_CHECK(rove::equal(renderer->given[1].joint_matrices[1], joints[1], 0.0f));
}

BOOST_AUTO_TEST_SUITE_END()
