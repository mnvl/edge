// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of the pose cache of skinned meshes (AnimatedMeshCache, Model::cached_pose): instances of one model from a ModelCache
// share what a mesh has in a pose -- its palette and its bounds, keyed by (clip, floor(time * 100)) -- so a pose is sampled once for all
// of them; the pose a cached instance has is the clip at the key's time (a hundredth of a second), whatever time asked for it; its joints
// are only built when somebody reads them; a model posed by anything else (a clip set directly, a mix, a hand-written joint) is out of the
// cache for good; a model that was not copied from another shares nothing.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "animated_mesh.h"
#include "animated_mesh_cache.h"
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

std::vector<mesh_type *> skinned(model_type &model)
{
	std::vector<mesh_type *> out;
	for (edge::MeshPtr const &m : model.meshes())
		if (mesh_type *s = dynamic_cast<mesh_type *>(m.get()); s && !s->joint_index_transform().empty())
			out.push_back(s);
	BOOST_REQUIRE(!out.empty());
	return out;
}

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

bool same(rove::matrix<4,4> const &a, rove::matrix<4,4> const &b)
{
	return same(palette{a}, palette{b});
}

/// A model of its own, posed the plain way at the hundredth @p tick of clip @p clip: what a cached pose must be.
struct Reference
{
	std::shared_ptr<model_type> model;

	Reference(Rig const &rig, std::size_t clip, int tick): model(rig.instance())
	{
		model->animations().at(clip)->pose(float(tick) * 0.01f);
	}
};

}

BOOST_AUTO_TEST_SUITE(animated_mesh_cache)

BOOST_AUTO_TEST_CASE(a_cached_pose_is_the_clip_at_the_hundredth_the_time_falls_in)
{
	Rig rig;
	auto m = rig.instance();
	// (times far from a hundredth's edge: which side of it a float lands on is not what is asked here)
	for (std::size_t clip = 0; clip < m->animations().size(); ++clip)
		for (int tick : {0, 1, 17, 36, 99})
			for (float offset : {0.001f, 0.0045f, 0.0085f})
			{
				float const time = float(tick) * 0.01f + offset;
				if (time > float(m->animations()[clip]->duration()))
					continue;
				m->set_cached_pose(clip, time);
				Reference const ref(rig, clip, tick);
				std::vector<mesh_type *> const got = skinned(*m), want = skinned(*ref.model);
				for (std::size_t k = 0; k < got.size(); ++k)
					BOOST_REQUIRE_MESSAGE(same(got[k]->skin_matrices(), fresh(*ref.model, *want[k])),
						"clip " << clip << " tick " << tick << " offset " << offset);
			}
}

BOOST_AUTO_TEST_CASE(the_pose_moves_from_one_hundredth_to_the_next)
{
	Rig rig;
	auto m = rig.instance();
	mesh_type *mesh = skinned(*m)[0];
	std::vector<palette> seen;
	for (int tick = 10; tick < 60; ++tick)
	{
		m->set_cached_pose(0, float(tick) * 0.01f + 0.005f);
		seen.push_back(mesh->skin_matrices());
	}
	std::size_t same_as_next = 0;
	for (std::size_t i = 1; i < seen.size(); ++i)
		same_as_next += same(seen[i], seen[i - 1]);
	BOOST_TEST(same_as_next < 5u);      // a clip that moves
	m->set_cached_pose(0, 0.505f);
	BOOST_TEST(same(mesh->skin_matrices(), seen[40]));      // and a key asked again is the same pose
}

BOOST_AUTO_TEST_CASE(instances_of_a_model_sample_a_pose_once)
{
	Rig rig;
	auto a = rig.instance(), b = rig.instance();
	std::vector<mesh_type *> const ma = skinned(*a), mb = skinned(*b);
	a->set_cached_pose(0, 0.503f);
	for (mesh_type *m : ma)
		m->skin_matrices();      // the miss: a poses its joints
	std::uint64_t const before = b->pose_serial();
	b->set_cached_pose(0, 0.508f);      // the same hundredth
	for (std::size_t k = 0; k < mb.size(); ++k)
		BOOST_TEST(same(mb[k]->skin_matrices(), ma[k]->skin_matrices()));
	BOOST_TEST(b->pose_serial() == before);      // b never sampled nor built its joints
	Reference const ref(rig, 0, 50);
	std::vector<mesh_type *> const mr = skinned(*ref.model);
	for (std::size_t k = 0; k < mb.size(); ++k)
		BOOST_TEST(same(mb[k]->skin_matrices(), fresh(*ref.model, *mr[k])));
}

BOOST_AUTO_TEST_CASE(other_hundredths_and_other_clips_are_other_poses)
{
	Rig rig;
	BOOST_REQUIRE_GE(rig.model->animations().size(), 2u);
	auto a = rig.instance(), b = rig.instance();
	mesh_type *ma = skinned(*a)[0], *mb = skinned(*b)[0];
	a->set_cached_pose(0, 0.30f);
	palette const first = ma->skin_matrices();
	b->set_cached_pose(0, 0.31f);
	BOOST_TEST(!same(mb->skin_matrices(), first));
	b->set_cached_pose(1, 0.30f);
	BOOST_TEST(!same(mb->skin_matrices(), first));
	Reference const ref(rig, 1, 30);
	BOOST_TEST(same(mb->skin_matrices(), fresh(*ref.model, *skinned(*ref.model)[0])));
	a->set_cached_pose(0, 0.30f);
	BOOST_TEST(same(ma->skin_matrices(), first));      // and a's own is still there
}

BOOST_AUTO_TEST_CASE(only_copies_of_one_model_share_a_cache)
{
	Rig rig, other;
	auto a = rig.instance(), b = rig.instance(), c = other.instance();
	std::vector<mesh_type *> const ma = skinned(*a), mb = skinned(*b), mc = skinned(*c), mp = skinned(*rig.model);
	for (std::size_t k = 0; k < ma.size(); ++k)
	{
		BOOST_REQUIRE(ma[k]->pose_cache());
		BOOST_TEST(ma[k]->pose_cache() == mb[k]->pose_cache());
		BOOST_TEST(ma[k]->pose_cache() != mc[k]->pose_cache());
		BOOST_TEST(ma[k]->pose_cache() == mp[k]->pose_cache());      // (the prototype's, made when it was first copied)
	}
	// two meshes of one model do not share
	if (ma.size() > 1)
		BOOST_TEST(ma[0]->pose_cache() != ma[1]->pose_cache());
	// a model loaded on its own makes the cache of its meshes when it needs it
	Rig solo;
	auto meshes = skinned(*solo.model);
	solo.model->set_cached_pose(0, 0.2f);
	BOOST_TEST(!meshes[0]->skin_matrices().empty());
	BOOST_TEST(meshes[0]->pose_cache());
	BOOST_TEST(meshes[0]->pose_cache() != ma[0]->pose_cache());
}

BOOST_AUTO_TEST_CASE(a_model_posed_by_hand_leaves_the_cache_for_good)
{
	Rig rig;
	auto m = rig.instance();
	BOOST_TEST(m->can_pose_cached());
	m->set_cached_pose(0, 0.5f);
	mesh_type *mesh = skinned(*m)[0];
	mesh->skin_matrices();
	m->animations()[0]->pose(0.9f);      // a clip set directly
	BOOST_TEST(!m->cached_pose().has_value());
	BOOST_TEST(same(mesh->skin_matrices(), fresh(*m, *mesh)));      // the pose of the joints, not the key's
	BOOST_TEST(!m->can_pose_cached());
	BOOST_CHECK_THROW(m->set_cached_pose(0, 0.5f), std::logic_error);
	// and its siblings are not touched by it
	auto n = rig.instance();
	BOOST_TEST(n->can_pose_cached());
}

BOOST_AUTO_TEST_CASE(a_rebuild_with_a_cached_pose_pending_builds_the_pose_first)
{
	Rig rig;
	auto a = rig.instance(), b = rig.instance();
	a->set_cached_pose(0, 0.405f);
	skinned(*a)[0]->skin_matrices();
	b->set_cached_pose(0, 0.405f);      // a hit later: b's joints were never built
	b->ensure_joints();
	Reference const ref(rig, 0, 40);
	for (std::size_t j = 0; j < b->joints().size(); ++j)
		BOOST_TEST(same(b->joints()[j]->matrix, ref.model->joints()[j]->matrix));
	BOOST_TEST(b->can_pose_cached());      // reading the joints leaves the model cached
	BOOST_TEST(b->cached_pose().has_value());
}

BOOST_AUTO_TEST_CASE(whoever_reads_the_joints_of_a_hit_sees_the_pose)
{
	Rig rig;
	auto a = rig.instance(), b = rig.instance();
	a->set_cached_pose(0, 0.625f);
	for (mesh_type *m : skinned(*a))
		m->skin_matrices();
	b->set_cached_pose(0, 0.625f);
	skinned(*b)[0]->skin_matrices();      // a hit: b's joints are not built
	Reference const ref(rig, 0, 62);
	std::uint64_t const serial = b->pose_serial();
	// the named lookup, the bounds and the collision octree all read the joints
	std::string const name = ref.model->joints()[3]->name;
	BOOST_TEST(same(b->joint_by_name(name)->matrix, ref.model->joint_by_name(name)->matrix));
	BOOST_TEST(b->pose_serial() != serial);      // built now
	rove::matrix<4,4> world;
	world.identity();
	rove::aabb<3> const got = b->bounds(world), want = ref.model->bounds(world);
	for (int i = 0; i < 3; ++i)
	{
		BOOST_TEST(got.lo.i[i] == want.lo.i[i]);
		BOOST_TEST(got.hi.i[i] == want.hi.i[i]);
	}
	auto c = rig.instance();
	c->set_cached_pose(0, 0.625f);
	skinned(*c)[0]->skin_matrices();
	BOOST_TEST(c->build_octree(world).bounds().lo.i[1] == ref.model->build_octree(world).bounds().lo.i[1]);
	// and the palette of the mesh is the same pose all the while
	std::vector<mesh_type *> const mb = skinned(*b), mr = skinned(*ref.model);
	for (std::size_t k = 0; k < mb.size(); ++k)
		BOOST_TEST(same(mb[k]->skin_matrices(), fresh(*ref.model, *mr[k])));
}

BOOST_AUTO_TEST_CASE(a_time_or_a_clip_that_does_not_exist_is_refused)
{
	Rig rig;
	auto m = rig.instance();
	float const length = float(m->animations()[0]->duration());
	BOOST_CHECK_THROW(m->set_cached_pose(0, -0.001f), std::invalid_argument);
	BOOST_CHECK_THROW(m->set_cached_pose(0, length + 0.5f), std::invalid_argument);
	BOOST_CHECK_THROW(m->set_cached_pose(0, std::nanf("")), std::invalid_argument);
	BOOST_CHECK_THROW(m->set_cached_pose(m->animations().size(), 0.1f), std::invalid_argument);
	BOOST_TEST(!m->cached_pose().has_value());      // none of them left a pose behind
	m->set_cached_pose(0, length);      // the last instant is a pose
	BOOST_TEST(m->cached_pose().has_value());
}

BOOST_AUTO_TEST_CASE(the_bounds_of_a_cached_pose_hold_the_skinned_mesh_and_follow_the_pose)
{
	Rig rig;
	auto m = rig.instance();
	mesh_type *mesh = skinned(*m)[0];
	rove::aabb<3> union_of_poses;
	union_of_poses.null();
	std::vector<rove::aabb<3>> boxes;
	for (int tick = 0; tick < 100; tick += 7)
	{
		m->set_cached_pose(0, float(tick) * 0.01f + 0.005f);
		rove::aabb<3> const box = mesh->posed_bounds();
		Reference const ref(rig, 0, tick);
		for (auto const &v : skinned(*ref.model)[0]->skinned_vertices(ref.model->joints()))
			for (int i = 0; i < 3; ++i)
			{
				BOOST_REQUIRE_MESSAGE(v.position.i[i] >= box.lo.i[i] - 1e-4f && v.position.i[i] <= box.hi.i[i] + 1e-4f,
					"tick " << tick << " axis " << i << ": " << v.position.i[i] << " outside " << box.lo.i[i] << ".." << box.hi.i[i]);
			}
		auto volume = [](rove::aabb<3> const &b) { return (b.hi.i[0] - b.lo.i[0]) * (b.hi.i[1] - b.lo.i[1]) * (b.hi.i[2] - b.lo.i[2]); };
		BOOST_TEST_MESSAGE("tick " << tick << ": posed box volume " << volume(box) << ", every pose's " << volume(mesh->pose_bounds()));
		boxes.push_back(box);
	}
	bool moved = false;
	for (std::size_t i = 1; i < boxes.size(); ++i)
		for (int a = 0; a < 3; ++a)
			moved = moved || boxes[i].lo.i[a] != boxes[0].lo.i[a] || boxes[i].hi.i[a] != boxes[0].hi.i[a];
	BOOST_TEST(moved);
}

BOOST_AUTO_TEST_CASE(culling_uses_the_box_of_the_pose_a_model_is_in_cached_or_by_hand_and_the_box_of_every_pose_before_it_is_posed)
{
	Rig rig;
	auto m = rig.instance();
	mesh_type *mesh = skinned(*m)[0];
	rove::matrix<4,4> world;
	world.identity();
	world.ij[3][0] = 3;
	edge::Joint const &owner = *m->joints()[0];
	// no cached pose: the box that holds every pose
	rove::obb<3> every(mesh->pose_bounds());
	every.transform(world);
	rove::obb<3> const loose = mesh->conservative_bounds(m->joints(), owner, world);
	BOOST_TEST(loose.radius() == every.radius());
	// a cached one: the pose's own
	m->set_cached_pose(0, 0.335f);
	rove::obb<3> tight(mesh->posed_bounds());
	tight.transform(world);
	rove::obb<3> const got = mesh->conservative_bounds(m->joints(), owner, world);
	BOOST_TEST(got.radius() == tight.radius());
	BOOST_TEST(got.radius() <= loose.radius() + 1e-4f);
	// the model left the cache by hand: the box of the pose it was put in, its palette's (bounds_in)
	m->animations()[0]->pose(0.33f);
	rove::obb<3> hand(mesh->pose_cache()->bounds_in(mesh->skin_matrices()));
	hand.transform(world);
	rove::obb<3> const by_hand = mesh->conservative_bounds(m->joints(), owner, world);
	BOOST_TEST(by_hand.radius() == hand.radius());
	BOOST_TEST(by_hand.radius() < every.radius());
	// ... and of the next one it is put in
	m->animations()[0]->pose(0.9f);
	rove::obb<3> next(mesh->pose_cache()->bounds_in(mesh->skin_matrices()));
	next.transform(world);
	BOOST_TEST(mesh->conservative_bounds(m->joints(), owner, world).radius() == next.radius());
}

BOOST_AUTO_TEST_SUITE_END()
