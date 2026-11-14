// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// The previous frame's matrices of a draw, for motion vectors (TAA, motion blur): what a draw was placed by the frame before -- its
// world, its instances, its joints -- seen through what its camera was the frame before.  A draw seen the first time was where it is;
// a camera seen the first time, or cut, was where it is.  What a shader reads (dc.previous_*, edge_motion.glsl) is checked by the
// pixels a shader writing the motion leaves in a float target, on every backend that draws.

#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <rove/matrix.h>
#include <rove/obb.h>

#include "animated_model.h"
#include "camera.h"
#include "draw_call.h"
#include "image.h"
#include "index.h"
#include "joint.h"
#include "material.h"
#include "null_renderer.h"
#include "pixel.h"
#include "renderer.h"
#include "scheduler.h"
#include "static_model.h"
#include "test_backends.h"
#include "test_fixture.h"
#include "texture.h"
#include "uber_draw_call.h"
#include "vertex.h"

using namespace edge;

namespace
{

using Matrix = rove::matrix<4, 4>;
using Matrices = std::vector<Matrix>;

Matrix moved(float x, float y, float z = 0)
{
	Matrix m;
	m.translation(x, y, z);
	return m;
}

Matrix turned(float angle)
{
	Matrix m;
	m.rotation(rove::vec<3>(0, 0, 1), angle);
	return m;
}

std::string describe(Matrix const &m)
{
	std::ostringstream out;
	out << "[";
	for (int c = 0; c < 4; ++c)
		out << (c ? " | " : "") << m.ij[c][0] << " " << m.ij[c][1] << " " << m.ij[c][2] << " " << m.ij[c][3];
	return out.str() + "]";
}

bool same(Matrix const &a, Matrix const &b)
{
	for (int c = 0; c < 4; ++c)
		for (int r = 0; r < 4; ++r)
			if (std::abs(a.ij[c][r] - b.ij[c][r]) > 1e-5f * (1 + std::abs(b.ij[c][r])))
				return false;
	return true;
}

bool same(Matrices const &a, Matrices const &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (!same(a[i], b[i]))
			return false;
	return true;
}

#define CHECK_SAME(a, b) BOOST_CHECK_MESSAGE(same(a, b), #a " is " << describe(a) << ", want " << describe(b))
#define CHECK_SAME_LIST(a, b) BOOST_CHECK_MESSAGE(same(a, b), #a ": " << (a).size() << " matrices, not the " << (b).size() << " wanted")

/// An orthographic camera over [x - 1, x + 1] x [y - 1, y + 1] of the plane z = 0, from +z: +x right, +y up.
CameraPtr camera_at(float x, float y)
{
	CameraPtr camera = std::make_shared<Camera>();
	camera->set_orthographic_projection(2.0f, 2.0f, 0.0f, 2.0f);
	camera->look_at(rove::vec<3>(x, y, 1), rove::vec<3>(x, y, 0), rove::vec<3>(0, 1, 0));
	return camera;
}

Matrix view_projection(CameraPtr const &camera)
{
	return camera->projection_matrix() * camera->view_matrix();
}

/// A square of half-width `extent` in the plane z = 0, centred on the origin, in a static vertex (PTBNT) and a skinned one (PNJWT): the
/// skinned one's every vertex half of joint 0, half of joint 1.
struct Square
{
	VertexBufferPtr vertices, skinned_vertices;
	IndexBufferPtr indices;

	Square(RendererPtr const &renderer, float extent)
	{
		rove::vec<3> const corners[4] = {{-extent, -extent, 0}, {extent, -extent, 0}, {extent, extent, 0}, {-extent, extent, 0}};
		vertices = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PTBNT>(), sizeof(VERTEX_PTBNT) * 4);
		auto vertices_mapping = vertices->write(Contents::DISCARD);
		VERTEX_PTBNT *v = vertices_mapping.as<VERTEX_PTBNT>().data();
		for (int k = 0; k < 4; ++k)
		{
			v[k].position = corners[k];
			v[k].tangent = rove::vec<3>(1, 0, 0);
			v[k].binormal = rove::vec<3>(0, 1, 0);
			v[k].normal = rove::vec<3>(0, 0, 1);
			v[k].texcoords = rove::vec<2>(0, 0);
		}
		vertices_mapping.unmap();
		skinned_vertices = renderer->create_vertex_buffer(VertexTraits::of<VERTEX_PNJWT>(), sizeof(VERTEX_PNJWT) * 4);
		auto skinned_vertices_mapping = skinned_vertices->write(Contents::DISCARD);
		VERTEX_PNJWT *s = skinned_vertices_mapping.as<VERTEX_PNJWT>().data();
		for (int k = 0; k < 4; ++k)
		{
			s[k] = VERTEX_PNJWT();
			s[k].position = corners[k];
			s[k].normal = rove::vec<3>(0, 0, 1);
			for (int j = 0; j < 8; ++j)
			{
				s[k].joints[j] = j == 1 ? 1 : 0;
				s[k].weights[j] = j < 2 ? 0.5f : 0.0f;
			}
			s[k].texcoords = rove::vec<2>(0, 0);
		}
		skinned_vertices_mapping.unmap();
		indices = renderer->create_index_buffer(IndexFormat::U16, sizeof(INDEX_U16) * 6);
		auto indices_mapping = indices->write(Contents::DISCARD);
		INDEX_U16 *i = indices_mapping.as<INDEX_U16>().data();
		INDEX_U16 const order[6] = {0, 1, 2, 0, 2, 3};
		std::copy(order, order + 6, i);
		indices_mapping.unmap();
	}

	/// A call of the square, its bounds the square's.
	template <class Call>
	void fill(Call &call, MaterialPtr const &material, CameraPtr const &camera, bool skinned = false) const
	{
		call.set_vertex_buffer(skinned ? skinned_vertices : vertices);
		call.set_index_buffer(indices);
		call.set_material(material);
		call.set_triangles_count(2);
		call.set_obb(rove::obb<3>(rove::aabb<3>(rove::vec<3>(-0.25f, -0.25f, -0.01f), rove::vec<3>(0.25f, 0.25f, 0.01f))));
		call.set_camera(camera);
	}
};

/// One frame of @p renderer, @p before done first (commits, say), then a pass executing each of @p calls into the default target.
template <class Before>
void frame(RendererPtr const &renderer, std::vector<DrawCallPtr> const &calls, Before const &before)
{
	renderer->begin_frame();
	before();
	renderer->begin_pass(renderer->default_render_target());
	for (DrawCallPtr const &call : calls)
		call->execute();
	renderer->end_pass();
	renderer->finish_frame();
}

/// What a renderer was given for each draw, read as it was given: the matrices of this frame and of the frame before.
struct Seen
{
	Matrix world_view_projection, world, previous_world_view_projection, previous_world;
	Matrices visible, previous_visible;
	Matrices joints, previous_joints;
	bool skinned;
	void const *joints_address = nullptr, *previous_joints_address = nullptr;     // the lists themselves
};

class ReadingRenderer: public NullRenderer
{
public:
	ReadingRenderer(): NullRenderer(64, 64) {}
	std::vector<Seen> seen;

protected:
	void do_draw(DrawCall const &call) override
	{
		Seen s;
		s.world_view_projection = call.shader_world_view_projection();
		s.world = call.shader_world();
		s.previous_world_view_projection = call.shader_previous_world_view_projection();
		s.previous_world = call.shader_previous_world();
		s.visible = call.visible_instances();
		s.previous_visible = call.previous_visible_instances();
		Matrices const *joints = call.joint_matrices(), *previous = call.previous_joint_matrices();
		s.skinned = joints != nullptr;
		BOOST_CHECK_MESSAGE((joints == nullptr) == (previous == nullptr), "previous joints exactly where there are joints");
		if (joints)
			s.joints = *joints;
		if (previous)
			s.previous_joints = *previous;
		s.joints_address = joints;
		s.previous_joints_address = previous;
		seen.push_back(s);
	}
};

struct NullScene
{
	RendererPtr renderer = std::make_shared<NullRenderer>(64, 64);
	MaterialPtr material = renderer->create_material();
	Square square{renderer, 0.25f};

	DrawCallPtr call(CameraPtr const &camera, Matrix const &world) const
	{
		DrawCallPtr dc = renderer->create_draw_call();
		square.fill(*dc, material, camera);
		dc->set_world_matrix(world);
		return dc;
	}
};

std::string asset(std::string const &name)
{
	return TestFixture::graphic_sources_path() + "test/data/" + name;
}

}

BOOST_AUTO_TEST_SUITE(motion)

// ---- frames

BOOST_AUTO_TEST_CASE_TEMPLATE(the_frame_index_counts_frames_ended_and_a_commit_between_frames_is_of_the_next, F, AllBackends)
{
	F fixture;
	auto const &renderer = fixture.renderer;
	BOOST_REQUIRE_MESSAGE(renderer, "renderer unavailable: " << fixture.error);
	BOOST_CHECK_EQUAL(renderer->frame_index(), 0u);
	renderer->begin_frame();
	BOOST_CHECK_EQUAL(renderer->frame_index(), 0u);
	BOOST_CHECK_THROW(renderer->begin_frame(), std::logic_error);       // refused: no frame more
	renderer->begin_pass(renderer->default_render_target());
	BOOST_CHECK_THROW(renderer->finish_frame(), std::logic_error);      // refused inside a pass: the frame goes on
	renderer->end_pass();
	BOOST_CHECK_EQUAL(renderer->frame_index(), 0u);
	renderer->finish_frame();
	BOOST_CHECK_EQUAL(renderer->frame_index(), 1u);                 // between frames: the next one's
	renderer->begin_frame();
	BOOST_CHECK_EQUAL(renderer->frame_index(), 1u);
	renderer->submit_frame();
	BOOST_CHECK_EQUAL(renderer->frame_index(), 2u);
	BOOST_CHECK_THROW(renderer->submit_frame(), std::logic_error);
	BOOST_CHECK_EQUAL(renderer->frame_index(), 2u);
}

// ---- a call's own previous frame

BOOST_FIXTURE_TEST_CASE(a_call_seen_the_first_time_through_a_camera_seen_the_first_time_has_its_own_matrices_as_the_previous_ones,
	NullScene)
{
	CameraPtr const camera = camera_at(0.5f, -0.25f);
	Matrix const world = moved(0.25f, 0.125f) * turned(0.5f);
	DrawCallPtr const dc = call(camera, world);
	dc->commit();
	CHECK_SAME(dc->shader_previous_world_view_projection(), view_projection(camera) * world);
	CHECK_SAME(dc->shader_previous_world(), world);
	CHECK_SAME(dc->shader_world_view_projection(), view_projection(camera) * world);
	BOOST_CHECK(dc->previous_visible_instances().empty());
	BOOST_CHECK(dc->previous_joint_matrices() == nullptr);
}

BOOST_FIXTURE_TEST_CASE(the_previous_matrices_are_those_of_the_last_commit_of_the_frame_before, NullScene)
{
	CameraPtr const camera = camera_at(0, 0);
	Matrix const w0 = moved(0.25f, 0), w1 = moved(0.5f, -0.25f) * turned(0.25f), w2 = moved(-0.5f, 0.5f);
	DrawCallPtr const dc = call(camera, w0);
	Matrix const vp0 = view_projection(camera);
	frame(renderer, {dc}, [&] { dc->commit(); dc->commit(); });    // committed twice, at one place: one thing
	BOOST_CHECK_EQUAL(renderer->frame_index(), 1u);

	// committed between frames, so of the next one: the frame before is frame 0
	camera->look_at(rove::vec<3>(0.125f, 0.25f, 1), rove::vec<3>(0.125f, 0.25f, 0), rove::vec<3>(0, 1, 0));
	Matrix const vp1 = view_projection(camera);
	dc->set_world_matrix(w1);
	dc->commit();
	frame(renderer, {dc}, [] {});
	CHECK_SAME(dc->shader_previous_world_view_projection(), vp0 * w0);
	CHECK_SAME(dc->shader_previous_world(), w0);
	CHECK_SAME(dc->shader_world_view_projection(), vp1 * w1);
	CHECK_SAME(dc->shader_world(), w1);

	// the frame after: frame 1's, and still frame 1's when committed again in that frame (a second pass)
	dc->set_world_matrix(w2);
	renderer->begin_frame();
	for (int pass = 0; pass < 2; ++pass)
	{
		dc->commit();
		CHECK_SAME(dc->shader_previous_world_view_projection(), vp1 * w1);
		CHECK_SAME(dc->shader_previous_world(), w1);
	}
	renderer->finish_frame();
}

BOOST_FIXTURE_TEST_CASE(a_call_that_sat_out_its_cameras_frame_before_has_no_previous_placement, NullScene)
{
	CameraPtr const camera = camera_at(0, 0);
	DrawCallPtr const dc = call(camera, moved(0.25f, 0)), other = call(camera, moved(0, 0));
	frame(renderer, {dc}, [&] { dc->commit(); });
	frame(renderer, {other}, [&] { other->commit(); });     // the camera drew, the call did not (it was hidden, say)
	Matrix const w2 = moved(-0.25f, 0.5f);
	dc->set_world_matrix(w2);
	frame(renderer, {dc}, [&] { dc->commit(); });
	CHECK_SAME(dc->shader_previous_world(), w2);
	CHECK_SAME(dc->shader_previous_world_view_projection(), view_projection(camera) * w2);
}

BOOST_FIXTURE_TEST_CASE(the_frame_before_is_the_last_one_the_camera_drew_in, NullScene)
{
	// two frames for each one shown: the scene through its camera, then the screen through another (a frame of the screen alone)
	CameraPtr const scene = camera_at(0, 0), screen = camera_at(5, 5);
	Matrix const w0 = moved(0.25f, 0), w2 = moved(0.5f, 0.25f);
	DrawCallPtr const dc = call(scene, w0), ui = call(screen, Matrix());
	Matrix const vp0 = view_projection(scene);
	frame(renderer, {dc}, [&] { dc->commit(); });
	frame(renderer, {ui}, [&] { ui->commit(); });
	scene->look_at(rove::vec<3>(0.125f, 0, 1), rove::vec<3>(0.125f, 0, 0), rove::vec<3>(0, 1, 0));
	dc->set_world_matrix(w2);
	frame(renderer, {dc}, [&] { dc->commit(); });
	CHECK_SAME(dc->shader_previous_world(), w0);
	CHECK_SAME(dc->shader_previous_world_view_projection(), vp0 * w0);
}

BOOST_FIXTURE_TEST_CASE(the_previous_view_projection_is_the_cameras_own_however_many_cameras_see_the_call, NullScene)
{
	// a call drawn through two cameras each frame (Scheduler::change_draw_calls_camera): each camera's frame before, not the other's
	CameraPtr const a = camera_at(0, 0), b = camera_at(0.5f, 0.5f);
	Matrix const world = moved(0.25f, 0);
	DrawCallPtr const dc = call(a, world);
	Matrix const a0 = view_projection(a), b0 = view_projection(b);
	frame(renderer, {}, [&] {
		dc->set_camera(a);
		dc->commit();
		dc->set_camera(b);
		dc->commit();
	});
	a->look_at(rove::vec<3>(-0.25f, 0, 1), rove::vec<3>(-0.25f, 0, 0), rove::vec<3>(0, 1, 0));
	b->look_at(rove::vec<3>(0.5f, 0.75f, 1), rove::vec<3>(0.5f, 0.75f, 0), rove::vec<3>(0, 1, 0));
	renderer->begin_frame();
	dc->set_camera(a);
	dc->commit();
	CHECK_SAME(dc->shader_previous_world_view_projection(), a0 * world);
	CHECK_SAME(dc->shader_world_view_projection(), view_projection(a) * world);
	dc->set_camera(b);
	dc->commit();
	CHECK_SAME(dc->shader_previous_world_view_projection(), b0 * world);
	CHECK_SAME(dc->shader_world_view_projection(), view_projection(b) * world);
	renderer->finish_frame();

	// a camera another call saw the frame before has that frame's view-projection for a call seen the first time
	Matrix const b1 = view_projection(b);
	b->look_at(rove::vec<3>(0, 0, 1), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	DrawCallPtr const fresh = call(b, moved(-0.5f, 0));
	fresh->commit();
	CHECK_SAME(fresh->shader_previous_world_view_projection(), b1 * moved(-0.5f, 0));
	CHECK_SAME(fresh->shader_previous_world(), moved(-0.5f, 0));
}

BOOST_FIXTURE_TEST_CASE(calls_committed_on_several_threads_each_keep_their_cameras_frame_before, NullScene)
{
	// Draw calls made and committed on several threads at once, each thread through cameras of its own (an arena building its
	// slots' views in parallel): every camera's frame before is its own, as on one thread.
	constexpr int THREADS = 8, CAMERAS = 16, FRAMES = 4, CALLS = 64;
	std::vector<CameraPtr> cameras;
	for (int c = 0; c < THREADS * CAMERAS; ++c)
		cameras.push_back(camera_at(0.01f * c, -0.01f * c));
	std::vector<Matrix> before(cameras.size());
	for (int f = 0; f < FRAMES; ++f)
	{
		for (std::size_t c = 0; c < cameras.size(); ++c)
		{
			float const x = 0.01f * static_cast<float>(c) + 0.1f * f;
			cameras[c]->look_at(rove::vec<3>(x, 0, 1), rove::vec<3>(x, 0, 0), rove::vec<3>(0, 1, 0));
		}
		renderer->begin_frame();
		std::vector<std::vector<DrawCallPtr>> made(THREADS);
		std::vector<std::thread> threads;
		for (int t = 0; t < THREADS; ++t)
			threads.emplace_back([&, t] {
				for (int c = 0; c < CAMERAS; ++c)
					for (int k = 0; k < CALLS; ++k)
					{
						DrawCallPtr dc = call(cameras[t * CAMERAS + c], moved(0.001f * k, 0));
						dc->commit();
						made[t].push_back(std::move(dc));
					}
			});
		for (std::thread &thread : threads)
			thread.join();
		renderer->finish_frame();
		for (int t = 0; t < THREADS; ++t)
			for (int c = 0; c < CAMERAS; ++c)
			{
				std::size_t const camera = static_cast<std::size_t>(t * CAMERAS + c);
				DrawCallPtr const &dc = made[t][static_cast<std::size_t>(c * CALLS)];
				Matrix const expected = f == 0 ? view_projection(cameras[camera]) : before[camera];
				CHECK_SAME(dc->shader_previous_world_view_projection(), expected * moved(0, 0));
			}
		for (std::size_t c = 0; c < cameras.size(); ++c)
			before[c] = view_projection(cameras[c]);
	}
}

BOOST_FIXTURE_TEST_CASE(a_cut_camera_has_no_previous_view_projection_but_what_it_sees_keeps_its_motion, NullScene)
{
	CameraPtr const camera = camera_at(0, 0);
	Matrix const w0 = moved(0.25f, 0), w1 = moved(0.5f, 0), v0 = moved(-0.5f, 0), v1 = moved(-0.25f, 0.25f);
	DrawCallPtr const dc = call(camera, w0), other = call(camera, v0);
	frame(renderer, {dc, other}, [&] { dc->commit(); other->commit(); });
	camera->look_at(rove::vec<3>(5, 5, 1), rove::vec<3>(5, 5, 0), rove::vec<3>(0, 1, 0));
	camera->cut();
	dc->set_world_matrix(w1);
	other->set_world_matrix(v1);
	frame(renderer, {dc, other}, [&] { dc->commit(); other->commit(); });
	CHECK_SAME(dc->shader_previous_world_view_projection(), view_projection(camera) * w0);
	CHECK_SAME(dc->shader_previous_world(), w0);
	// and every call the cut camera sees after it in the frame
	CHECK_SAME(other->shader_previous_world_view_projection(), view_projection(camera) * v0);
	CHECK_SAME(other->shader_previous_world(), v0);

	// the frame after the cut, the camera's motion is back
	Matrix const vp1 = view_projection(camera);
	camera->look_at(rove::vec<3>(5.25f, 5, 1), rove::vec<3>(5.25f, 5, 0), rove::vec<3>(0, 1, 0));
	frame(renderer, {dc}, [&] { dc->commit(); });
	CHECK_SAME(dc->shader_previous_world_view_projection(), vp1 * w1);
}

BOOST_FIXTURE_TEST_CASE(reset_previous_forgets_where_the_call_was, NullScene)
{
	CameraPtr const camera = camera_at(0, 0);
	DrawCallPtr const dc = call(camera, moved(0.25f, 0));
	frame(renderer, {dc}, [&] { dc->commit(); });
	Matrix const vp0 = view_projection(camera);
	camera->look_at(rove::vec<3>(0.5f, 0, 1), rove::vec<3>(0.5f, 0, 0), rove::vec<3>(0, 1, 0));
	Matrix const w1 = moved(-0.75f, 0.5f);
	dc->set_world_matrix(w1);
	dc->reset_previous();     // another object now: it was where it is, the camera still moved
	frame(renderer, {dc}, [&] { dc->commit(); });
	CHECK_SAME(dc->shader_previous_world(), w1);
	CHECK_SAME(dc->shader_previous_world_view_projection(), vp0 * w1);

	// reset inside a frame, after a commit: from the next commit on
	Matrix const vp1 = view_projection(camera);
	Matrix const w2 = moved(0.125f, 0);
	dc->set_world_matrix(w2);
	renderer->begin_frame();
	dc->commit();
	CHECK_SAME(dc->shader_previous_world(), w1);
	dc->reset_previous();
	dc->commit();
	CHECK_SAME(dc->shader_previous_world(), w2);
	CHECK_SAME(dc->shader_previous_world_view_projection(), vp1 * w2);
	renderer->finish_frame();
}

BOOST_FIXTURE_TEST_CASE(a_call_drawn_at_two_places_in_a_frame_is_two_things_and_has_no_previous_placement, NullScene)
{
	CameraPtr const camera = camera_at(0, 0);
	Matrix const a = moved(0.25f, 0), b = moved(-0.25f, 0), c = moved(0, 0.5f);
	DrawCallPtr const dc = call(camera, a);
	frame(renderer, {}, [&] {
		dc->commit();
		dc->set_world_matrix(b);
		dc->commit();
	});
	dc->set_world_matrix(c);
	frame(renderer, {dc}, [&] { dc->commit(); });
	CHECK_SAME(dc->shader_previous_world(), c);     // not a's, not b's: which was it?

	// once a frame, then twice: the first of the frame has the frame before's, the second (another place) none
	Matrix const d = moved(0.5f, 0.5f);
	renderer->begin_frame();
	dc->set_world_matrix(d);
	dc->commit();
	CHECK_SAME(dc->shader_previous_world(), c);
	dc->set_world_matrix(a);
	dc->commit();
	CHECK_SAME(dc->shader_previous_world(), a);
	renderer->finish_frame();
	frame(renderer, {dc}, [&] { dc->commit(); });
	CHECK_SAME(dc->shader_previous_world(), a);     // frame before had two places: none
}

BOOST_FIXTURE_TEST_CASE(an_instanced_calls_previous_instances_follow_the_instances_in_view, NullScene)
{
	CameraPtr const camera = camera_at(0, 0);
	Matrix const w0 = moved(0, 0.125f), w1 = moved(0, -0.125f);
	DrawCallPtr const dc = call(camera, w0);
	Matrices const before = {moved(-0.5f, 0), moved(0, 0), moved(0.5f, 0)};
	dc->set_instances(before);
	Matrix const vp0 = view_projection(camera);
	frame(renderer, {dc}, [&] { dc->commit(); });
	BOOST_CHECK_EQUAL(dc->previous_visible_instances().size(), 3u);
	CHECK_SAME_LIST(dc->previous_visible_instances(), dc->visible_instances());

	// the second out of view now, and a fourth that is new
	Matrices const after = {moved(-0.5f, 0.25f), moved(5, 0), moved(0.5f, -0.25f), moved(0, 0.5f)};
	dc->set_instances(after);
	dc->set_world_matrix(w1);
	camera->look_at(rove::vec<3>(0.125f, 0, 1), rove::vec<3>(0.125f, 0, 0), rove::vec<3>(0, 1, 0));
	frame(renderer, {dc}, [&] { dc->commit(); });
	Matrices const visible = {after[0] * w1, after[2] * w1, after[3] * w1};
	Matrices const previous = {before[0] * w0, before[2] * w0, after[3] * w1};
	CHECK_SAME_LIST(dc->visible_instances(), visible);
	CHECK_SAME_LIST(dc->previous_visible_instances(), previous);
	for (size_t i = 0; i < previous.size() && i < dc->previous_visible_instances().size(); ++i)
		BOOST_CHECK_MESSAGE(same(dc->previous_visible_instances()[i], previous[i]), "previous instance " << i << " "
			<< describe(dc->previous_visible_instances()[i]) << ", want " << describe(previous[i]));
	// the camera's alone, as this frame's is
	CHECK_SAME(dc->shader_previous_world_view_projection(), vp0);
	CHECK_SAME(dc->shader_previous_world(), Matrix());

	// fewer instances the frame after: each keeps its own
	Matrices const fewer = {moved(-0.5f, 0)};
	dc->set_instances(fewer);
	frame(renderer, {dc}, [&] { dc->commit(); });
	CHECK_SAME_LIST(dc->previous_visible_instances(), Matrices{after[0] * w1});
}

BOOST_FIXTURE_TEST_CASE(a_call_instanced_now_that_was_not_has_no_previous_placement, NullScene)
{
	CameraPtr const camera = camera_at(0, 0);
	DrawCallPtr const dc = call(camera, moved(0.25f, 0));
	frame(renderer, {dc}, [&] { dc->commit(); });
	Matrices const instances = {moved(-0.25f, 0)};
	dc->set_instances(instances);
	frame(renderer, {dc}, [&] { dc->commit(); });
	CHECK_SAME_LIST(dc->previous_visible_instances(), dc->visible_instances());
	CHECK_SAME(dc->shader_previous_world_view_projection(), view_projection(camera));
}

BOOST_FIXTURE_TEST_CASE(a_skinned_calls_previous_joints_are_the_frame_befores_palette, NullScene)
{
	CameraPtr const camera = camera_at(0, 0);
	UberDrawCallPtr const dc = renderer->create_uber_draw_call();
	square.fill(*dc, material, camera, true);
	Matrices const p0 = {moved(0.25f, 0), moved(0, 0.25f)}, p1 = {moved(0.5f, 0), turned(0.5f)};
	dc->set_joint_matrix_array(p0);
	frame(renderer, {dc}, [&] { dc->commit(); });
	BOOST_REQUIRE(dc->previous_joint_matrices());
	CHECK_SAME_LIST(*dc->previous_joint_matrices(), p0);
	dc->set_joint_matrix_array(p1);
	frame(renderer, {dc}, [&] { dc->commit(); });
	BOOST_REQUIRE(dc->previous_joint_matrices());
	CHECK_SAME_LIST(*dc->previous_joint_matrices(), p0);
	CHECK_SAME_LIST(*dc->joint_matrices(), p1);

	// a palette of another length is another skeleton: none before it
	Matrices const p2 = {moved(1, 0), moved(0, 1), moved(1, 1)};
	dc->set_joint_matrix_array(p2);
	frame(renderer, {dc}, [&] { dc->commit(); });
	CHECK_SAME_LIST(*dc->previous_joint_matrices(), p2);
	CHECK_SAME(dc->shader_previous_world(), dc->shader_world());

	// joint matrices set after the commit are drawn (UberDrawCall reads them as it is drawn): of the length the commit saw, the commit's
	// previous ones stand
	Matrices const p3 = {moved(2, 0), moved(0, 2), moved(2, 2)};
	dc->set_joint_matrix_array(p3);
	CHECK_SAME_LIST(*dc->previous_joint_matrices(), p2);
	// of another length, drawn in the commit's frame, their previous ones are not known: refused, not made up
	renderer->begin_frame();
	dc->commit();
	dc->set_joint_matrix_array(p0);
	renderer->begin_pass(renderer->default_render_target());
	BOOST_CHECK_THROW(dc->execute(), std::logic_error);
	renderer->end_pass();
	renderer->finish_frame();
	// drawn in a frame after (not committed again: it did not move), they are their own
	frame(renderer, {dc}, [] {});
	CHECK_SAME_LIST(*dc->previous_joint_matrices(), p0);
}

BOOST_FIXTURE_TEST_CASE(what_is_given_as_the_previous_frames_stands_for_the_frame_it_is_given_in, NullScene)
{
	CameraPtr const camera = camera_at(0, 0);
	Matrix const w0 = moved(0.25f, 0), w1 = moved(0.5f, 0), given = moved(-0.5f, -0.5f);
	UberDrawCallPtr const dc = renderer->create_uber_draw_call();
	square.fill(*dc, material, camera, true);
	Matrices const palette = {moved(0.25f, 0), moved(0, 0.25f)}, given_palette = {turned(0.25f), turned(-0.25f)};
	dc->set_joint_matrix_array(palette);
	dc->set_world_matrix(w0);
	// a call made afresh each frame (as a model's are) is told where its thing was: given between frames, of the next frame
	dc->set_previous_world_matrix(given);
	dc->set_previous_joint_matrix_array(given_palette);
	frame(renderer, {dc}, [&] { dc->commit(); });
	CHECK_SAME(dc->shader_previous_world(), given);
	CHECK_SAME(dc->shader_previous_world_view_projection(), view_projection(camera) * given);
	CHECK_SAME_LIST(*dc->previous_joint_matrices(), given_palette);

	// the frame after, what the call remembers: frame 0's own
	dc->set_world_matrix(w1);
	frame(renderer, {dc}, [&] { dc->commit(); });
	CHECK_SAME(dc->shader_previous_world(), w0);
	CHECK_SAME_LIST(*dc->previous_joint_matrices(), palette);

	// given inside a frame, for that frame's commits
	renderer->begin_frame();
	dc->set_previous_world_matrix(given);
	dc->commit();
	CHECK_SAME(dc->shader_previous_world(), given);
	dc->commit();
	CHECK_SAME(dc->shader_previous_world(), given);
	renderer->finish_frame();

	// instances: each given one for the instance of its place, the instances past them new
	DrawCallPtr const instanced = call(camera, Matrix());
	Matrices const instances = {moved(-0.5f, 0), moved(5, 0), moved(0.5f, 0)};
	instanced->set_instances(instances);
	Matrices const given_instances = {moved(-0.5f, 0.25f), moved(0, 0)};
	instanced->set_previous_instances(given_instances);
	frame(renderer, {instanced}, [&] { instanced->commit(); });
	CHECK_SAME_LIST(instanced->previous_visible_instances(), (Matrices{given_instances[0], instances[2]}));
}

BOOST_FIXTURE_TEST_CASE(what_is_given_as_the_previous_frames_must_fit_the_call, NullScene)
{
	CameraPtr const camera = camera_at(0, 0);
	DrawCallPtr const plain = call(camera, Matrix());
	BOOST_CHECK_THROW(plain->set_previous_instances(Matrices()), std::invalid_argument);
	plain->set_previous_instances(Matrices{moved(1, 0)});
	BOOST_CHECK_THROW(plain->commit(), std::logic_error);      // previous instances of a call that has none

	UberDrawCallPtr const skinned = renderer->create_uber_draw_call();
	square.fill(*skinned, material, camera, true);
	skinned->set_joint_matrix_array(Matrices{moved(1, 0), moved(0, 1)});
	skinned->set_previous_joint_matrix_array(Matrices{moved(1, 0)});
	BOOST_CHECK_THROW(skinned->commit(), std::invalid_argument);     // a palette of another length
	BOOST_CHECK_THROW(skinned->set_previous_joint_matrix_array(Matrices(MAX_JOINT_MATRICES + 1)), std::length_error);

	UberDrawCallPtr const unskinned = renderer->create_uber_draw_call();
	square.fill(*unskinned, material, camera);
	unskinned->set_previous_joint_matrix_array(Matrices{moved(1, 0)});
	BOOST_CHECK_THROW(unskinned->commit(), std::logic_error);  // previous joints of a call that has none
}

// ---- models: a model drawn once a frame is one thing

namespace
{

struct ModelScene
{
	std::shared_ptr<ReadingRenderer> reading = std::make_shared<ReadingRenderer>();
	RendererPtr renderer = reading;
	SchedulerPtr scheduler = std::make_shared<Scheduler>(renderer);
	CameraPtr camera = camera_at(0, 0);

	std::shared_ptr<StaticModelPTBNT> quad() const
	{
		auto model = std::make_shared<StaticModelPTBNT>(renderer, scheduler);
		model->load_from_gltf(asset("test_gltf_loader_pbr.glb"));     // one quad, two triangle groups
		model->set_always_visible(true);
		return model;
	}

	/// A frame of @p draw's draws, flushed; what the renderer was given
	template <class Draw>
	std::vector<Seen> drawn(Draw const &draw)
	{
		reading->seen.clear();
		renderer->begin_frame();
		draw();
		renderer->begin_pass(renderer->default_render_target());
		scheduler->flush();
		renderer->end_pass();
		renderer->finish_frame();
		return reading->seen;
	}
};

}

BOOST_FIXTURE_TEST_CASE(a_model_drawn_once_a_frame_was_where_the_frame_before_drew_it, ModelScene)
{
	auto const model = quad();
	Matrix const w0 = moved(0.25f, 0), w1 = moved(-0.25f, 0.5f) * turned(0.5f);
	std::vector<Seen> const first = drawn([&] { model->draw(camera, w0); });
	BOOST_REQUIRE_EQUAL(first.size(), 2u);
	for (Seen const &s : first)
		CHECK_SAME(s.previous_world, s.world);
	std::vector<Seen> const second = drawn([&] { model->draw(camera, w1); });
	BOOST_REQUIRE_EQUAL(second.size(), 2u);
	for (size_t i = 0; i < second.size(); ++i)
	{
		CHECK_SAME(second[i].previous_world, first[i].world);
		CHECK_SAME(second[i].previous_world_view_projection, first[i].world_view_projection);
		BOOST_CHECK(!same(second[i].world, first[i].world));
	}

	// drawn in two passes of a frame (a shadow, then the view) at one place: one thing, in both passes, each through its camera
	CameraPtr const shadow = camera_at(0.5f, 0);
	drawn([&] { model->draw(shadow, w1); model->draw(camera, w1); });
	std::vector<Seen> const twice = drawn([&] { model->draw(shadow, w0); model->draw(camera, w0); });
	BOOST_REQUIRE_EQUAL(twice.size(), 4u);
	for (Seen const &s : twice)
		CHECK_SAME(s.previous_world, second[0].world);

	// reset: it was where it is
	model->reset_previous();
	std::vector<Seen> const reset = drawn([&] { model->draw(camera, w1); });
	for (Seen const &s : reset)
		CHECK_SAME(s.previous_world, s.world);
}

BOOST_FIXTURE_TEST_CASE(a_static_mesh_on_a_moving_joint_was_where_its_joint_was, ModelScene)
{
	// a rigid clip moves the joint a static mesh hangs from: the model at one place, the mesh moved
	auto const model = quad();
	JointPtr owner;
	for (JointPtr const &joint : model->joints())
		if (joint->mesh.lock())
			owner = joint;
	BOOST_REQUIRE(owner);
	Matrix const world = moved(0.25f, 0);
	owner->transforms.insert(owner->transforms.begin(), Joint::Transform{"motion", moved(-0.25f, 0.125f)});
	model->rebuild_joints();
	std::vector<Seen> const first = drawn([&] { model->draw(camera, world); });
	owner->transforms.front().matrix = moved(0, 0.5f);
	model->rebuild_joints();
	std::vector<Seen> const second = drawn([&] { model->draw(camera, world); });
	BOOST_REQUIRE_EQUAL(second.size(), first.size());
	for (size_t i = 0; i < second.size(); ++i)
	{
		CHECK_SAME(second[i].previous_world, first[i].world);
		CHECK_SAME(second[i].world, moved(0.25f, 0.375f) * first[i].world);
	}
}

BOOST_FIXTURE_TEST_CASE(a_skinned_model_hands_its_palette_on_without_copying_it, ModelScene)
{
	// the palette a skinned mesh makes for a frame is the one its calls draw with, and the one its calls draw the frame after with as
	// the previous one: kept and handed on, not copied
	auto const model = std::make_shared<AnimatedModelPTBNJWT>(renderer, scheduler);
	model->load_from_gltf(asset("test_skinned_groups.glb"));
	model->set_always_visible(true);
	std::vector<Seen> const first = drawn([&] { model->draw(camera, Matrix()); });
	std::vector<Seen> const second = drawn([&] { model->draw(camera, moved(0.25f, 0)); });
	BOOST_REQUIRE(!first.empty() && second.size() == first.size());
	for (size_t i = 0; i < second.size(); ++i)
	{
		BOOST_CHECK(first[i].joints_address == first[0].joints_address);
		BOOST_CHECK_MESSAGE(second[i].previous_joints_address == first[i].joints_address, "draw " << i << ": the previous palette is "
			"the frame before's own list");
		BOOST_CHECK(second[i].joints_address != first[i].joints_address);
	}
	// a call's own memory the same
	UberDrawCallPtr const dc = renderer->create_uber_draw_call();
	Square const square(renderer, 0.25f);
	square.fill(*dc, renderer->create_material(), camera, true);
	Placement::Matrices const palette = std::make_shared<Matrices const>(Matrices{moved(0.25f, 0), moved(0, 0)});
	dc->set_joint_matrix_array(palette);
	drawn([&] { dc->commit(); scheduler->add(dc); });
	dc->set_joint_matrix_array(Matrices{moved(0.5f, 0), moved(0, 0)});
	drawn([&] { dc->commit(); scheduler->add(dc); });
	BOOST_CHECK(dc->previous_joint_matrices() == palette.get());
}

BOOST_FIXTURE_TEST_CASE(a_model_drawn_every_other_frame_was_where_its_cameras_frame_before_drew_it, ModelScene)
{
	auto const model = quad();
	CameraPtr const screen = camera_at(5, 5);
	std::vector<Seen> const first = drawn([&] { model->draw(camera, moved(0.25f, 0)); });
	drawn([&] { quad()->draw(screen, Matrix()); });           // the screen's frame: another camera, another model
	std::vector<Seen> const third = drawn([&] { model->draw(camera, moved(0.5f, 0)); });
	BOOST_REQUIRE_EQUAL(third.size(), first.size());
	for (size_t i = 0; i < third.size(); ++i)
		CHECK_SAME(third[i].previous_world, first[i].world);
	// sat out a frame its camera drew (hidden, say): it was where it is
	drawn([&] { quad()->draw(camera, Matrix()); });
	std::vector<Seen> const fifth = drawn([&] { model->draw(camera, moved(0.75f, 0)); });
	for (Seen const &s : fifth)
		CHECK_SAME(s.previous_world, s.world);
}

BOOST_FIXTURE_TEST_CASE(a_model_drawn_at_two_places_a_frame_is_two_things_whose_motion_is_not_known, ModelScene)
{
	auto const model = quad();
	drawn([&] { model->draw(camera, moved(0.25f, 0)); model->draw(camera, moved(-0.25f, 0)); });
	std::vector<Seen> const after = drawn([&] { model->draw(camera, moved(0.5f, 0)); model->draw(camera, moved(-0.5f, 0)); });
	BOOST_REQUIRE_EQUAL(after.size(), 4u);
	for (Seen const &s : after)
		CHECK_SAME(s.previous_world, s.world);

	// a copy of the model is a thing of its own
	auto const copy = std::make_shared<StaticModelPTBNT>(*model, scheduler);
	drawn([&] { model->draw(camera, moved(0.25f, 0)); copy->draw(camera, moved(-0.25f, 0)); });
	drawn([&] { model->draw(camera, moved(0.25f, 0)); copy->draw(camera, moved(-0.25f, 0)); });
	std::vector<Seen> const own = drawn([&] { model->draw(camera, moved(0.5f, 0)); copy->draw(camera, moved(-0.5f, 0)); });
	BOOST_REQUIRE_EQUAL(own.size(), 4u);
	CHECK_SAME(own[0].previous_world, moved(-0.25f, 0) * own[0].world);     // from 0.25 to 0.5
	CHECK_SAME(own[2].previous_world, moved(0.25f, 0) * own[2].world);      // from -0.25 to -0.5
}

BOOST_FIXTURE_TEST_CASE(a_model_drawn_as_instances_was_where_its_instances_were, ModelScene)
{
	auto const model = quad();
	Matrices const before = {moved(-0.5f, 0), moved(0.5f, 0)}, after = {moved(-0.5f, 0.25f), moved(0.5f, -0.25f), moved(0, 0.5f)};
	std::vector<Seen> const first = drawn([&] { model->draw_instances(camera, before); });
	std::vector<Seen> const second = drawn([&] { model->draw_instances(camera, after); });
	BOOST_REQUIRE_EQUAL(second.size(), 2u);
	for (size_t i = 0; i < second.size(); ++i)
	{
		BOOST_REQUIRE_EQUAL(second[i].previous_visible.size(), 3u);
		CHECK_SAME(second[i].previous_visible[0], first[i].visible[0]);
		CHECK_SAME(second[i].previous_visible[1], first[i].visible[1]);
		CHECK_SAME(second[i].previous_visible[2], second[i].visible[2]);
	}
	// drawn once at one place, then as instances: another make-up, none before
	drawn([&] { model->draw(camera, moved(0.25f, 0)); });
	std::vector<Seen> const third = drawn([&] { model->draw_instances(camera, after); });
	for (Seen const &s : third)
		CHECK_SAME_LIST(s.previous_visible, s.visible);
}

BOOST_FIXTURE_TEST_CASE(a_skinned_model_was_in_the_pose_the_frame_before_drew_it_in, ModelScene)
{
	auto const model = std::make_shared<AnimatedModelPTBNJWT>(renderer, scheduler);
	model->load_from_gltf(asset("test_skinned_groups.glb"));
	model->set_always_visible(true);
	JointPtr const tip = model->joint_by_name("tip");
	BOOST_REQUIRE(tip);
	std::vector<Seen> const first = drawn([&] { model->draw(camera, Matrix()); });
	BOOST_REQUIRE(!first.empty());
	for (Seen const &s : first)
	{
		BOOST_REQUIRE(s.skinned);
		CHECK_SAME_LIST(s.previous_joints, s.joints);
	}
	tip->transforms.insert(tip->transforms.begin(), Joint::Transform{"motion", moved(0.25f, 0)});
	model->rebuild_joints();
	std::vector<Seen> const second = drawn([&] { model->draw(camera, moved(0, 0.5f)); });
	BOOST_REQUIRE_EQUAL(second.size(), first.size());
	for (size_t i = 0; i < second.size(); ++i)
	{
		CHECK_SAME_LIST(second[i].previous_joints, first[i].joints);
		BOOST_CHECK(!same(second[i].joints, first[i].joints));
		CHECK_SAME(second[i].previous_world, first[i].world);
	}
}

// ---- pixels: what a shader reads

namespace
{

/// A float target, a camera over it, the motion shaders: the square drawn writes its motion since the frame before in texture
/// coordinates of the target (u right, v down) into red and green, and 1 into alpha.
struct MotionScene
{
	RendererPtr renderer;
	Square square;
	CameraPtr camera = camera_at(0, 0);
	RenderTargetPtr target;
	MaterialPtr material, skinned_material;
	int const size = 64;

	explicit MotionScene(RendererPtr const &renderer_):
		renderer(renderer_),
		square(renderer_, 0.25f)
	{
		target = renderer->create_render_target(size, size, PixelFormat::R32G32B32A32F);
		material = renderer->create_material();
		material->set_cull_mode(Material::CullMode::NONE);
		material->set_shader_program(renderer->create_shader_program(test_shader("test_motion.vert"), test_shader("test_motion.frag")));
		skinned_material = material->clone();
		skinned_material->set_shader_program(renderer->create_shader_program(test_shader("test_motion_skin.vert"),
			test_shader("test_motion.frag")));
	}

	void look_at(float x, float y) const
	{
		camera->look_at(rove::vec<3>(x, y, 1), rove::vec<3>(x, y, 0), rove::vec<3>(0, 1, 0));
	}

	Image<rove::rgba<float>> render(std::vector<DrawCallPtr> const &calls) const
	{
		RenderPassClearState clear;
		clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = 0;
		renderer->begin_frame();
		for (DrawCallPtr const &call : calls)
			call->commit();
		renderer->begin_pass(target, clear);
		for (DrawCallPtr const &call : calls)
			call->execute();
		renderer->end_pass();
		renderer->finish_frame();
		return read_image<rove::rgba<float>>(*target->color_texture());
	}

	/// The pixel where the camera sees normalised device coordinates (x, y)
	rove::rgba<float> at(Image<rove::rgba<float>> const &image, float x, float y) const
	{
		return image.at(column(x), row(y));
	}

	int column(float x) const { return std::min(size - 1, int((x + 1) / 2 * size)); }
	int row(float y) const { return std::min(size - 1, int((1 - y) / 2 * size)); }

	/// The normalised device coordinates of the centre of the pixel at (x, y), where the shader is run for it
	std::pair<float, float> centre(float x, float y) const
	{
		return {(column(x) + 0.5f) / size * 2 - 1, 1 - (row(y) + 0.5f) / size * 2};
	}

	static std::string text(rove::rgba<float> const &p)
	{
		std::ostringstream out;
		out << "(" << p.r << ", " << p.g << ", " << p.b << ", " << p.a << ")";
		return out.str();
	}
};

/// Drawn (alpha 1) with this motion (red, green)
#define CHECK_MOTION(pixel, u, v) BOOST_CHECK_MESSAGE((pixel).a == 1 && std::abs((pixel).r - (u)) < 1e-4f && std::abs((pixel).g - (v)) < 1e-4f, \
	#pixel " is " << MotionScene::text(pixel) << ", want motion (" << (u) << ", " << (v) << ") drawn")

}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_square_moving_under_a_moving_camera_writes_its_motion, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	MotionScene const scene(fixture.renderer);
	DrawCallPtr const dc = fixture.renderer->create_draw_call();
	scene.square.fill(*dc, scene.material, scene.camera);

	// frame 0: seen the first time, through a camera seen the first time: no motion
	auto image = scene.render({dc});
	CHECK_MOTION(scene.at(image, 0, 0), 0.0f, 0.0f);

	// frame 1: the square to (0.25, 0.125), the camera to (-0.125, 0): on the screen from (0, 0) to (0.375, 0.125), half that in
	// texture coordinates, v down
	dc->set_world_matrix(moved(0.25f, 0.125f));
	scene.look_at(-0.125f, 0);
	image = scene.render({dc});
	CHECK_MOTION(scene.at(image, 0.375f, 0.125f), 0.1875f, -0.0625f);
	CHECK_MOTION(scene.at(image, 0.55f, 0.3f), 0.1875f, -0.0625f);
	BOOST_CHECK_MESSAGE(scene.at(image, -0.2f, -0.2f).a == 0, "where the square was is clear");

	// frame 2: neither moves
	image = scene.render({dc});
	CHECK_MOTION(scene.at(image, 0.375f, 0.125f), 0.0f, 0.0f);

	// frame 3: the camera cut to (0.5, 0.25), the square moved on by (0, -0.25): its own motion only, (0, -0.25) on the screen (it
	// would be (-0.625, -0.5) from where the camera was)
	dc->set_world_matrix(moved(0.25f, -0.125f));
	scene.look_at(0.5f, 0.25f);
	scene.camera->cut();
	image = scene.render({dc});
	CHECK_MOTION(scene.at(image, -0.25f, -0.375f), 0.0f, 0.125f);

	// frame 4: turned a quarter about its centre (-0.25, -0.375 on the screen): a point d from the centre now was R^-1 d from it, R
	// taking (x, y) to (-y, x); its motion on the screen d - R^-1 d = (dx - dy, dx + dy), at the pixel's centre
	Matrix quarter;
	quarter.ij[0][0] = 0, quarter.ij[0][1] = 1, quarter.ij[1][0] = -1, quarter.ij[1][1] = 0;
	dc->set_world_matrix(moved(0.25f, -0.125f) * quarter);
	image = scene.render({dc});
	for (auto const &[x, y] : {std::pair{-0.25f, -0.2f}, std::pair{-0.1f, -0.375f}, std::pair{-0.3f, -0.45f}})
	{
		auto const [cx, cy] = scene.centre(x, y);
		float const dx = cx + 0.25f, dy = cy + 0.375f;
		CHECK_MOTION(scene.at(image, x, y), 0.5f * (dx - dy), -0.5f * (dx + dy));
	}
}

BOOST_AUTO_TEST_CASE_TEMPLATE(each_instance_writes_its_own_motion_when_one_before_it_is_culled, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	MotionScene const scene(fixture.renderer);
	DrawCallPtr const dc = fixture.renderer->create_draw_call();
	scene.square.fill(*dc, scene.material, scene.camera);
	dc->set_world_matrix(moved(0, 0.125f));
	dc->set_instances({moved(-0.5f, -0.5f), moved(0, -0.5f), moved(0.5f, -0.5f)});
	scene.render({dc});

	// the middle one out of view: the third is drawn second, from the third's place, not the second's
	dc->set_world_matrix(moved(0, 0.25f));
	dc->set_instances({moved(-0.5f, -0.25f), moved(5, 0), moved(0.5f, -0.75f), moved(0, 0.25f)});
	auto const image = scene.render({dc});
	// first: from (-0.5, -0.375) to (-0.5, 0): screen (0, 0.375), texture (0, -0.1875)
	CHECK_MOTION(scene.at(image, -0.5f, 0.0f), 0.0f, -0.1875f);
	// third: from (0.5, -0.375) to (0.5, -0.5): screen (0, -0.125), texture (0, 0.0625)
	CHECK_MOTION(scene.at(image, 0.5f, -0.5f), 0.0f, 0.0625f);
	// fourth, new: no motion of its own
	CHECK_MOTION(scene.at(image, 0.0f, 0.5f), 0.0f, 0.0f);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_skinned_square_writes_the_motion_of_its_joints, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	MotionScene const scene(fixture.renderer);
	UberDrawCallPtr const dc = fixture.renderer->create_uber_draw_call();
	scene.square.fill(*dc, scene.skinned_material, scene.camera, true);
	// half of each joint: the square at the mean of the two moves
	dc->set_joint_matrix_array({moved(0.2f, 0), moved(0, 0)});
	auto image = scene.render({dc});
	CHECK_MOTION(scene.at(image, 0.1f, 0), 0.0f, 0.0f);
	dc->set_joint_matrix_array({moved(0.4f, 0), moved(0, 0.2f)});
	image = scene.render({dc});
	// from (0.1, 0) to (0.2, 0.1): screen (0.1, 0.1), texture (0.05, -0.05)
	CHECK_MOTION(scene.at(image, 0.2f, 0.1f), 0.05f, -0.05f);

	// instanced and skinned: each instance's previous place, the palette's previous pose
	dc->set_instances({moved(-0.5f, 0), moved(0.5f, 0)});
	scene.render({dc});
	dc->set_instances({moved(-0.5f, 0.25f), moved(0.5f, 0)});
	dc->set_joint_matrix_array({moved(0.4f, 0), moved(0, 0.4f)});
	image = scene.render({dc});
	// the joints move it (0, 0.1); the first instance (0, 0.25) more: texture (0, -0.175) and (0, -0.05)
	CHECK_MOTION(scene.at(image, -0.3f, 0.45f), 0.0f, -0.175f);
	CHECK_MOTION(scene.at(image, 0.7f, 0.2f), 0.0f, -0.05f);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_skinned_model_posed_anew_writes_the_motion_of_its_pose, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	MotionScene const scene(fixture.renderer);
	SchedulerPtr const scheduler = std::make_shared<Scheduler>(fixture.renderer);
	auto const model = std::make_shared<AnimatedModelPTBNJWT>(fixture.renderer, scheduler);
	model->load_from_gltf(asset("test_skinned_groups.glb"));
	model->set_materials_shader_program(scene.skinned_material->shader_program());
	for (MaterialPtr const &m : model->materials())
		m->set_cull_mode(Material::CullMode::NONE);
	JointPtr const tip = model->joint_by_name("tip");
	BOOST_REQUIRE(tip);
	// the quad of group 2 (x 4..5, y 1..2, all of it the tip's) in view: the camera over x 3.5..5.5, y 0.5..2.5
	scene.look_at(4.5f, 1.5f);
	RenderPassClearState clear;
	clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = 0;
	auto const render = [&](Matrix const &world) {
		model->draw(scene.camera, world);
		fixture.renderer->begin_frame();
		fixture.renderer->begin_pass(scene.target, clear);
		scheduler->flush();
		fixture.renderer->end_pass();
		fixture.renderer->finish_frame();
		return read_image<rove::rgba<float>>(*scene.target->color_texture());
	};
	auto image = render(Matrix());
	CHECK_MOTION(scene.at(image, 0, 0), 0.0f, 0.0f);
	tip->transforms.insert(tip->transforms.begin(), Joint::Transform{"motion", moved(0.25f, 0)});
	model->rebuild_joints();
	image = render(moved(0, 0.125f));
	// the tip's quad from centre (4.5, 1.5) to (4.75, 1.625): screen (0.25, 0.125), texture (0.125, -0.0625)
	CHECK_MOTION(scene.at(image, 0.25f, 0.125f), 0.125f, -0.0625f);
}

// ---- what review found: a camera seen less often, a call drawn again without a commit

namespace
{

/// A call moving 0.125 in x a frame, through `main` every frame and `sensor` every other frame (committed first or last), each camera
/// moving its own way: each commit's previous matrices are those of its own camera's frame before, both the call's place and the
/// camera's view-projection then.
void check_two_cadences(bool sensor_first)
{
	RendererPtr const renderer = std::make_shared<NullRenderer>(64, 64);
	MaterialPtr const material = renderer->create_material();
	Square const square(renderer, 0.25f);
	CameraPtr const main = camera_at(0, 0), sensor = camera_at(0, 0);
	DrawCallPtr const dc = renderer->create_draw_call();
	square.fill(*dc, material, main);
	std::map<Camera const *, std::pair<Matrix, Matrix>> last;      // each camera's view-projection and the call's world in its last frame
	for (int f = 0; f < 7; ++f)
	{
		Matrix const world = moved(0.125f * f, 0);
		dc->set_world_matrix(world);
		main->look_at(rove::vec<3>(0.0625f * f, 0, 1), rove::vec<3>(0.0625f * f, 0, 0), rove::vec<3>(0, 1, 0));
		sensor->look_at(rove::vec<3>(0, 0.03125f * f, 1), rove::vec<3>(0, 0.03125f * f, 0), rove::vec<3>(0, 1, 0));
		std::vector<CameraPtr> cameras = {main};
		if (f % 2 == 0)
			cameras.insert(sensor_first ? cameras.begin() : cameras.end(), sensor);
		renderer->begin_frame();
		for (CameraPtr const &camera : cameras)
		{
			dc->set_camera(camera);
			dc->commit();
			auto const before = last.find(camera.get());
			Matrix const want_world = before == last.end() ? world : before->second.second;
			Matrix const want_vp = before == last.end() ? view_projection(camera) : before->second.first;
			char const *const name = camera == main ? "main" : "sensor";
			BOOST_CHECK_MESSAGE(same(dc->shader_previous_world(), want_world), (sensor_first ? "sensor first" : "main first")
				<< ", frame " << f << ", " << name << ": previous world " << describe(dc->shader_previous_world()) << ", want "
				<< describe(want_world));
			BOOST_CHECK_MESSAGE(same(dc->shader_previous_world_view_projection(), want_vp * want_world), (sensor_first
				? "sensor first" : "main first") << ", frame " << f << ", " << name << ": previous world-view-projection "
				<< describe(dc->shader_previous_world_view_projection()) << ", want " << describe(want_vp * want_world));
		}
		renderer->finish_frame();
		for (CameraPtr const &camera : cameras)
			last[camera.get()] = {view_projection(camera), world};
	}
}

}

BOOST_AUTO_TEST_CASE(each_camera_finds_its_own_frame_before_whichever_commits_first)
{
	check_two_cadences(true);
	check_two_cadences(false);
}

BOOST_FIXTURE_TEST_CASE(a_camera_drawing_more_rarely_than_the_frames_kept_finds_no_frame_before, NullScene)
{
	// kept: the frames of the last PlacementHistory::FRAMES (4) commits before this frame's; a camera every 4th frame finds its own,
	// one every 5th none: it was where it is
	for (int cadence : {4, 5})
	{
		CameraPtr const main = camera_at(0, 0), rare = camera_at(0, 0);
		DrawCallPtr const dc = call(main, moved(0, 0));
		for (int f = 0; f <= cadence; ++f)
		{
			Matrix const world = moved(0.125f * f, 0);
			dc->set_world_matrix(world);
			renderer->begin_frame();
			dc->set_camera(main);
			dc->commit();
			if (f % cadence == 0)
			{
				dc->set_camera(rare);
				dc->commit();
				if (f == cadence)
				{
					Matrix const want = cadence <= int(PlacementHistory::FRAMES) ? moved(0, 0) : world;
					BOOST_CHECK_MESSAGE(same(dc->shader_previous_world(), want), "a camera every " << cadence << " frames: previous "
						<< describe(dc->shader_previous_world()) << ", want " << describe(want));
				}
			}
			renderer->finish_frame();
		}
	}
}

BOOST_FIXTURE_TEST_CASE(a_model_seen_by_two_cameras_of_two_cadences_was_where_each_cameras_frame_before_drew_it, ModelScene)
{
	auto const model = quad();
	CameraPtr const sensor = camera_at(0, 0);
	for (bool sensor_first : {true, false})
	{
		model->reset_previous();
		for (int f = 0; f < 6; ++f)
		{
			Matrix const world = moved(0.125f * f, 0);
			bool const with_sensor = f % 2 == 0;
			std::vector<Seen> const seen = drawn([&] {
				if (with_sensor && sensor_first)
					model->draw(sensor, world);
				model->draw(camera, world);
				if (with_sensor && !sensor_first)
					model->draw(sensor, world);
			});
			size_t const groups = with_sensor ? seen.size() / 2 : seen.size();
			for (size_t i = 0; i < seen.size(); ++i)
			{
				bool const is_sensor = with_sensor && ((i < groups) == sensor_first);
				int const back = f == 0 ? 0 : is_sensor ? 2 : 1;
				// the model's world then, the joint's matrix the same
				Matrix const expected = moved(0.125f * float(f - back), 0) * (moved(-0.125f * f, 0) * seen[i].world);
				BOOST_CHECK_MESSAGE(same(seen[i].previous_world, expected), (sensor_first ? "sensor first" : "main first") << ", frame "
					<< f << ", " << (is_sensor ? "sensor" : "main") << " draw " << i << ": previous world "
					<< describe(seen[i].previous_world) << ", want " << describe(expected));
			}
		}
	}
}

BOOST_FIXTURE_TEST_CASE(a_call_drawn_again_without_a_commit_has_not_moved_since, NullScene)
{
	// commit()'s contract: committed again after the call or its camera changes; drawn in a later frame without a commit, it did not
	CameraPtr const camera = camera_at(0, 0);
	DrawCallPtr const dc = call(camera, moved(0, 0));
	frame(renderer, {dc}, [&] { dc->commit(); });
	dc->set_world_matrix(moved(0.5f, 0));
	frame(renderer, {dc}, [&] { dc->commit(); });
	CHECK_SAME(dc->shader_previous_world(), moved(0, 0));
	for (int f = 2; f < 4; ++f)
	{
		frame(renderer, {dc}, [] {});
		CHECK_SAME(dc->shader_previous_world(), moved(0.5f, 0));
		CHECK_SAME(dc->shader_previous_world_view_projection(), dc->shader_world_view_projection());
	}
	// instanced and skinned the same
	UberDrawCallPtr const skinned = renderer->create_uber_draw_call();
	square.fill(*skinned, material, camera, true);
	skinned->set_instances({moved(-0.5f, 0), moved(0.5f, 0)});
	skinned->set_joint_matrix_array({moved(0, 0), moved(0, 0)});
	frame(renderer, {skinned}, [&] { skinned->commit(); });
	skinned->set_instances({moved(-0.5f, 0.25f), moved(0.5f, 0.25f)});
	skinned->set_joint_matrix_array({moved(0.25f, 0), moved(0, 0)});
	frame(renderer, {skinned}, [&] { skinned->commit(); });
	frame(renderer, {skinned}, [] {});
	CHECK_SAME_LIST(skinned->previous_visible_instances(), skinned->visible_instances());
	CHECK_SAME_LIST(*skinned->previous_joint_matrices(), *skinned->joint_matrices());
}

BOOST_AUTO_TEST_CASE_TEMPLATE(each_camera_writes_its_own_motion_whichever_commits_first, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	for (bool sensor_first : {true, false})
	{
		MotionScene const scene(fixture.renderer);
		CameraPtr const sensor = camera_at(0, 0);
		RenderTargetPtr const sensor_target = fixture.renderer->create_render_target(scene.size, scene.size, PixelFormat::R32G32B32A32F);
		DrawCallPtr const dc = fixture.renderer->create_draw_call();
		scene.square.fill(*dc, scene.material, scene.camera);
		RenderPassClearState clear;
		clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = 0;
		for (int f = 0; f < 5; ++f)
		{
			// 0.125 a frame to the right: through the main camera 0.0625 of the target a frame, through the sensor 0.125 every other
			dc->set_world_matrix(moved(0.125f * f - 0.25f, 0));
			std::vector<std::pair<CameraPtr, RenderTargetPtr>> views = {{scene.camera, scene.target}};
			if (f % 2 == 0)
				views.insert(sensor_first ? views.begin() : views.end(), {sensor, sensor_target});
			fixture.renderer->begin_frame();
			for (auto const &[camera, target] : views)
			{
				dc->set_camera(camera);
				dc->commit();
				fixture.renderer->begin_pass(target, clear);
				dc->execute();
				fixture.renderer->end_pass();
			}
			fixture.renderer->finish_frame();
			float const x = 0.125f * f - 0.25f;
			auto const main_image = read_image<rove::rgba<float>>(*scene.target->color_texture());
			BOOST_TEST_CONTEXT((sensor_first ? "sensor first" : "main first") << ", frame " << f)
			{
				CHECK_MOTION(scene.at(main_image, x, 0), f == 0 ? 0.0f : 0.0625f, 0.0f);
				if (f % 2 == 0)
				{
					auto const sensor_image = read_image<rove::rgba<float>>(*sensor_target->color_texture());
					CHECK_MOTION(scene.at(sensor_image, x, 0), f == 0 ? 0.0f : 0.125f, 0.0f);
				}
			}
		}
	}
}

BOOST_AUTO_TEST_CASE_TEMPLATE(a_call_drawn_again_without_a_commit_writes_no_motion, F, Backends)
{
	F fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	MotionScene const scene(fixture.renderer);
	DrawCallPtr const dc = fixture.renderer->create_draw_call();
	scene.square.fill(*dc, scene.material, scene.camera);
	scene.render({dc});
	dc->set_world_matrix(moved(0.25f, 0));
	auto image = scene.render({dc});
	CHECK_MOTION(scene.at(image, 0.25f, 0), 0.125f, 0.0f);
	// drawn again, not committed: it has not moved
	RenderPassClearState clear;
	clear.clear_color_r = clear.clear_color_g = clear.clear_color_b = clear.clear_color_a = 0;
	fixture.renderer->begin_frame();
	fixture.renderer->begin_pass(scene.target, clear);
	dc->execute();
	fixture.renderer->end_pass();
	fixture.renderer->finish_frame();
	image = read_image<rove::rgba<float>>(*scene.target->color_texture());
	CHECK_MOTION(scene.at(image, 0.25f, 0), 0.0f, 0.0f);
}

BOOST_FIXTURE_TEST_CASE(a_call_says_whether_its_previous_matrices_are_known, NullScene)
{
	// what a TAA pass needs to throw its history away: has_previous(), and EDGE_FLAG_PREVIOUS_UNKNOWN in the shader's flags
	CameraPtr const camera = camera_at(0, 0);
	DrawCallPtr const dc = call(camera, moved(0, 0));
	auto const known = [&](char const *what, bool want) {
		BOOST_CHECK_MESSAGE(dc->has_previous() == want, what << ": has_previous() " << dc->has_previous() << ", want " << want);
	};
	frame(renderer, {dc}, [&] { dc->commit(); });
	known("seen the first time", false);
	dc->set_world_matrix(moved(0.25f, 0));
	frame(renderer, {dc}, [&] { dc->commit(); });
	known("the frame after", true);
	frame(renderer, {dc}, [] {});
	known("drawn again without a commit: it did not move", true);
	renderer->begin_frame();
	dc->commit();
	known("the frame's first placement", true);
	dc->set_world_matrix(moved(0.5f, 0));
	dc->commit();
	known("a second placement in the frame", false);
	renderer->finish_frame();
	frame(renderer, {dc}, [&] { dc->commit(); });
	known("after a frame of two placements", false);
	frame(renderer, {dc}, [&] { dc->commit(); });
	known("and the frame after it", true);
	dc->set_instances({moved(0, 0)});
	frame(renderer, {dc}, [&] { dc->commit(); });
	known("instanced now", false);
	frame(renderer, {dc}, [&] { dc->commit(); });
	known("instanced the frame before too", true);
	dc->reset_previous();
	frame(renderer, {dc}, [&] { dc->commit(); });
	known("reset", false);
	frame(renderer, {dc}, [&] { dc->commit(); });
	camera->cut();
	frame(renderer, {dc}, [&] { dc->commit(); });
	known("through a cut camera", false);
	frame(renderer, {dc}, [&] { dc->commit(); });
	known("the frame after the cut", true);
	dc->set_camera(camera_at(0, 0));
	frame(renderer, {dc}, [&] { dc->commit(); });
	known("through a camera seen the first time", false);
	// seen the first time, then drawn again without a commit: it did not move, which is known
	DrawCallPtr const again = call(camera, moved(0, 0));
	frame(renderer, {again}, [&] { again->commit(); });
	BOOST_CHECK(!again->has_previous());
	frame(renderer, {again}, [] {});
	BOOST_CHECK_MESSAGE(again->has_previous(), "drawn again without a commit after a first frame: did not move");
	// told where it was in full (a model's call made afresh): known
	DrawCallPtr const fresh = call(camera, moved(0, 0));
	fresh->set_previous_world_matrix(moved(0.125f, 0));
	frame(renderer, {fresh}, [&] { fresh->commit(); });
	BOOST_CHECK(fresh->has_previous());
}

#ifdef EDGE_USE_GLES
BOOST_AUTO_TEST_CASE(gles_writes_the_previous_blocks_only_for_a_program_that_reads_them)
{
	GlesFixture fixture;
	BOOST_REQUIRE_MESSAGE(fixture.renderer, "renderer unavailable: " << fixture.error);
	MotionScene const scene(fixture.renderer);
	UberDrawCallPtr const dc = fixture.renderer->create_uber_draw_call();
	scene.square.fill(*dc, scene.skinned_material, scene.camera, true);
	dc->set_instances({moved(-0.5f, 0), moved(0.5f, 0)});
	dc->set_joint_matrix_array({moved(0.2f, 0), moved(0, 0)});
	uint64_t const before = fixture.renderer->previous_block_writes();
	scene.render({dc});
	BOOST_CHECK_EQUAL(fixture.renderer->previous_block_writes() - before, 2u);   // a batch of instances, a palette
	// the built-in skinned shader reads neither (it writes no motion): nothing written
	MaterialPtr const plain = scene.material->clone();
	plain->clear_shader_program();
	dc->set_material(plain);
	uint64_t const after = fixture.renderer->previous_block_writes();
	scene.render({dc});
	BOOST_CHECK_EQUAL(fixture.renderer->previous_block_writes() - after, 0u);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
