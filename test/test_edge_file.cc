// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Contract of the .edge model file: a model loaded from a glTF file, saved and loaded again is the same model -- every field of
// its materials and their textures (size, format, sRGB, mip levels and every texel), its joints, its meshes (vertices, skin,
// morph targets, custom attributes, pose box), and its animations, which pose the joints exactly as the glTF's did at any time.  A file
// that is damaged, or of another version, is refused with an error rather than loaded as something else.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <cereal/archives/binary.hpp>

#include "animated_mesh.h"
#include "animated_model.h"
#include "camera.h"
#include "gltf_loader.h"
#include "material.h"
#include "model_cache.h"
#include "null_renderer.h"
#include "pipeline.h"
#include "scheduler.h"
#include "serialized_model.h"
#include "static_model.h"
#include "test_fixture.h"
#include "texture.h"

namespace
{

using Animated = edge::AnimatedModelPTBNJWT;

std::string asset(std::string const &name)
{
	return edge::TestFixture::graphic_sources_path() + "test/data/" + name;
}

struct Fixture
{
	edge::RendererPtr renderer = [] { edge::RendererPtr r(new edge::NullRenderer(320, 200)); return r; }();
	edge::SchedulerPtr scheduler = std::make_shared<edge::Scheduler>(renderer);
	std::filesystem::path const file = std::filesystem::temp_directory_path() / "edge_file_contract.edge";

	~Fixture()
	{
		std::error_code ignored;
		std::filesystem::remove(file, ignored);
	}

	template <class M>
	std::shared_ptr<M> from_gltf(std::string const &name, edge::GltfOptions const &options = edge::GltfOptions())
	{
		auto model = std::make_shared<M>(renderer, scheduler);
		model->load_from_gltf(asset(name), options);
		return model;
	}

	template <class M>
	std::shared_ptr<M> reloaded(M const &model)
	{
		model.save_to_file(file.string());
		auto loaded = std::make_shared<M>(renderer, scheduler);
		loaded->load_from_file(file.string());
		return loaded;
	}

	std::vector<char> bytes() const
	{
		std::ifstream in(file, std::ios::binary);
		return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}

	void write(std::vector<char> const &data) const
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out.write(data.data(), std::streamsize(data.size()));
	}

	/// The file as its structures, to be damaged in one place and written back.
	edge::SerializedModel serialized() const
	{
		std::ifstream in(file, std::ios::binary);
		cereal::BinaryInputArchive archive(in);
		edge::SerializedModel model;
		archive(model);
		return model;
	}

	void write(edge::SerializedModel const &model) const
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		cereal::BinaryOutputArchive archive(out);
		archive(model);
	}

	/// What a load of the file throws: a std::runtime_error that names @p expected.
	template <class M>
	void refused(std::string const &expected)
	{
		auto loaded = std::make_shared<M>(renderer, scheduler);
		try
		{
			loaded->load_from_file(file.string());
			BOOST_ERROR("the file was loaded; expected an error about " << expected);
		}
		catch (std::runtime_error const &e)
		{
			BOOST_CHECK_MESSAGE(std::string(e.what()).find(expected) != std::string::npos, e.what());
		}
	}

	/// The size of the file of a model of one mesh of @p count vertices.
	template <class M, class V>
	std::uintmax_t file_size_with(size_t count)
	{
		auto model = std::make_shared<M>(renderer, scheduler);
		edge::MeshPtr const mesh = model->add_mesh();
		std::vector<edge::GenericVertex> vertices(count);
		std::memset(vertices.data(), 0, vertices.size() * sizeof(edge::GenericVertex));
		for (size_t i = 0; i < count; ++i)
			vertices[i].position = rove::vec<3>(float(i), 0, 0);
		mesh->set_vertices(vertices.data(), V::FORMAT, count);
		unsigned indices[] = {0, 1, 2};
		mesh->set_indices(indices, 3);
		if constexpr (std::is_same_v<M, Animated>)
			static_cast<edge::AnimatedMeshPTBNJWT &>(*mesh).set_pose_bounds(rove::aabb<3>(rove::vec<3>(0, 0, 0), rove::vec<3>(1, 1, 1)));
		model->save_to_file(file.string());
		return std::filesystem::file_size(file);
	}
};

/// The mesh's vertices, indices, morph targets and weights, pose box and flags as the file has them.
edge::SerializedMesh geometry(edge::Mesh const &mesh)
{
	edge::SerializedMesh serialized;
	mesh.save_geometry(serialized);
	return serialized;
}

bool same_vertices(std::vector<edge::GenericVertex> const &a, std::vector<edge::GenericVertex> const &b)
{
	return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(edge::GenericVertex)) == 0;
}

void same_geometry(edge::Mesh const &a, edge::Mesh const &b, std::string const &what)
{
	edge::SerializedMesh const ga = geometry(a), gb = geometry(b);
	BOOST_CHECK_MESSAGE(same_vertices(ga.vertices, gb.vertices), what << ": vertices");
	BOOST_CHECK_MESSAGE(ga.indices == gb.indices, what << ": indices");
	BOOST_REQUIRE_EQUAL(ga.morph_target_deltas.size(), gb.morph_target_deltas.size());
	for (size_t t = 0; t < ga.morph_target_deltas.size(); ++t)
		BOOST_CHECK_MESSAGE(same_vertices(ga.morph_target_deltas[t], gb.morph_target_deltas[t]), what << ": morph target " << t);
	BOOST_CHECK_MESSAGE(ga.morph_weights == gb.morph_weights, what << ": morph weights");
	BOOST_CHECK_MESSAGE(ga.always_visible == gb.always_visible, what << ": always visible");
	BOOST_REQUIRE_MESSAGE(bool(ga.pose_bounds) == bool(gb.pose_bounds), what << ": pose box");
	if (ga.pose_bounds)
		BOOST_CHECK_MESSAGE(ga.pose_bounds->lo == gb.pose_bounds->lo && ga.pose_bounds->hi == gb.pose_bounds->hi, what << ": pose box");
}

void same_matrix(rove::matrix<4,4> const &a, rove::matrix<4,4> const &b, float tolerance, std::string const &what)
{
	for (int c = 0; c < 4; ++c)
		for (int r = 0; r < 4; ++r)
			BOOST_REQUIRE_MESSAGE(std::abs(a.ij[c][r] - b.ij[c][r]) <= tolerance, what << " [" << c << "][" << r << "]: " << a.ij[c][r] << " vs " << b.ij[c][r]);
}

void same_texture(edge::TexturePtr const &a, edge::TexturePtr const &b, std::string const &what)
{
	BOOST_REQUIRE_MESSAGE(bool(a) == bool(b), what << ": one has a texture, the other not");
	if (!a)
		return;
	BOOST_CHECK_MESSAGE(a->width() == b->width() && a->height() == b->height(), what << ": size");
	BOOST_CHECK_MESSAGE(a->pixel_format() == b->pixel_format(), what << ": pixel format");
	BOOST_CHECK_MESSAGE(a->is_srgb() == b->is_srgb(), what << ": sRGB");
	BOOST_CHECK_MESSAGE(a->usage().mipmaps == b->usage().mipmaps, what << ": mipmaps");
	BOOST_CHECK_MESSAGE(a->usage().provided_mip_levels == b->usage().provided_mip_levels, what << ": provided mip levels");
	BOOST_CHECK_MESSAGE(a->mip_levels() == b->mip_levels(), what << ": mip levels");
	BOOST_REQUIRE_MESSAGE(a->pixel_format() == b->pixel_format() && a->width() == b->width() && a->height() == b->height(), what);
	size_t const size = edge::Texture::levels_size(a->width(), a->height(), a->pixel_format(), std::max(1, a->usage().provided_mip_levels));
	auto const ta = std::as_const(*a).read(), tb = std::as_const(*b).read();
	BOOST_REQUIRE(ta.data() && tb.data());
	BOOST_CHECK_MESSAGE(std::memcmp(ta.data(), tb.data(), size) == 0, what << ": texels");
}

void same_material(edge::Material const &a, edge::Material const &b, std::string const &what)
{
	BOOST_CHECK_EQUAL(a.name(), b.name());
	BOOST_CHECK_MESSAGE(a.cull_mode() == b.cull_mode(), what << ": cull mode");
	BOOST_CHECK_MESSAGE(a.zwrite_enabled() == b.zwrite_enabled(), what << ": zwrite");
	BOOST_CHECK_MESSAGE(a.alpha_blend_enabled() == b.alpha_blend_enabled(), what << ": alpha blend");
	BOOST_CHECK_MESSAGE(a.preserve_destination_alpha() == b.preserve_destination_alpha(), what << ": preserve destination alpha");
	BOOST_CHECK_MESSAGE(a.src_blend_mode() == b.src_blend_mode() && a.dest_blend_mode() == b.dest_blend_mode(), what << ": blend modes");
	BOOST_CHECK_MESSAGE(a.metallic_factor() == b.metallic_factor(), what << ": metallic");
	BOOST_CHECK_MESSAGE(a.roughness_factor() == b.roughness_factor(), what << ": roughness");
	BOOST_CHECK_MESSAGE(a.alpha_cutoff() == b.alpha_cutoff(), what << ": alpha cutoff");
	BOOST_CHECK_MESSAGE((a.emissive_factor() - b.emissive_factor()).length() == 0, what << ": emissive factor");
	BOOST_CHECK_MESSAGE((a.base_color_factor() - b.base_color_factor()).length() == 0, what << ": base colour factor");
	for (size_t i = 0; i < edge::Material::EXTRA_PARAMETERS_COUNT; ++i)
		BOOST_CHECK_MESSAGE((a.extra_parameter(i) - b.extra_parameter(i)).length() == 0, what << ": extra parameter " << i);
	BOOST_REQUIRE_EQUAL(a.diffuse_textures_count(), b.diffuse_textures_count());
	for (size_t i = 0; i < a.diffuse_textures_count(); ++i)
		same_texture(a.diffuse_texture(i), b.diffuse_texture(i), what + " diffuse " + std::to_string(i));
	same_texture(a.normal_map_texture(), b.normal_map_texture(), what + " normal map");
	same_texture(a.texture_blending_weights_texture(), b.texture_blending_weights_texture(), what + " blend weights");
	same_texture(a.metallic_roughness_texture(), b.metallic_roughness_texture(), what + " metallic-roughness");
	same_texture(a.emissive_texture(), b.emissive_texture(), what + " emissive");
}

/// Everything but the animations' behaviour.
void same_model(edge::Model const &a, edge::Model const &b)
{
	BOOST_REQUIRE_EQUAL(a.materials().size(), b.materials().size());
	for (size_t i = 0; i < a.materials().size(); ++i)
		same_material(*a.materials()[i], *b.materials()[i], "material " + std::to_string(i));

	BOOST_REQUIRE_EQUAL(a.joints().size(), b.joints().size());
	for (size_t i = 0; i < a.joints().size(); ++i)
	{
		edge::Joint const &ja = *a.joints()[i], &jb = *b.joints()[i];
		std::string const what = "joint " + std::to_string(i) + " '" + ja.name + "'";
		BOOST_CHECK_MESSAGE(ja.index == jb.index && ja.id == jb.id && ja.name == jb.name, what);
		same_matrix(ja.matrix, jb.matrix, 0, what + " matrix");
		BOOST_REQUIRE_EQUAL(ja.transforms.size(), jb.transforms.size());
		for (size_t t = 0; t < ja.transforms.size(); ++t)
		{
			BOOST_CHECK_EQUAL(ja.transforms[t].sid, jb.transforms[t].sid);
			same_matrix(ja.transforms[t].matrix, jb.transforms[t].matrix, 0, what + " transform");
		}
		auto const position = [](edge::Model const &m, edge::JointPtr const &j) -> long {
			if (!j)
				return -1;
			for (size_t k = 0; k < m.joints().size(); ++k)
				if (m.joints()[k] == j)
					return long(k);
			return -2;
		};
		BOOST_CHECK_MESSAGE(position(a, ja.parent.lock()) == position(b, jb.parent.lock()), what << ": parent");
		BOOST_REQUIRE_EQUAL(ja.children.size(), jb.children.size());
		for (size_t c = 0; c < ja.children.size(); ++c)
			BOOST_CHECK_MESSAGE(position(a, ja.children[c].lock()) == position(b, jb.children[c].lock()), what << ": child " << c);
		auto const mesh_position = [](edge::Model const &m, edge::MeshPtr const &mesh) -> long {
			for (size_t k = 0; k < m.meshes().size(); ++k)
				if (m.meshes()[k] == mesh)
					return long(k);
			return -1;
		};
		BOOST_CHECK_MESSAGE(mesh_position(a, ja.mesh.lock()) == mesh_position(b, jb.mesh.lock()), what << ": mesh");
	}

	BOOST_REQUIRE_EQUAL(a.meshes().size(), b.meshes().size());
	for (size_t i = 0; i < a.meshes().size(); ++i)
	{
		edge::Mesh const &ma = *a.meshes()[i], &mb = *b.meshes()[i];
		std::string const what = "mesh " + std::to_string(i) + " '" + ma.name() + "'";
		BOOST_CHECK_MESSAGE(ma.id() == mb.id() && ma.name() == mb.name(), what);
		BOOST_CHECK_MESSAGE(ma.serialization_type() == mb.serialization_type(), what << ": type");
		same_matrix(ma.bind_shape_matrix(), mb.bind_shape_matrix(), 0, what + " bind shape");
		BOOST_REQUIRE_EQUAL(ma.inverted_bind_matrices().size(), mb.inverted_bind_matrices().size());
		for (size_t k = 0; k < ma.inverted_bind_matrices().size(); ++k)
			same_matrix(ma.inverted_bind_matrices()[k], mb.inverted_bind_matrices()[k], 0, what + " inverse bind " + std::to_string(k));
		BOOST_CHECK(ma.joint_index_transform() == mb.joint_index_transform());
		BOOST_REQUIRE_EQUAL(ma.custom_attributes().size(), mb.custom_attributes().size());
		for (auto const &entry : ma.custom_attributes())
		{
			auto const found = mb.custom_attributes().find(entry.first);
			BOOST_REQUIRE_MESSAGE(found != mb.custom_attributes().end(), what << ": custom attribute " << entry.first);
			BOOST_CHECK_MESSAGE(found->second.components == entry.second.components && found->second.values == entry.second.values,
				what << ": custom attribute " << entry.first);
		}
		BOOST_REQUIRE_EQUAL(ma.triangles().size(), mb.triangles().size());
		for (size_t k = 0; k < ma.triangles().size(); ++k)
		{
			auto const &ta = ma.triangles()[k], &tb = mb.triangles()[k];
			BOOST_CHECK(ta.minimal_vertex_index == tb.minimal_vertex_index && ta.vertices_count == tb.vertices_count
				&& ta.first_index_offset == tb.first_index_offset && ta.triangles_count == tb.triangles_count);
			auto const material = [](edge::Model const &m, edge::MaterialPtr const &p) -> long {
				for (size_t q = 0; q < m.materials().size(); ++q)
					if (m.materials()[q] == p)
						return long(q);
				return -1;
			};
			BOOST_CHECK_MESSAGE(material(a, ta.material_ptr) == material(b, tb.material_ptr), what << ": material of triangles " << k);
		}
		same_geometry(ma, mb, what);
	}
}

/// The two models' joints, posed by the same clip at the same times, are where they are.
void same_animations(edge::Model &a, edge::Model &b)
{
	BOOST_REQUIRE_EQUAL(a.animations().size(), b.animations().size());
	for (size_t i = 0; i < a.animations().size(); ++i)
	{
		edge::Animation &x = *a.animations()[i], &y = *b.animations()[i];
		BOOST_CHECK_EQUAL(x.name, y.name);
		BOOST_REQUIRE_EQUAL(x.samplers.size(), y.samplers.size());
		BOOST_REQUIRE_EQUAL(x.weight_samplers.size(), y.weight_samplers.size());
		BOOST_CHECK_EQUAL(x.duration(), y.duration());
		float const length = float(x.duration());
		for (int step = -1; step <= 20; ++step)
		{
			float const t = step < 0 ? -1.0f : length * float(step) / 19.0f + (step == 20 ? 5.0f : 0.0f);      // before the start, along the clip, past its end
			x.pose(t);
			y.pose(t);
			for (size_t j = 0; j < a.joints().size(); ++j)
				same_matrix(a.joints()[j]->matrix, b.joints()[j]->matrix, 0, "clip '" + x.name + "' t=" + std::to_string(t) + " joint " + a.joints()[j]->name);
		}
	}
}

}

BOOST_FIXTURE_TEST_SUITE(edge_file, Fixture)

/// Every glTF of the tests that loads, of each kind ModelCache loads: loaded from the glTF and loaded from the .edge file it saves, the
/// model is the same, as the file it saves again says byte for byte (serialize() is all a model keeps).  The one model two loaders make.
template <class M> void the_same_from_either_file(Fixture &f, std::string const &name)
{
	edge::GltfOptions keep;
	keep.compress_textures = false;
	auto const direct = f.from_gltf<M>(name, keep);
	direct->save_to_file(f.file.string());
	std::vector<char> const first = f.bytes();
	auto const loaded = std::make_shared<M>(f.renderer, f.scheduler);
	loaded->load_from_file(f.file.string());
	loaded->save_to_file(f.file.string());
	BOOST_CHECK_MESSAGE(f.bytes() == first, name << ": the model from its .edge file is not the one from the glTF");
}

BOOST_AUTO_TEST_CASE(every_test_model_is_the_same_from_its_gltf_and_from_its_edge_file)
{
	// (each of the kind ModelCache loads it as)
	for (char const *name : {"cone.glb", "cylinder_30_degrees.glb", "test_attributes.glb", "test_attributes_two.glb", "test_gltf_loader_pbr.glb",
		"test_gltf_loader_dds.glb", "test_gltf_loder_plane.glb", "test_gltf_loder_directions.glb", "test_gltf_loder_materials.glb",
		"test_gltf_loder_tbn.glb", "test_gltf_loder_animation.glb", "test_gltf_loder_animation_cubic_valid.gltf",
		"test_gltf_loder_animation_weights_path.gltf", "test_gltf_scene_after_an_empty_one.gltf", "test_uv_set_1.gltf", "test_uv_transform.gltf",
		"test_uv_transform_turned.gltf", "test_external_buffer.gltf", "test_vertex_colors.glb", "test_vertex_colors_ubyte.glb",
		"test_vertex_colors_ushort.glb", "test_vertex_colors_vec3.glb", "test_vertex_colors_two.glb", "test_figure.glb", "test_gltf_loder_joint.glb",
		"test_skinned_groups.glb"})
	{
		edge::GltfContents const contents = edge::gltf_contents(asset(name));
		if (contents.skin)
			the_same_from_either_file<Animated>(*this, name);
		else if (contents.vertex_colors)
			the_same_from_either_file<edge::StaticModelPTBNTC>(*this, name);
		else
			the_same_from_either_file<edge::StaticModelPTBNT>(*this, name);
	}
}

BOOST_AUTO_TEST_CASE(a_saved_model_says_the_pipeline_that_wrote_it_and_no_converter_s_options)
{
	auto const model = from_gltf<edge::StaticModelPTBNT>("test_gltf_loder_plane.glb");
	model->save_to_file(file.string());
	edge::SerializedModel const saved = edge::Model::read_file(file.string());
	BOOST_TEST(saved.pipeline == std::string(edge::pipeline_hash()));
	BOOST_TEST(saved.options.empty());
}

BOOST_AUTO_TEST_CASE(a_skinned_model_with_clips_comes_back_the_same)
{
	auto original = from_gltf<Animated>("test_figure.glb");
	auto loaded = reloaded(*original);
	same_model(*original, *loaded);
	same_animations(*original, *loaded);
}

BOOST_AUTO_TEST_CASE(a_skinned_models_pose_box_comes_back)
{
	auto original = from_gltf<Animated>("test_figure.glb");
	auto loaded = reloaded(*original);
	for (size_t i = 0; i < original->meshes().size(); ++i)
	{
		auto const &a = static_cast<edge::AnimatedMeshPTBNJWT const &>(*original->meshes()[i]);
		auto const &b = static_cast<edge::AnimatedMeshPTBNJWT const &>(*loaded->meshes()[i]);
		BOOST_CHECK_EQUAL(a.pose_bounds().lo.x, b.pose_bounds().lo.x);
		BOOST_CHECK_EQUAL(a.pose_bounds().hi.y, b.pose_bounds().hi.y);
		BOOST_CHECK_GT(b.pose_bounds().hi.x, 0.0f);
	}
}

BOOST_AUTO_TEST_CASE(a_model_that_was_posed_before_saving_comes_back_as_posed)
{
	// saving writes the joints' local transforms as they are: a posed model is what it is
	auto original = from_gltf<Animated>("test_figure.glb");
	original->animations().at(0)->pose(0.4f);
	auto loaded = reloaded(*original);
	same_model(*original, *loaded);
}

BOOST_AUTO_TEST_CASE(a_skinned_models_custom_attributes_come_back)
{
	auto original = from_gltf<Animated>("test_skinned_groups.glb");
	BOOST_REQUIRE(!original->meshes().at(0)->custom_attributes().empty());
	auto loaded = reloaded(*original);
	same_model(*original, *loaded);
	BOOST_CHECK(loaded->meshes().at(0)->custom_attributes().at("_GROUP").values == original->meshes().at(0)->custom_attributes().at("_GROUP").values);
}

BOOST_AUTO_TEST_CASE(a_static_models_custom_attributes_of_every_width_come_back)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_attributes.glb");
	auto loaded = reloaded(*original);
	same_model(*original, *loaded);
}

BOOST_AUTO_TEST_CASE(pbr_materials_and_their_textures_come_back)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_gltf_loader_pbr.glb");
	auto loaded = reloaded(*original);
	same_model(*original, *loaded);
	bool saw_metallic_roughness = false, saw_emissive = false;
	for (auto const &m : loaded->materials())
	{
		saw_metallic_roughness = saw_metallic_roughness || m->metallic_roughness_texture();
		saw_emissive = saw_emissive || m->emissive_texture();
	}
	BOOST_CHECK(saw_metallic_roughness);
	BOOST_CHECK(saw_emissive);
}

/// What the game loads its textures with: uncompressed, sRGB colour, mip chains made by the GPU.
BOOST_AUTO_TEST_CASE(srgb_and_mipmapped_textures_come_back_as_such)
{
	edge::GltfOptions game;
	game.compress_textures = false;
	game.srgb_color = true;
	game.mipmaps = true;
	auto original = from_gltf<edge::StaticModelPTBNT>("test_gltf_loader_pbr.glb", game);
	auto loaded = reloaded(*original);
	same_model(*original, *loaded);
	bool saw_srgb = false, saw_mips = false;
	for (auto const &m : loaded->materials())
		for (size_t i = 0; i < m->diffuse_textures_count(); ++i)
			if (auto const t = m->diffuse_texture(i))
			{
				saw_srgb = saw_srgb || t->is_srgb();
				saw_mips = saw_mips || t->mip_levels() > 1;
			}
	BOOST_CHECK(saw_srgb);
	BOOST_CHECK(saw_mips);
}

/// A DDS carries its own mip levels, and every one of them must come back.
BOOST_AUTO_TEST_CASE(a_dds_textures_provided_mip_levels_come_back)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_gltf_loader_dds.glb");
	auto loaded = reloaded(*original);
	same_model(*original, *loaded);
	bool saw_levels = false;
	for (auto const &m : loaded->materials())
		for (size_t i = 0; i < m->diffuse_textures_count(); ++i)
			if (auto const t = m->diffuse_texture(i))
				saw_levels = saw_levels || t->usage().provided_mip_levels > 1;
	BOOST_CHECK(saw_levels);
}

BOOST_AUTO_TEST_CASE(morph_targets_and_their_animated_weights_come_back)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_gltf_loder_animation_weights_path.gltf");
	BOOST_REQUIRE(!original->animations().empty());
	BOOST_REQUIRE(!original->animations().at(0)->weight_samplers.empty());
	auto loaded = reloaded(*original);
	same_model(*original, *loaded);
	same_animations(*original, *loaded);
	for (size_t i = 0; i < original->animations().size(); ++i)
		for (float t : {0.0f, 0.25f, 0.5f, 1.0f})
		{
			original->animations()[i]->pose(t);
			loaded->animations()[i]->pose(t);
			BOOST_CHECK_MESSAGE(geometry(*original->meshes().at(0)).morph_weights == geometry(*loaded->meshes().at(0)).morph_weights,
				"morph weights at t=" << t);
		}
}

BOOST_AUTO_TEST_CASE(joint_animations_come_back)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_gltf_loder_animation.glb");
	BOOST_REQUIRE(!original->animations().empty());
	auto loaded = reloaded(*original);
	same_model(*original, *loaded);
	same_animations(*original, *loaded);
}

BOOST_AUTO_TEST_CASE(a_loaded_models_animations_are_bound_to_it)
{
	auto original = from_gltf<Animated>("test_figure.glb");
	auto loaded = reloaded(*original);
	loaded->animations().at(2)->pose(0.3f);       // rebuilds this model's joints, not a stranger's
	rove::matrix<4,4> const before = original->joints().at(5)->matrix;
	loaded->animations().at(2)->pose(0.7f);
	same_matrix(original->joints().at(5)->matrix, before, 0, "the original's joint");
	original->animations().at(2)->pose(0.7f);
	same_matrix(loaded->joints().at(5)->matrix, original->joints().at(5)->matrix, 0, "the loaded joint");
}

BOOST_AUTO_TEST_CASE(drawing_a_loaded_model_submits_what_the_gltf_model_submits)
{
	auto original = from_gltf<Animated>("test_figure.glb");
	auto loaded = reloaded(*original);
	edge::CameraPtr camera(new edge::Camera());
	camera->set_perspective_projection(1.0f, 4.0f / 3.0f, 0.05f, 50.0f);
	camera->look_at(rove::vec<3>(0, 1, 3), rove::vec<3>(0, 1, 0), rove::vec<3>(0, 1, 0));
	rove::matrix<4,4> identity;
	identity.identity();
	size_t calls[2];
	for (int i = 0; i < 2; ++i)
	{
		auto &model = i == 0 ? *original : *loaded;
		model.scheduler()->clear();
		model.draw(camera, identity);
		edge::flush_in_a_pass(model.scheduler());
		calls[i] = model.scheduler()->visible_calls_count();
	}
	BOOST_CHECK_GT(calls[0], 0u);
	BOOST_CHECK_EQUAL(calls[0], calls[1]);
}

BOOST_AUTO_TEST_CASE(loading_through_the_cache_gives_instances_with_their_own_clips)
{
	auto original = from_gltf<Animated>("test_figure.glb");
	original->save_to_file(file.string());
	edge::ModelCache cache(renderer);
	auto a = cache.load<Animated>(file.string(), scheduler);
	auto b = cache.load<Animated>(file.string(), scheduler);
	BOOST_CHECK_EQUAL(cache.prototypes_count(), 1u);
	BOOST_REQUIRE_EQUAL(a->animations().size(), original->animations().size());
	a->animations().at(2)->pose(0.5f);
	b->animations().at(2)->pose(0.0f);
	float moved = 0;
	for (size_t j = 0; j < a->joints().size(); ++j)
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				moved += std::abs(a->joints()[j]->matrix.ij[c][r] - b->joints()[j]->matrix.ij[c][r]);
	BOOST_CHECK_GT(moved, 0.0f);
}

/// The CPU copy of a texture is the loader's choice (the game frees it after the upload), not the file's.
BOOST_AUTO_TEST_CASE(a_load_can_drop_the_cpu_copy_of_the_textures)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_gltf_loader_pbr.glb");
	original->save_to_file(file.string());
	auto kept = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	kept->load_from_file(file.string(), true);
	auto dropped = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	dropped->load_from_file(file.string(), false);
	bool checked = false;
	for (size_t m = 0; m < kept->materials().size(); ++m)
		for (size_t i = 0; i < kept->materials()[m]->diffuse_textures_count(); ++i)
			if (auto const t = kept->materials()[m]->diffuse_texture(i))
			{
				BOOST_CHECK(t->usage().keep_cpu_copy);
				BOOST_CHECK(!dropped->materials()[m]->diffuse_texture(i)->usage().keep_cpu_copy);
				checked = true;
			}
	BOOST_CHECK(checked);
}

BOOST_AUTO_TEST_CASE(a_model_whose_textures_kept_no_cpu_copy_cannot_be_saved_and_says_why)
{
	edge::GltfOptions gpu_only;
	gpu_only.keep_cpu_copy = false;
	auto const model = from_gltf<edge::StaticModelPTBNT>("test_gltf_loader_pbr.glb", gpu_only);
	try
	{
		model->save_to_file(file.string());
		BOOST_FAIL("a model without the texels of its textures was saved");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find("no CPU copy") != std::string::npos, e.what());
	}
}

BOOST_AUTO_TEST_CASE(a_skinned_mesh_without_a_pose_box_cannot_be_saved)
{
	auto model = std::make_shared<Animated>(renderer, scheduler);
	model->add_mesh();
	BOOST_CHECK_THROW(model->save_to_file(file.string()), std::logic_error);
}

/// Version 5 wrote vertices without a color, version 6 every vertex whole whatever its type: read as this version either is garbage.
BOOST_AUTO_TEST_CASE(a_file_of_an_older_version_is_refused_and_says_so)
{
	auto original = from_gltf<Animated>("test_figure.glb");
	original->save_to_file(file.string());
	std::vector<char> const whole = bytes();
	for (std::uint32_t const version : {4u, 5u, 6u})
	{
		std::vector<char> data = whole;
		std::memcpy(data.data() + 4, &version, 4);
		write(data);
		refused<Animated>("version " + std::to_string(version));
	}
}

BOOST_AUTO_TEST_CASE(a_file_with_the_wrong_magic_is_refused)
{
	auto original = from_gltf<Animated>("test_figure.glb");
	original->save_to_file(file.string());
	std::vector<char> data = bytes();
	data[0] ^= 0x55;
	write(data);
	auto loaded = std::make_shared<Animated>(renderer, scheduler);
	BOOST_CHECK_THROW(loaded->load_from_file(file.string()), std::runtime_error);
}

/// Every cut of the file short of its end is an error, never a half-loaded model.
BOOST_AUTO_TEST_CASE(a_truncated_file_is_refused_wherever_it_is_cut)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_gltf_loder_animation.glb");
	original->save_to_file(file.string());
	std::vector<char> const whole = bytes();
	for (size_t keep = 0; keep < whole.size(); keep += std::max<size_t>(1, whole.size() / 64))
	{
		write(std::vector<char>(whole.begin(), whole.begin() + std::ptrdiff_t(keep)));
		auto loaded = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
		BOOST_CHECK_THROW(loaded->load_from_file(file.string()), std::exception);
	}
}

BOOST_AUTO_TEST_CASE(bytes_after_the_end_of_the_model_are_refused)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_gltf_loder_animation.glb");
	original->save_to_file(file.string());
	std::vector<char> data = bytes();
	data.push_back(0);
	write(data);
	auto loaded = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	BOOST_CHECK_THROW(loaded->load_from_file(file.string()), std::runtime_error);
}

/// A vertex takes the file the components of the mesh's vertex type and no more: a static mesh does not carry joints and weights.
BOOST_AUTO_TEST_CASE(a_vertex_costs_the_file_only_the_components_of_its_type)
{
	auto const vertex_cost = [this](auto model, auto vertex) {
		using M = typename decltype(model)::type;
		using V = typename decltype(vertex)::type;
		return file_size_with<M, V>(4) - file_size_with<M, V>(3);
	};
	BOOST_CHECK_EQUAL(vertex_cost(std::type_identity<edge::StaticModelPNT>(), std::type_identity<edge::VERTEX_PNT>()), 12u + 12u + 8u);
	BOOST_CHECK_EQUAL(vertex_cost(std::type_identity<edge::StaticModelPTBNT>(), std::type_identity<edge::VERTEX_PTBNT>()), 4u * 12u + 8u);
	BOOST_CHECK_EQUAL(vertex_cost(std::type_identity<edge::StaticModelPTBNTC>(), std::type_identity<edge::VERTEX_PTBNTC>()), 4u * 12u + 8u + 16u);
	BOOST_CHECK_EQUAL(vertex_cost(std::type_identity<Animated>(), std::type_identity<edge::VERTEX_PTBNJWT>()), 4u * 12u + 32u + 32u + 8u);
}

BOOST_AUTO_TEST_CASE(a_custom_attribute_of_another_length_than_the_vertices_is_refused)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_attributes.glb");
	original->save_to_file(file.string());
	edge::SerializedModel model = serialized();
	edge::SerializedCustomAttribute &attribute = model.meshes.at(0).custom_attributes.at(0);
	attribute.values.resize(attribute.values.size() - attribute.components);
	write(model);
	refused<edge::StaticModelPTBNT>(attribute.name);
}

BOOST_AUTO_TEST_CASE(a_custom_attribute_of_another_length_than_the_vertices_is_not_saved)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_attributes.glb");
	edge::MeshPtr const mesh = original->meshes().at(0);
	std::map<std::string, edge::Mesh::CustomAttribute> attributes = mesh->custom_attributes();
	attributes.begin()->second.values.push_back(0);
	mesh->set_custom_attributes(attributes);
	BOOST_CHECK_THROW(original->save_to_file(file.string()), std::logic_error);
}

BOOST_AUTO_TEST_CASE(morph_weights_of_another_count_than_the_targets_are_refused)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_gltf_loder_animation_weights_path.gltf");
	original->save_to_file(file.string());
	edge::SerializedModel model = serialized();
	BOOST_REQUIRE(!model.meshes.at(0).morph_weights.empty());
	model.meshes.at(0).morph_weights.pop_back();
	write(model);
	refused<edge::StaticModelPTBNT>("morph weights");
}

/// A rotation key of no length is damage, as glTF refuses one: rove refuses to turn by it (it was the identity), and the file says which clip.
BOOST_AUTO_TEST_CASE(a_rotation_key_that_is_no_rotation_is_refused)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_gltf_loder_animation.glb");
	original->save_to_file(file.string());
	edge::SerializedModel model = serialized();
	BOOST_REQUIRE(!model.animations.empty() && !model.animations.at(0).samplers.empty());
	std::vector<float> &values = model.animations.at(0).samplers.at(0).keyframes.at(1).values;
	values[3] = values[4] = values[5] = values[6] = 0;
	write(model);
	refused<edge::StaticModelPTBNT>("rotation keyframe");
}

/// A length the file has no room for is damage, found before anything that long is made: not an allocation of terabytes.
BOOST_AUTO_TEST_CASE(a_damaged_length_is_refused_before_it_is_allocated)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_attributes.glb");
	original->save_to_file(file.string());
	edge::SerializedModel model = serialized();
	std::string const marker = "_DAMAGED_LENGTH_FOLLOWS";
	model.meshes.at(0).custom_attributes.at(0).name = marker;
	write(model);
	std::vector<char> data = bytes();
	auto const at = std::search(data.begin(), data.end(), marker.begin(), marker.end());
	BOOST_REQUIRE(at != data.end());
	std::uint64_t const length = std::uint64_t(1) << 62;           // after the name: the components (4 bytes), then the count of values
	std::memcpy(&*(at + std::ptrdiff_t(marker.size()) + 4), &length, 8);
	write(data);
	refused<edge::StaticModelPTBNT>("damaged");
}

/// Triangles may have no material (a model built by hand): they are saved with none and come back with none.
BOOST_AUTO_TEST_CASE(triangles_without_a_material_come_back_without_one)
{
	auto original = from_gltf<edge::StaticModelPTBNT>("test_gltf_loder_plane.glb");
	edge::MeshPtr const mesh = original->meshes().at(0);
	edge::Mesh::triangles_list_type groups = mesh->triangles();
	for (edge::Mesh::Triangles &triangles : groups)
		triangles.material_ptr.reset();
	mesh->set_triangles(groups);
	auto loaded = reloaded(*original);
	same_model(*original, *loaded);
	for (edge::Mesh::Triangles const &triangles : loaded->meshes().at(0)->triangles())
		BOOST_CHECK(!triangles.material_ptr);
}

// The file's numbers are as wide on every machine, so a file written on one is read on another: a browser's WebAssembly has a size_t of
// 4 bytes, the desktop that converted the models one of 8.  Byte for byte, little-endian.
BOOST_AUTO_TEST_CASE(the_file_s_numbers_are_as_wide_on_every_machine)
{
	auto const bytes = [](auto const &value)
	{
		std::ostringstream stream;
		{
			cereal::BinaryOutputArchive archive(stream);
			archive(value);
		}
		std::string const s = stream.str();
		return std::vector<unsigned char>(s.begin(), s.end());
	};
	auto const le64 = [](std::uint64_t v)
	{
		std::vector<unsigned char> out;
		for (int i = 0; i < 8; ++i)
			out.push_back(static_cast<unsigned char>(v >> (8 * i)));
		return out;
	};
	edge::SerializedTriangles triangles;
	triangles.material_index = -1;
	triangles.minimal_vertex_index = 1;
	triangles.vertices_count = 2;
	triangles.first_index_offset = 3;
	triangles.triangles_count = 0x0102030405ULL;      // (more than 32 bits hold)
	std::vector<unsigned char> expected = le64(~std::uint64_t(0));
	for (std::uint64_t v : {std::uint64_t(1), std::uint64_t(2), std::uint64_t(3), std::uint64_t(0x0102030405ULL)})
	{
		auto const b = le64(v);
		expected.insert(expected.end(), b.begin(), b.end());
	}
	BOOST_TEST(bytes(triangles) == expected, boost::test_tools::per_element());

	edge::SerializedJoint joint;
	joint.index = 5;
	joint.id = "j";
	std::vector<unsigned char> const joint_bytes = bytes(joint);
	std::vector<unsigned char> start = le64(5);                // the index,
	auto const id_length = le64(1);                             // then the id: its length and its byte
	start.insert(start.end(), id_length.begin(), id_length.end());
	start.push_back('j');
	BOOST_REQUIRE(joint_bytes.size() > start.size());
	BOOST_TEST(std::vector<unsigned char>(joint_bytes.begin(), joint_bytes.begin() + std::ptrdiff_t(start.size())) == start,
		boost::test_tools::per_element());

	// a skinned mesh's joints (each a joint of the model), after its kind, id, name and matrices: 8 bytes each, as their count (they were
	// size_t's: a file converted on a 64-bit desktop read as damaged in a 32-bit browser build)
	edge::SerializedMesh mesh;
	mesh.serialization_type = "animated_mesh:1";          // (positions alone)
	mesh.joint_index_transform = {7, 0x0102030405ULL};
	std::vector<unsigned char> const mesh_bytes = bytes(mesh);
	std::vector<unsigned char> joints = le64(2);
	for (std::uint64_t v : {std::uint64_t(7), std::uint64_t(0x0102030405ULL)})
	{
		auto const b = le64(v);
		joints.insert(joints.end(), b.begin(), b.end());
	}
	std::size_t const before = 8 + mesh.serialization_type.size() + 8 + 8 + 16 * 4 + 8;          // kind, id, name, bind shape, no inverse binds
	BOOST_REQUIRE(mesh_bytes.size() > before + joints.size());
	BOOST_TEST(std::vector<unsigned char>(mesh_bytes.begin() + std::ptrdiff_t(before), mesh_bytes.begin() + std::ptrdiff_t(before + joints.size())) ==
		joints, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(a_missing_file_is_an_error)
{
	auto loaded = std::make_shared<Animated>(renderer, scheduler);
	BOOST_CHECK_THROW(loaded->load_from_file((file.parent_path() / "no_such_model.edge").string()), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
