// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of a skinned mesh's palette (BasicAnimatedMesh::skin_matrices, what draw() hands vertex_skin.glsl): it is always the pose the
// model's joints were last built into (Model::rebuild_joints), it is worked out once per pose however many times the mesh is drawn (a model
// drawn in the scene, three shadow cascades and the lamps costs one palette), and instances of one model keep their own.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "animated_mesh.h"
#include "animated_model.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "test_fixture.h"

namespace
{

using model_type = edge::AnimatedModelPTBNJWT;
using mesh_type = edge::AnimatedMeshPTBNJWT;
using palette = std::vector<rove::matrix<4, 4>>;

struct Rig
{
	edge::RendererPtr renderer;
	edge::SchedulerPtr scheduler;
	std::shared_ptr<model_type> model;

	Rig()
	{
		renderer = std::make_shared<edge::NullRenderer>(320, 200);
		scheduler = std::make_shared<edge::Scheduler>(renderer);
		model = std::make_shared<model_type>(renderer, scheduler);
		model->load_from_gltf(edge::TestFixture::graphic_sources_path() + "test/data/test_figure.glb");
		BOOST_REQUIRE(!model->animations().empty());
	}

	std::shared_ptr<model_type> instance() const
	{
		auto copy = std::make_shared<model_type>(static_cast<model_type const &>(*model), scheduler);
		copy->bind_animations();
		return copy;
	}
};

std::vector<mesh_type const *> skinned(model_type const &model)
{
	std::vector<mesh_type const *> out;
	for (edge::MeshPtr const &m : model.meshes())
		if (mesh_type const *s = dynamic_cast<mesh_type const *>(m.get()); s && !s->joint_index_transform().empty())
			out.push_back(s);
	BOOST_REQUIRE(!out.empty());
	return out;
}

/// The palette worked out from the joints as they are now.
palette fresh(model_type const &model, mesh_type const &mesh)
{
	palette out;
	for (std::size_t j : mesh.joint_index_transform())
		out.push_back(model.joints()[j]->matrix * mesh.inverted_bind_matrices()[j] * mesh.bind_shape_matrix());
	return out;
}

bool same(palette const &a, palette const &b)
{
	if (a.size() != b.size())
		return false;
	for (std::size_t i = 0; i < a.size(); ++i)
		for (std::size_t c = 0; c < 4; ++c)
			for (std::size_t r = 0; r < 4; ++r)
				if (a[i].ij[c][r] != b[i].ij[c][r])
					return false;
	return true;
}

}

BOOST_AUTO_TEST_SUITE(skin_palette)

BOOST_AUTO_TEST_CASE(the_palette_is_the_pose_the_joints_were_last_built_into)
{
	Rig rig;
	palette first;
	bool moved = false;
	for (float t : {0.0f, 0.3f, 0.7f, 0.3f, 1.1f})
	{
		rig.model->pose(t);
		for (mesh_type const *m : skinned(*rig.model))
		{
			BOOST_TEST(same(m->skin_matrices(), fresh(*rig.model, *m)));
			BOOST_TEST(same(m->skin_matrices(), fresh(*rig.model, *m)));      // asked again: the same
		}
		palette const now = skinned(*rig.model)[0]->skin_matrices();
		if (first.empty())
			first = now;
		moved = moved || !same(now, first);
	}
	BOOST_TEST(moved);      // the clip moves the joints: the poses differ
}

BOOST_AUTO_TEST_CASE(between_two_poses_the_palette_is_not_worked_out_again)
{
	Rig rig;
	rig.model->pose(0.4f);
	mesh_type const *m = skinned(*rig.model)[0];
	palette const posed = m->skin_matrices();
	// a joint written without rebuilding: the palette is the pose's still (it is not worked out at each draw) ...
	std::size_t const j = m->joint_index_transform()[0];
	rove::matrix<4,4> moved = rig.model->joints()[j]->matrix;
	moved.ij[3][0] += 5;
	rig.model->joints()[j]->matrix = moved;
	BOOST_TEST(same(m->skin_matrices(), posed));
	BOOST_TEST(!same(fresh(*rig.model, *m), posed));
	// ... and a rebuild makes it the new pose's
	rig.model->rebuild_joints();
	BOOST_TEST(same(m->skin_matrices(), fresh(*rig.model, *m)));
}

BOOST_AUTO_TEST_CASE(instances_of_one_model_keep_their_own_palettes)
{
	Rig rig;
	rig.model->pose(0.2f);
	for (mesh_type const *m : skinned(*rig.model))      // the prototype's palettes worked out (an instance's meshes are copies of these meshes)
		m->skin_matrices();
	std::uint64_t const proto = rig.model->pose_serial();
	auto a = rig.instance(), b = rig.instance();
	// an instance posed one rebuild at a time and drawn only once its pose number has reached the prototype's (loading alone rebuilds a
	// model hundreds of times): numbered by model, the copied meshes would take the prototype's palettes for its own
	for (int i = 1; i == 1 || (a->pose_serial() < proto && i < 100000); ++i)
		a->animations()[0]->pose(0.5f + 0.0001f * float(i));
	std::vector<mesh_type const *> const pa = skinned(*a), pp = skinned(*rig.model);
	BOOST_REQUIRE(!same(fresh(*a, *pa[0]), fresh(*rig.model, *pp[0])));      // its pose is not the prototype's
	for (auto k : pa)
		BOOST_TEST(same(k->skin_matrices(), fresh(*a, *k)));
	// and two instances posed side by side, drawn at every pose, never share
	for (int i = 1; i <= 50; ++i)
	{
		a->animations()[0]->pose(0.5f + 0.01f * float(i));
		b->animations()[0]->pose(0.9f + 0.01f * float(i));
		std::vector<mesh_type const *> const qa = skinned(*a), qb = skinned(*b);
		for (std::size_t k = 0; k < qa.size(); ++k)
		{
			BOOST_REQUIRE(same(qa[k]->skin_matrices(), fresh(*a, *qa[k])));
			BOOST_REQUIRE(same(qb[k]->skin_matrices(), fresh(*b, *qb[k])));
		}
		BOOST_REQUIRE(!same(qa[0]->skin_matrices(), qb[0]->skin_matrices()));
	}
	for (auto k : pp)
		BOOST_TEST(same(k->skin_matrices(), fresh(*rig.model, *k)));      // the prototype's untouched
}

/// An instance of a model shares its clips' keyframes with the model it was made from (a thousand monsters of one kind hold one copy of the
/// clips), and is posed on its own all the same; a key added to one copy of a clip is its own (the others keep theirs).
BOOST_AUTO_TEST_CASE(instances_share_the_keyframes_of_their_clips_and_pose_on_their_own)
{
	Rig rig;
	auto a = rig.instance(), b = rig.instance();
	BOOST_REQUIRE(!rig.model->animations().empty());
	std::size_t shared = 0;
	for (std::size_t c = 0; c < rig.model->animations().size(); ++c)
		for (std::size_t s = 0; s < rig.model->animations()[c]->samplers.size(); ++s)
		{
			edge::Sampler const &p = rig.model->animations()[c]->samplers[s];
			if (p.samples_count() == 0)
				continue;
			BOOST_REQUIRE_EQUAL(a->animations()[c]->samplers[s].samples_count(), p.samples_count());
			BOOST_TEST(&a->animations()[c]->samplers[s].sample(0) == &p.sample(0));
			BOOST_TEST(&b->animations()[c]->samplers[s].sample(0) == &p.sample(0));
			++shared;
		}
	BOOST_TEST(shared > 10u);
	// posed apart, they are apart
	a->animations()[0]->pose(0.2f);
	b->animations()[0]->pose(0.9f);
	mesh_type const *ma = skinned(*a)[0], *mb = skinned(*b)[0];
	BOOST_TEST(!same(ma->skin_matrices(), mb->skin_matrices()));
	// a key added to one copy of a clip is that copy's
	edge::Sampler copy = rig.model->animations()[0]->samplers[0];
	std::size_t const n = copy.samples_count();
	edge::Sampler::Key late = copy.sample(0);
	late.time = 1000.0f;
	copy.add_sample(late);
	BOOST_TEST(copy.samples_count() == n + 1);
	BOOST_TEST(rig.model->animations()[0]->samplers[0].samples_count() == n);
	BOOST_TEST(a->animations()[0]->samplers[0].samples_count() == n);
}

BOOST_AUTO_TEST_SUITE_END()
