// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <boost/test/unit_test.hpp>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>

#include "animated_model.h"
#include "camera.h"
#include "model_builder.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "static_model.h"
#include "test_fixture.h"

namespace
{

std::shared_ptr<edge::ModelBuilder> make_model()
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	return std::make_shared<edge::ModelBuilder>(renderer, scheduler);
}

std::string make_asset_path(std::string const &asset_name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + asset_name;
}

std::shared_ptr<edge::ModelBuilder> load_model(std::string const &asset_name)
{
	auto model = make_model();
	model->load_from_gltf(make_asset_path(asset_name));
	return model;
}

}

BOOST_AUTO_TEST_SUITE(model)

BOOST_AUTO_TEST_CASE(joint_by_id)
{
	auto model = load_model("test_gltf_loder_joint.glb");
	BOOST_REQUIRE(!model->joints().empty());

	std::string const &id = model->joints()[0]->id;
	edge::JointPtr found = model->joint_by_id(id);
	BOOST_CHECK_EQUAL(found.get(), model->joints()[0].get());
}

BOOST_AUTO_TEST_CASE(joint_by_name)
{
	auto model = load_model("test_gltf_loder_joint.glb");
	BOOST_REQUIRE(!model->joints().empty());

	std::string const &name = model->joints()[0]->name;
	if (!name.empty())
	{
		edge::JointPtr found = model->joint_by_name(name);
		BOOST_CHECK_EQUAL(found.get(), model->joints()[0].get());
	}
}

BOOST_AUTO_TEST_CASE(joint_by_id_throws_for_missing)
{
	auto model = load_model("test_gltf_loder_plane.glb");
	BOOST_CHECK_THROW(model->joint_by_id("nonexistent_id"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(mesh_by_id)
{
	auto model = load_model("test_gltf_loder_plane.glb");
	BOOST_REQUIRE(!model->meshes().empty());

	std::string const &id = model->meshes()[0]->id();
	edge::MeshPtr found = model->mesh_by_id(id);
	BOOST_CHECK_EQUAL(found.get(), model->meshes()[0].get());
}

BOOST_AUTO_TEST_CASE(mesh_by_name)
{
	auto model = load_model("test_gltf_loder_plane.glb");
	BOOST_REQUIRE(!model->meshes().empty());

	std::string const &name = model->meshes()[0]->name();
	if (!name.empty())
	{
		edge::MeshPtr found = model->mesh_by_name(name);
		BOOST_CHECK_EQUAL(found.get(), model->meshes()[0].get());
	}
}

BOOST_AUTO_TEST_CASE(material_by_name)
{
	auto model = load_model("test_gltf_loder_materials.glb");
	BOOST_REQUIRE(!model->materials().empty());

	edge::MaterialPtr found = model->material_by_name("lambert1");
	BOOST_CHECK_EQUAL(found->name(), "lambert1");
}

BOOST_AUTO_TEST_CASE(material_by_name_throws_for_missing)
{
	auto model = load_model("test_gltf_loder_plane.glb");
	BOOST_CHECK_THROW(model->material_by_name("nonexistent"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(set_always_visible_propagates_to_meshes)
{
	auto model = load_model("test_gltf_loder_plane.glb");
	// Should not throw
	BOOST_CHECK_NO_THROW(model->set_always_visible(true));
	BOOST_CHECK_NO_THROW(model->set_always_visible(false));
}

BOOST_AUTO_TEST_CASE(a_model_draws_into_the_scheduler_it_is_given)
{
	edge::RendererPtr const renderer(new edge::NullRenderer(320, 200));
	auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, std::make_shared<edge::Scheduler>(renderer));
	model->load_from_gltf(make_asset_path("test_gltf_loder_plane.glb"));
	auto camera = std::make_shared<edge::Camera>();
	camera->set_perspective_projection(1.0f, 1.0f, 0.1f, 100.0f);
	camera->look_at(rove::vec<3>(0, 0, 5), rove::vec<3>(0, 0, 0), rove::vec<3>(0, 1, 0));
	rove::matrix<4,4> world;
	world.identity();

	edge::SchedulerPtr const first = model->scheduler();
	auto const other = std::make_shared<edge::Scheduler>(renderer);
	model->set_scheduler(other);
	BOOST_CHECK(model->scheduler() == other);
	model->draw(camera, world, 0u);
	other->flush(false);
	std::size_t const drawn = other->visible_calls_count() + other->invisible_calls_count();
	BOOST_CHECK_GT(drawn, 0u);
	first->flush(false);
	BOOST_CHECK_EQUAL(first->visible_calls_count() + first->invisible_calls_count(), 0u);

	BOOST_CHECK_THROW(model->set_scheduler(nullptr), std::invalid_argument);
	edge::RendererPtr const elsewhere(new edge::NullRenderer(32, 32));
	BOOST_CHECK_THROW(model->set_scheduler(std::make_shared<edge::Scheduler>(elsewhere)), std::invalid_argument);
	BOOST_CHECK(model->scheduler() == other);
}

BOOST_AUTO_TEST_CASE(pose_propagates)
{
	auto model = load_model("test_gltf_loder_joint.glb");
	BOOST_REQUIRE(!model->animations().empty());

	// Get initial joint matrices
	model->pose(0.0f);
	rove::matrix<4,4> mat_at_0 = model->joints()[0]->matrix;

	// Advance time — matrices should change
	model->pose(1000.0f);
	rove::matrix<4,4> mat_at_end = model->joints()[0]->matrix;

	bool changed = false;
	for (std::size_t col = 0; col < 4 && !changed; ++col)
		for (std::size_t row = 0; row < 4 && !changed; ++row)
			if (std::abs(mat_at_0.ij[col][row] - mat_at_end.ij[col][row]) > 1e-4f)
				changed = true;

	BOOST_CHECK(changed);
}

BOOST_AUTO_TEST_CASE(add_joint)
{
	auto model = make_model();
	BOOST_CHECK(model->joints().empty());

	edge::JointPtr j = model->add_joint();
	BOOST_REQUIRE(j);
	BOOST_CHECK_EQUAL(model->joints().size(), 1u);
	BOOST_CHECK_EQUAL(j->index, 0u);
}

BOOST_AUTO_TEST_CASE(clear_empties_all)
{
	auto model = load_model("test_gltf_loder_joint.glb");
	BOOST_REQUIRE(!model->joints().empty());
	BOOST_REQUIRE(!model->meshes().empty());

	model->clear();
	BOOST_CHECK(model->joints().empty());
	BOOST_CHECK(model->meshes().empty());
	BOOST_CHECK(model->animations().empty());
	BOOST_CHECK(model->materials().empty());
}

BOOST_AUTO_TEST_CASE(build_octree_works_for_model_builder)
{
	// ModelBuilder meshes are now static, so build_octree should succeed
	auto model = load_model("test_gltf_loder_plane.glb");

	rove::matrix<4,4> identity;
	identity.identity();

	model->pose(0.0f);
	BOOST_CHECK_NO_THROW(model->build_octree(identity));
}

// ============================================================================
// bounds(world) tests
// ============================================================================

namespace {

rove::matrix<4,4> identity_matrix()
{
	rove::matrix<4,4> m;
	m.identity();
	return m;
}

void check_non_degenerate(rove::aabb<3> const &b)
{
	BOOST_CHECK_LE(b.lo.x, b.hi.x);
	BOOST_CHECK_LE(b.lo.y, b.hi.y);
	BOOST_CHECK_LE(b.lo.z, b.hi.z);
	float const extent = (b.hi.x - b.lo.x) + (b.hi.y - b.lo.y) + (b.hi.z - b.lo.z);
	BOOST_CHECK_GT(extent, 0.0f);
}

void check_close(rove::aabb<3> const &a, rove::aabb<3> const &b)
{
	BOOST_CHECK_SMALL(std::abs(a.lo.x - b.lo.x), 1e-3f);
	BOOST_CHECK_SMALL(std::abs(a.lo.y - b.lo.y), 1e-3f);
	BOOST_CHECK_SMALL(std::abs(a.lo.z - b.lo.z), 1e-3f);
	BOOST_CHECK_SMALL(std::abs(a.hi.x - b.hi.x), 1e-3f);
	BOOST_CHECK_SMALL(std::abs(a.hi.y - b.hi.y), 1e-3f);
	BOOST_CHECK_SMALL(std::abs(a.hi.z - b.hi.z), 1e-3f);
}

}  // namespace

/// Right after a gltf load -- no pose() -- the bounds are the
/// mesh's, not a degenerate box.
BOOST_AUTO_TEST_CASE(bounds_non_degenerate_after_gltf_load)
{
	auto model = load_model("test_gltf_loder_plane.glb");
	check_non_degenerate(model->bounds(identity_matrix()));
}

/// A binary save/load roundtrip keeps the bounds.
BOOST_AUTO_TEST_CASE(bounds_survive_a_file_roundtrip)
{
	auto source = load_model("test_gltf_loder_plane.glb");

	std::filesystem::path const tmp =
		std::filesystem::temp_directory_path() / "edge_bounds_roundtrip.bin";
	source->save_to_file(tmp.string());

	auto loaded = make_model();
	loaded->load_from_file(tmp.string());
	std::error_code ec;
	std::filesystem::remove(tmp, ec);

	rove::aabb<3> const lb = loaded->bounds(identity_matrix());
	check_non_degenerate(lb);
	check_close(source->bounds(identity_matrix()), lb);
}

/// A model with no meshes has no bounds: bounds() says so rather than
/// return an empty or made-up box.
BOOST_AUTO_TEST_CASE(bounds_of_a_cleared_model_throw)
{
	auto model = load_model("test_gltf_loder_plane.glb");
	check_non_degenerate(model->bounds(identity_matrix()));

	model->clear();
	BOOST_CHECK_THROW((void)model->bounds(identity_matrix()), std::runtime_error);
}

/// clear() then load_from_gltf() gives the bounds of the new geometry.
BOOST_AUTO_TEST_CASE(bounds_after_reload_following_clear)
{
	auto model = load_model("test_gltf_loder_plane.glb");
	rove::aabb<3> const before = model->bounds(identity_matrix());
	model->clear();

	model->load_from_gltf(make_asset_path("test_gltf_loder_plane.glb"));
	rove::aabb<3> const after = model->bounds(identity_matrix());
	check_non_degenerate(after);
	check_close(before, after);
}

/// A skinned plane (test_gltf_loder_joint.glb): the bounds are its skinned
/// vertices, a non-degenerate box.
BOOST_AUTO_TEST_CASE(bounds_valid_for_a_skinned_model)
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	auto model = std::make_shared<edge::AnimatedModelPTBNJWT>(renderer, scheduler);
	model->load_from_gltf(make_asset_path("test_gltf_loder_joint.glb"));
	model->pose(0.0f);
	check_non_degenerate(model->bounds(identity_matrix()));
}

/// An instance of a model shares its geometry and pose, so its bounds are
/// the source's.
BOOST_AUTO_TEST_CASE(instance_has_the_bounds_of_its_source)
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	auto source = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	source->load_from_gltf(make_asset_path("test_gltf_loder_plane.glb"));

	edge::StaticModelPTBNT copy(*source, scheduler);
	check_close(source->bounds(identity_matrix()), copy.bounds(identity_matrix()));
}

/// The multi-mesh materials asset: the bounds cover both meshes, the second
/// of which its node moves 2 along x.
BOOST_AUTO_TEST_CASE(bounds_cover_every_mesh_of_the_materials_asset)
{
	auto model = load_model("test_gltf_loder_materials.glb");
	rove::aabb<3> const b = model->bounds(identity_matrix());
	check_non_degenerate(b);
	BOOST_CHECK_GE(b.hi.x, 2.0f);
}

BOOST_AUTO_TEST_CASE(a_model_has_a_renderer_and_a_scheduler)
{
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	BOOST_CHECK_THROW(std::make_shared<edge::ModelBuilder>(edge::RendererPtr(), scheduler), std::invalid_argument);
	BOOST_CHECK_THROW(std::make_shared<edge::ModelBuilder>(renderer, edge::SchedulerPtr()), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Model-level frustum culling tests
//
// These use StaticModelPTBNT, which inherits Model::draw() without overriding
// it, so the model-level frustum guard added in this change is exercised.
// ============================================================================

namespace
{

/// Shared fixture that creates a NullRenderer + Scheduler pair, loads a plane
/// model as a StaticModelPTBNT, and provides helper cameras and world matrices
/// for frustum cull testing.
struct FrustumCullFixture
{
	edge::RendererPtr renderer;
	edge::SchedulerPtr scheduler;

	FrustumCullFixture()
	{
		renderer = std::make_shared<edge::NullRenderer>(320, 200);
		scheduler = std::make_shared<edge::Scheduler>(renderer);
	}

	/// Loads the plane test asset into a StaticModelPTBNT that uses the shared
	/// renderer/scheduler so draw calls end up in @c scheduler.
	std::shared_ptr<edge::StaticModelPTBNT> load_plane() const
	{
		auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
		model->load_from_gltf(make_asset_path("test_gltf_loder_plane.glb"));
		model->pose(0.0f);
		return model;
	}

	/// Camera at (0,0,5) looking toward the origin with a wide FOV and large
	/// far plane — a model at the origin is solidly inside this frustum.
	edge::CameraPtr camera_facing_model() const
	{
		edge::CameraPtr cam(new edge::Camera());
		cam->set_perspective_projection(1.5f, 1.0f, 0.01f, 1000.0f);
		cam->look_at(
			rove::vec<3>(0.0f, 0.0f, 5.0f),
			rove::vec<3>(0.0f, 0.0f, 0.0f),
			rove::vec<3>(0.0f, 1.0f, 0.0f));
		return cam;
	}

	/// Camera at (0,0,5000) looking further along +Z with a short far plane
	/// (100 units).  The frustum spans z ≈ [4999.9, 5100] — the origin is
	/// far outside.
	edge::CameraPtr camera_facing_away() const
	{
		edge::CameraPtr cam(new edge::Camera());
		cam->set_perspective_projection(1.0f, 1.0f, 0.1f, 100.0f);
		cam->look_at(
			rove::vec<3>(0.0f, 0.0f, 5000.0f),
			rove::vec<3>(0.0f, 0.0f, 5100.0f),
			rove::vec<3>(0.0f, 1.0f, 0.0f));
		return cam;
	}

	/// Identity world matrix — model stays at the origin.
	rove::matrix<4,4> identity_world() const
	{
		rove::matrix<4,4> m;
		m.identity();
		return m;
	}

	/// World matrix that translates the model 100 000 units along X.
	/// Even a wide-FOV camera centred near the origin cannot see this.
	/// Translation is stored in column 3 (column-major convention).
	rove::matrix<4,4> far_world() const
	{
		rove::matrix<4,4> m;
		m.identity();
		m.ij[3][0] = 100000.0f;
		return m;
	}

	/// Total draw calls that reached the scheduler in the last flush.
	std::size_t total_calls() const
	{
		return scheduler->visible_calls_count() +
		       scheduler->invisible_calls_count();
	}
};

}  // namespace

BOOST_AUTO_TEST_SUITE(model_frustum_cull)

/// When the model is at the origin and the camera is looking at it,
/// draw calls must be submitted and counted as visible.
BOOST_FIXTURE_TEST_CASE(draw_submits_draw_calls_when_model_in_frustum, FrustumCullFixture)
{
	auto model = load_plane();
	model->draw(camera_facing_model(), identity_world());
	edge::flush_in_a_pass(scheduler);

	BOOST_CHECK_GT(total_calls(), 0u);
	BOOST_CHECK_GT(scheduler->visible_calls_count(), 0u);
}

/// When the model is translated far outside the camera frustum, draw calls
/// are still submitted but the per-draw-call frustum test culls them.
BOOST_FIXTURE_TEST_CASE(draw_culls_draw_calls_when_model_outside_frustum, FrustumCullFixture)
{
	auto model = load_plane();
	model->draw(camera_facing_model(), far_world());
	edge::flush_in_a_pass(scheduler);

	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 0u);
	BOOST_CHECK_GT(scheduler->invisible_calls_count(), 0u);
}

/// With the camera pointing away from the origin, the model at the origin is
/// outside the frustum.  Draw calls are submitted but culled.
BOOST_FIXTURE_TEST_CASE(draw_culls_draw_calls_camera_facing_away, FrustumCullFixture)
{
	auto model = load_plane();
	model->draw(camera_facing_away(), identity_world());
	edge::flush_in_a_pass(scheduler);

	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 0u);
	BOOST_CHECK_GT(scheduler->invisible_calls_count(), 0u);
}

/// set_always_visible(true) propagates to per-draw-call level,
/// so draw calls are always counted as visible.
BOOST_FIXTURE_TEST_CASE(draw_bypasses_cull_when_always_visible, FrustumCullFixture)
{
	auto model = load_plane();
	model->set_always_visible(true);
	model->draw(camera_facing_model(), far_world());
	edge::flush_in_a_pass(scheduler);

	BOOST_CHECK_GT(scheduler->visible_calls_count(), 0u);
}

/// set_always_visible(true) bypasses per-draw-call cull even when the
/// camera is pointing away from the model.
BOOST_FIXTURE_TEST_CASE(always_visible_bypasses_cull_with_camera_facing_away, FrustumCullFixture)
{
	auto model = load_plane();
	model->set_always_visible(true);
	model->draw(camera_facing_away(), identity_world());
	edge::flush_in_a_pass(scheduler);

	BOOST_CHECK_GT(scheduler->visible_calls_count(), 0u);
}

/// After toggling always_visible back to false, per-draw-call frustum culling
/// is re-enabled.  A model positioned outside the frustum must be culled.
BOOST_FIXTURE_TEST_CASE(draw_re_enables_cull_after_set_always_visible_false, FrustumCullFixture)
{
	auto model = load_plane();
	model->set_always_visible(true);
	model->set_always_visible(false);
	model->draw(camera_facing_model(), far_world());
	edge::flush_in_a_pass(scheduler);

	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 0u);
	BOOST_CHECK_GT(scheduler->invisible_calls_count(), 0u);
}

/// clear() must reset always_visible_ to false.  After clear() + reload,
/// a model outside the frustum must be culled at the draw-call level.
BOOST_FIXTURE_TEST_CASE(clear_resets_always_visible_so_cull_applies_after_reload, FrustumCullFixture)
{
	auto model = load_plane();
	model->set_always_visible(true);
	model->clear();

	// Reload geometry — always_visible_ must remain false (set by clear()).
	model->load_from_gltf(make_asset_path("test_gltf_loder_plane.glb"));
	model->pose(0.0f);

	model->draw(camera_facing_model(), far_world());
	edge::flush_in_a_pass(scheduler);

	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 0u);
	BOOST_CHECK_GT(scheduler->invisible_calls_count(), 0u);
}

/// A second sequential draw() call with the same model (no flush in between)
/// must accumulate draw calls in the scheduler.
BOOST_FIXTURE_TEST_CASE(draw_called_twice_accumulates_calls, FrustumCullFixture)
{
	auto model = load_plane();
	model->draw(camera_facing_model(), identity_world());
	model->draw(camera_facing_model(), identity_world());
	edge::flush_in_a_pass(scheduler);

	// Two draws of the same model — each submitted its per-mesh draw calls.
	BOOST_CHECK_GT(total_calls(), 1u);
}

/// When the world matrix positions the model inside the frustum (identity),
/// draw calls are visible.  When drawn again with a far world matrix,
/// those draw calls are culled by the per-draw-call frustum test.
BOOST_FIXTURE_TEST_CASE(world_matrix_determines_cull_per_draw_call, FrustumCullFixture)
{
	auto model = load_plane();

	// First draw: model at origin — visible.
	model->draw(camera_facing_model(), identity_world());
	edge::flush_in_a_pass(scheduler);
	BOOST_REQUIRE_GT(scheduler->visible_calls_count(), 0u);

	// Second draw: same model translated far away — culled.
	model->draw(camera_facing_model(), far_world());
	edge::flush_in_a_pass(scheduler);
	BOOST_CHECK_EQUAL(scheduler->visible_calls_count(), 0u);
	BOOST_CHECK_GT(scheduler->invisible_calls_count(), 0u);
}

BOOST_AUTO_TEST_SUITE_END()
