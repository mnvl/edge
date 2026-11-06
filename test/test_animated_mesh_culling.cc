// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <rove/frustum.h>
#include <rove/scalar.h>

#include "animated_model.h"
#include "animated_mesh.h"
#include "camera.h"
#include "joint.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "test_fixture.h"

/// Frustum culling contract of a skinned mesh's draw call: the bounds the
/// draw call is culled against must enclose the mesh *as the skin shader
/// places it* for the current joint pose -- not the bind pose.  A walking
/// pedestrian's foot swings well outside its bind-pose box; a camera close
/// to the foot (the robot's cameras are 0.25 m off the ground) sees the shoe,
/// and the shoe must not be culled.

namespace
{

using model_type = edge::AnimatedModelPTBNJWT;
using mesh_type = edge::AnimatedMeshPTBNJWT;

struct CullingFixture
{
	edge::RendererPtr renderer;
	edge::SchedulerPtr scheduler;
	std::shared_ptr<model_type> model;

	CullingFixture()
	{
		renderer = std::make_shared<edge::NullRenderer>(320, 200);
		scheduler = std::make_shared<edge::Scheduler>(renderer);
		model = std::make_shared<model_type>(renderer, scheduler);
		model->load_from_gltf(edge::TestFixture::graphic_sources_path() + "test/data/test_figure.glb");
		BOOST_REQUIRE(!model->animations().empty());
		BOOST_REQUIRE(!model->joints().empty());
	}

	/// The skinned meshes and the joint that owns each, as Model::draw() walks them.
	std::vector<std::pair<edge::Joint *, mesh_type *>> skinned_meshes() const
	{
		std::vector<std::pair<edge::Joint *, mesh_type *>> result;
		for (auto const &joint : model->joints())
		{
			edge::MeshPtr mesh_ptr = joint->mesh.lock();
			if (!mesh_ptr)
				continue;
			mesh_type *mesh = dynamic_cast<mesh_type *>(mesh_ptr.get());
			BOOST_REQUIRE(mesh);
			result.push_back(std::make_pair(joint.get(), mesh));
		}
		return result;
	}

	/// Vertex positions in world space exactly as vertex_skin.glsl computes them:
	/// sum over the 8 influences of w * (J * IBM * BSM) * p, then the world matrix.
	std::vector<rove::vec<3>> skin_vertices(mesh_type const &mesh, rove::matrix<4,4> const &world) const
	{
		std::vector<rove::matrix<4,4>> skin_matrices;
		for (std::size_t joint_index : mesh.joint_index_transform())
		{
			skin_matrices.push_back(model->joints()[joint_index]->matrix *
				mesh.inverted_bind_matrices()[joint_index] * mesh.bind_shape_matrix());
		}

		std::vector<rove::vec<3>> result;
		for (auto const &vertex : mesh.vertices())
		{
			rove::vec<3> pos(0, 0, 0);
			float weight_sum = 0.0f;
			for (std::size_t j = 0; j < 8; ++j)
			{
				pos += rove::transform_point(skin_matrices.at(vertex.joints.i[j]), vertex.position) * vertex.weights.i[j];
				weight_sum += vertex.weights.i[j];
			}
			// A vertex nobody's skin weights pull collapses to the origin in the
			// shader; the test would then be aiming cameras at nothing real.
			BOOST_REQUIRE_SMALL(weight_sum - 1.0f, 1e-3f);
			result.push_back(rove::transform_point(world, pos));
		}
		return result;
	}

	static edge::CameraPtr make_camera(rove::vec<3> const &eye, rove::vec<3> const &target, float fov)
	{
		edge::CameraPtr camera(new edge::Camera());
		camera->set_perspective_projection(fov, 4.0f / 3.0f, 0.05f, 50.0f);
		rove::vec<3> up(0, 0, 1);
		rove::vec<3> dir = target - eye;
		if (std::abs(dir.z) > 0.9f * dir.length())
			up = rove::vec<3>(0, 1, 0);
		camera->look_at(eye, target, up);
		return camera;
	}

	static rove::frustum<> camera_frustum(edge::CameraPtr const &camera)
	{
		return rove::frustum<>(camera->projection_matrix() * camera->view_matrix());
	}

	/// Submits one mesh's draw calls from the given camera and reports whether
	/// every one of them survived culling.
	bool mesh_drawn(edge::Joint *joint, mesh_type *mesh, edge::CameraPtr const &camera,
		rove::matrix<4,4> const &world)
	{
		scheduler->clear();
		mesh->draw(joint, camera, world);
		edge::flush_in_a_pass(scheduler);
		BOOST_REQUIRE_EQUAL(scheduler->visible_calls_count() + scheduler->invisible_calls_count(),
			mesh->triangles().size());
		return scheduler->invisible_calls_count() == 0;
	}
};

/// A pedestrian placed in a Z-up world: rotated about Z and moved.
rove::matrix<4,4> placement(float yaw, rove::vec<3> const &t)
{
	rove::matrix<4,4> rotation;
	rotation.identity();
	rotation.ij[0][0] = std::cos(yaw);
	rotation.ij[0][1] = std::sin(yaw);
	rotation.ij[1][0] = -std::sin(yaw);
	rotation.ij[1][1] = std::cos(yaw);
	rove::matrix<4,4> translation;
	translation.identity();
	translation.ij[3][0] = t.x;
	translation.ij[3][1] = t.y;
	translation.ij[3][2] = t.z;
	return translation * rotation;
}

/// Indices of the vertices at the six extremes of the cloud -- for a walking
/// figure these are the toes, heels, hands and head, the parts that travel
/// farthest from their bind pose.
std::vector<std::size_t> extreme_vertices(std::vector<rove::vec<3>> const &points)
{
	std::vector<std::size_t> result;
	for (std::size_t axis = 0; axis < 3; ++axis)
	{
		std::size_t lo = 0, hi = 0;
		for (std::size_t i = 1; i < points.size(); ++i)
		{
			if (points[i].i[axis] < points[lo].i[axis]) lo = i;
			if (points[i].i[axis] > points[hi].i[axis]) hi = i;
		}
		result.push_back(lo);
		result.push_back(hi);
	}
	return result;
}

}

BOOST_FIXTURE_TEST_SUITE(animated_mesh_culling, CullingFixture)

/// Aim a narrow camera at a skinned vertex from close by, from several sides,
/// throughout the walk cycle.  The vertex is in view, so its mesh must be drawn.
BOOST_AUTO_TEST_CASE(skinned_vertex_in_view_is_never_culled)
{
	float const duration = static_cast<float>(model->animation_duration());
	BOOST_REQUIRE_GT(duration, 0.0f);

	rove::matrix<4,4> const world = placement(0.7f, rove::vec<3>(3.0f, -2.0f, 0.0f));
	float const fov = 10.0f * rove::PI / 180.0f;
	float const distance = 0.6f;

	std::size_t checked = 0, culled = 0;
	std::string first_failure;

	for (int step = 0; step < 12; ++step)
	{
		model->pose(duration * float(step) / 12.0f);

		for (auto const &entry : skinned_meshes())
		{
			std::vector<rove::vec<3>> const points = skin_vertices(*entry.second, world);
			BOOST_REQUIRE(!points.empty());

			for (std::size_t vertex_index : extreme_vertices(points))
			{
				rove::vec<3> const &target = points[vertex_index];

				for (int side = 0; side < 6; ++side)
				{
					rove::vec<3> offset(0, 0, 0);
					offset.i[side / 2] = (side % 2 == 0) ? distance : -distance;
					edge::CameraPtr camera = make_camera(target + offset, target, fov);

					// The vertex is in the frustum by construction; make sure of it
					// so a failure below is the culling's, not the camera's.
					BOOST_REQUIRE(camera_frustum(camera).contains(target));

					++checked;
					if (!mesh_drawn(entry.first, entry.second, camera, world))
					{
						++culled;
						if (first_failure.empty())
						{
							first_failure = "mesh '" + entry.second->name() + "' at t=" +
								std::to_string(duration * float(step) / 12.0f) +
								" vertex " + std::to_string(vertex_index) + " side " + std::to_string(side);
						}
					}
				}
			}
		}
	}

	BOOST_TEST_MESSAGE("checked " << checked << " camera/vertex pairs, culled " << culled);
	BOOST_CHECK_MESSAGE(culled == 0, culled << " of " << checked
		<< " in-view skinned vertices had their mesh culled; first: " << first_failure);
}

/// The bounds must still cull: a camera past the figure, looking away from it, must not draw any of its meshes.  The box
/// holds the figure in every pose its clips make, so it reaches beyond the figure in the pose it has now by up to its own size:
/// the camera stands that far past the figure's face.
BOOST_AUTO_TEST_CASE(figure_behind_camera_is_culled)
{
	float const duration = static_cast<float>(model->animation_duration());
	rove::matrix<4,4> const world = placement(-1.3f, rove::vec<3>(-1.0f, 4.0f, 0.0f));

	for (int step = 0; step < 6; ++step)
	{
		model->pose(duration * float(step) / 6.0f);

		rove::aabb<3> figure;
		figure.null();
		for (auto const &entry : skinned_meshes())
			for (rove::vec<3> const &p : skin_vertices(*entry.second, world))
				figure.extend(p);

		rove::vec<3> const centre = (figure.lo + figure.hi) * 0.5f;
		float reach = 0;
		for (auto const &entry : skinned_meshes())
			reach = std::max(reach, 2.0f * (entry.second->pose_bounds().hi - entry.second->pose_bounds().lo).length());
		for (int side = 0; side < 6; ++side)
		{
			rove::vec<3> dir(0, 0, 0);
			dir.i[side / 2] = (side % 2 == 0) ? 1.0f : -1.0f;
			rove::vec<3> const face = (side % 2 == 0) ? figure.hi : figure.lo;
			rove::vec<3> eye = centre;
			eye.i[side / 2] = face.i[side / 2] + dir.i[side / 2] * reach;
			edge::CameraPtr camera = make_camera(eye, eye + dir, 60.0f * rove::PI / 180.0f);

			for (auto const &entry : skinned_meshes())
			{
				BOOST_CHECK_MESSAGE(!mesh_drawn(entry.first, entry.second, camera, world),
					"mesh '" << entry.second->name() << "' drawn although the whole figure is behind the camera"
					<< " (side " << side << ", step " << step << ")");
			}
		}
	}
}

/// A model posed by hand -- its joints set and rebuilt by an application's own rules (a game's retargeted moves, a ragdoll), poses no clip
/// of it makes -- is culled by the box of the pose it is in.  The figure carried 6 m off by its root joint, outside every clip, is drawn by
/// a camera looking at where it stands, and it is held by the box whatever the camera.
BOOST_AUTO_TEST_CASE(a_model_posed_by_hand_is_culled_by_the_pose_it_is_in)
{
	rove::matrix<4,4> world;
	world.identity();
	model->pose(0.0f);
	edge::JointPtr root;
	for (auto const &joint : model->joints())
		if (!joint->parent.lock() && !joint->transforms.empty())
			root = joint;
	BOOST_REQUIRE(root);
	rove::matrix<4,4> carried;
	carried.identity();
	carried.ij[3][0] = 6.0f;
	root->transforms[0].matrix = carried * root->transforms[0].matrix;
	model->rebuild_joints();

	rove::aabb<3> figure;
	figure.null();
	for (auto const &entry : skinned_meshes())
	{
		rove::obb<3> const box = entry.second->conservative_bounds(model->joints(), *entry.first, world);
		for (rove::vec<3> const &p : skin_vertices(*entry.second, world))
		{
			BOOST_CHECK_MESSAGE(box.contains(p), "mesh '" << entry.second->name() << "': a vertex outside its box");
			figure.extend(p);
		}
	}
	BOOST_REQUIRE_GT(figure.lo.x, 4.0f);          // (carried off: none of it where the clips have it)
	rove::vec<3> const centre = (figure.lo + figure.hi) * 0.5f;
	edge::CameraPtr const camera = make_camera(centre + rove::vec<3>(0, -3, 0), centre, 30.0f * rove::PI / 180.0f);
	for (auto const &entry : skinned_meshes())
		BOOST_CHECK_MESSAGE(mesh_drawn(entry.first, entry.second, camera, world), "mesh '" << entry.second->name() << "' culled where it stands");
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(animated_mesh_skinned_bounds, CullingFixture)

/// conservative_bounds() must enclose every vertex where the skin puts it, and the box of the whole figure must stay in
/// proportion to the figure: it holds every clip (the dance too, not only the walk the figure is measured over), so it is not
/// the tight box of a pose, but it is no larger than the figure's own reach in its clips.
BOOST_AUTO_TEST_CASE(encloses_every_skinned_vertex_and_stays_in_proportion)
{
	float const duration = static_cast<float>(model->animation_duration());
	rove::matrix<4,4> identity;
	identity.identity();

	rove::aabb<3> figure, box;
	figure.null();
	box.null();
	for (int step = 0; step < 24; ++step)
	{
		model->pose(duration * float(step) / 24.0f);

		for (auto const &entry : skinned_meshes())
		{
			mesh_type const &mesh = *entry.second;
			rove::aabb<3> const bounds = mesh.conservative_bounds(model->joints(), *entry.first, identity).bounds();
			box.extend(bounds);
			for (rove::vec<3> const &p : skin_vertices(mesh, identity))
			{
				figure.extend(p);
				for (std::size_t axis = 0; axis < 3; ++axis)
				{
					BOOST_REQUIRE_MESSAGE(p.i[axis] >= bounds.lo.i[axis] - 1e-4f && p.i[axis] <= bounds.hi.i[axis] + 1e-4f,
						"mesh '" << mesh.name() << "' step " << step << ": skinned vertex outside get_conservative_bounds()");
				}
			}
		}
	}
	float const ratio = (box.hi - box.lo).length() / (figure.hi - figure.lo).length();
	BOOST_TEST_MESSAGE("box diagonal / diagonal of the figure over the walk: " << ratio);
	BOOST_CHECK_LT(ratio, 1.5f);
}

BOOST_AUTO_TEST_SUITE_END()
