// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <rove/aabb.h>
#include <rove/obb.h>

#include "animated_mesh.h"
#include "animated_model.h"
#include "camera.h"
#include "joint.h"
#include "model_cache.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "test_fixture.h"

// A skinned mesh carries one box in model space that holds it in the bind pose and in every pose its model's clips make: the
// union of its vertices over all of them, grown by half the farthest a vertex moves between two samples of a clip, so that what the
// clip does between the samples is inside too.  Culling transforms that box by the world matrix, so what is drawn costs nothing
// per joint.  A pose the clips do not make (two clips mixed, a joint turned by code) is the caller's: GltfOptions::pose_margin grows
// the box by a fraction of its size for them.  These tests pose real skeletons (test/data/test_figure.glb, a walking figure;
// test/data/test_skinned_groups.glb, two joints) and look for a vertex outside the box.

namespace
{

using Model = edge::BasicAnimatedModel<edge::VERTEX_PTBNJWT>;
using Mesh = edge::BasicAnimatedMesh<edge::VERTEX_PTBNJWT>;

struct Fixture
{
	edge::RendererPtr renderer = [] { edge::RendererPtr r(new edge::NullRenderer(320, 200)); return r; }();
	edge::SchedulerPtr scheduler = std::make_shared<edge::Scheduler>(renderer);
	std::mt19937 random{12345};

	std::shared_ptr<Model> load(std::string const &name, float pose_margin = 0)
	{
		edge::GltfOptions options;
		options.pose_margin = pose_margin;
		auto model = std::make_shared<Model>(renderer, scheduler);
		model->load_from_gltf(edge::TestFixture::graphic_sources_path() + "test/data/" + name, options);
		return model;
	}

	static std::vector<Mesh const *> skinned(Model const &model)
	{
		std::vector<Mesh const *> result;
		for (edge::MeshPtr const &m : model.meshes())
			if (auto const *mesh = dynamic_cast<Mesh const *>(m.get()))
				result.push_back(mesh);
		BOOST_REQUIRE(!result.empty());
		return result;
	}
};

/// The largest distance by which a skinned vertex is outside the box (0: all inside).
float overshoot(Mesh const &mesh, Model const &model)
{
	rove::aabb<3> const box = mesh.pose_bounds();
	float worst = 0;
	for (auto const &v : mesh.skinned_vertices(model.joints()))
		for (int axis = 0; axis < 3; ++axis)
			worst = std::max({worst, box.lo.i[axis] - v.position.i[axis], v.position.i[axis] - box.hi.i[axis]});
	return worst;
}

rove::aabb<3> union_of_all(std::vector<Mesh const *> const &meshes)
{
	rove::aabb<3> box;
	box.null();
	for (Mesh const *mesh : meshes)
		box.extend(mesh->pose_bounds());
	return box;
}

float diagonal(rove::aabb<3> const &b)
{
	return (b.hi - b.lo).length();
}

}

BOOST_FIXTURE_TEST_SUITE(pose_bounds, Fixture)

BOOST_AUTO_TEST_CASE(a_loaded_skinned_mesh_has_a_box_that_holds_its_bind_pose)
{
	auto model = load("test_figure.glb");
	for (Mesh const *mesh : skinned(*model))
		BOOST_CHECK_SMALL(overshoot(*mesh, *model), 1e-5f);
}

/// The time of a clip's last key.  The figure's clips start at 1/24 s (0.042 s, where Blender starts them): a clip lasts from 0 to here,
/// which duration() says too.
float last_key(edge::Animation const &clip)
{
	float last = 0;
	for (edge::Sampler const &sampler : clip.samplers)
		last = std::max(last, sampler.sample(sampler.samples_count() - 1).time);
	return last;
}

/// Sampled 1.5 times as densely as the box was made -- at the midpoint of every interval between the samples it was made at, the
/// farthest a pose can be from both its neighbours -- and from the start of the clip to its last key.
BOOST_AUTO_TEST_CASE(holds_the_mesh_at_every_pose_of_every_clip_between_the_samples_too)
{
	auto model = load("test_figure.glb");
	BOOST_REQUIRE(!model->animations().empty());
	std::uniform_real_distribution<float> unit(0, 1);
	for (auto const &clip : model->animations())
	{
		float const length = last_key(*clip);
		BOOST_REQUIRE_EQUAL(length, float(clip->duration()));
		int const steps = int(length * 90) + 1;
		for (int step = 1; step <= 2 * steps; step += 2) // the odd multiples of the half-step: the midpoints only
		{
			clip->pose(length * float(step) / float(2 * steps));
			for (Mesh const *mesh : skinned(*model))
				BOOST_REQUIRE_MESSAGE(overshoot(*mesh, *model) < 1e-5f, "clip '" << clip->name << "' step " << step);
		}
		for (int draw = 0; draw < 15; ++draw)
		{
			clip->pose(length * unit(random));
			for (Mesh const *mesh : skinned(*model))
				BOOST_REQUIRE_MESSAGE(overshoot(*mesh, *model) < 1e-5f, "clip '" << clip->name << "' at a random time");
		}
	}
}

/// A box that holds everything would be easy; culling needs it close to the poses the clips make.
BOOST_AUTO_TEST_CASE(is_not_much_larger_than_the_poses_of_the_clips)
{
	auto model = load("test_figure.glb");
	rove::aabb<3> seen;
	seen.null();
	for (auto const &clip : model->animations())
		for (int step = 0; step <= 120; ++step)
		{
			clip->pose(last_key(*clip) * float(step) / 120.0f);
			for (Mesh const *mesh : skinned(*model))
				for (auto const &v : mesh->skinned_vertices(model->joints()))
					seen.extend(v.position);
		}
	float const ratio = diagonal(union_of_all(skinned(*model))) / diagonal(seen);
	BOOST_TEST_MESSAGE("box diagonal / diagonal of the poses of the clips: " << ratio);
	BOOST_CHECK_GE(ratio, 0.999f);
	BOOST_CHECK_LT(ratio, 1.1f);
}

BOOST_AUTO_TEST_CASE(a_model_without_clips_has_the_box_of_its_bind_pose)
{
	auto model = load("test_skinned_groups.glb");
	BOOST_REQUIRE(model->animations().empty());
	Mesh const &mesh = *skinned(*model).front();
	rove::aabb<3> tight;
	tight.null();
	for (auto const &v : mesh.skinned_vertices(model->joints()))
		tight.extend(v.position);
	BOOST_CHECK_SMALL((mesh.pose_bounds().lo - tight.lo).length() + (mesh.pose_bounds().hi - tight.hi).length(), 1e-5f);
}

/// The margin is for the poses the clips do not make: each side grows by the fraction of the box's size.
BOOST_AUTO_TEST_CASE(a_pose_margin_grows_every_side_by_that_fraction_of_the_size)
{
	auto plain = load("test_figure.glb"), margined = load("test_figure.glb", 0.25f);
	for (size_t i = 0; i < plain->meshes().size(); ++i)
	{
		rove::aabb<3> const a = static_cast<Mesh const &>(*plain->meshes()[i]).pose_bounds();
		rove::aabb<3> const b = static_cast<Mesh const &>(*margined->meshes()[i]).pose_bounds();
		for (int axis = 0; axis < 3; ++axis)
		{
			float const size = a.hi.i[axis] - a.lo.i[axis];
			BOOST_CHECK_SMALL(a.lo.i[axis] - 0.25f * size - b.lo.i[axis], 1e-5f);
			BOOST_CHECK_SMALL(a.hi.i[axis] + 0.25f * size - b.hi.i[axis], 1e-5f);
		}
	}
}

BOOST_AUTO_TEST_CASE(a_negative_pose_margin_is_refused)
{
	BOOST_CHECK_THROW(load("test_figure.glb", -0.1f), std::invalid_argument);
}

/// The model's joints are as they were: working the box out poses them, and leaves them in the pose the glTF gave.
BOOST_AUTO_TEST_CASE(working_the_box_out_leaves_the_joints_where_they_were)
{
	auto model = load("test_figure.glb");
	auto again = load("test_figure.glb");
	for (size_t j = 0; j < model->joints().size(); ++j)
	{
		BOOST_REQUIRE_EQUAL(model->joints()[j]->transforms.size(), again->joints()[j]->transforms.size());
		for (size_t t = 0; t < model->joints()[j]->transforms.size(); ++t)
			for (int c = 0; c < 4; ++c)
				for (int r = 0; r < 4; ++r)
					BOOST_REQUIRE_EQUAL(model->joints()[j]->transforms[t].matrix.ij[c][r], again->joints()[j]->transforms[t].matrix.ij[c][r]);
	}
	// and the pose it was in is the bind pose: a mesh skinned now is inside the box
	for (Mesh const *mesh : skinned(*model))
		BOOST_CHECK_SMALL(overshoot(*mesh, *model), 1e-5f);
}

BOOST_AUTO_TEST_CASE(the_culling_box_is_the_model_space_box_through_the_world_matrix)
{
	auto model = load("test_figure.glb");
	Mesh const &mesh = *skinned(*model).front();
	rove::matrix<4, 4> world;
	world.identity();
	world.rotation(rove::vec<3>(0, 1, 0), 0.7f);
	world.translate(rove::vec<3>(5, -2, 9));

	rove::obb<3> expected(mesh.pose_bounds());
	expected.transform(world);
	rove::obb<3> const got = mesh.conservative_bounds(model->joints(), *model->joints().front(), world);
	BOOST_CHECK_SMALL((got.origin - expected.origin).length(), 1e-4f);
	BOOST_CHECK_SMALL((got.tangent - expected.tangent).length(), 1e-4f);
	BOOST_CHECK_SMALL((got.normal - expected.normal).length(), 1e-4f);
	BOOST_CHECK_SMALL((got.binormal - expected.binormal).length(), 1e-4f);
}

/// A mesh whose box was never worked out has none: asking for it is an error, not an empty or unit box.
BOOST_AUTO_TEST_CASE(a_mesh_without_a_computed_box_refuses_to_give_one)
{
	auto model = std::make_shared<Model>(renderer, scheduler);
	edge::MeshPtr const mesh = model->add_mesh();
	auto const *skinned_mesh = dynamic_cast<Mesh const *>(mesh.get());
	BOOST_REQUIRE(skinned_mesh);
	BOOST_CHECK_THROW(skinned_mesh->pose_bounds(), std::logic_error);
}

BOOST_AUTO_TEST_CASE(an_instance_has_the_box_of_its_prototype)
{
	edge::ModelCache cache(renderer);
	auto a = cache.load<Model>(edge::TestFixture::graphic_sources_path() + "test/data/test_figure.glb", scheduler);
	auto b = cache.load<Model>(edge::TestFixture::graphic_sources_path() + "test/data/test_figure.glb", scheduler);
	rove::aabb<3> const ba = skinned(*a).front()->pose_bounds(), bb = skinned(*b).front()->pose_bounds();
	BOOST_CHECK_SMALL((ba.lo - bb.lo).length() + (ba.hi - bb.hi).length(), 1e-6f);
}

BOOST_AUTO_TEST_SUITE_END()
