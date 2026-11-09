// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cereal/archives/binary.hpp>

#include <rove/frustum.h>
#include "log.h"
#include <rove/obb.h>
#include "gltf_loader.h"
#include "model.h"
#include "pipeline.h"
#include "renderer.h"
#include "serialized_model.h"
#include "serialized_model_archive.h"
#include "texture.h"

namespace edge {

Model::Model(RendererPtr const &renderer_ptr, SchedulerPtr const &scheduler_ptr):
	renderer_(renderer_ptr),
	scheduler_(scheduler_ptr),
	always_visible_(false)
{
	if (!renderer_ || !scheduler_)
	{
		throw std::invalid_argument(std::string("a model without a ") + (renderer_ ? "scheduler" : "renderer"));
	}
}

Model::Model(Model const &model, SchedulerPtr const &scheduler_ptr):
	renderer_(model.renderer_),
	scheduler_(scheduler_ptr),
	always_visible_(model.always_visible_)
{
	std::unordered_map<JointPtr, JointPtr> joints_tt;
	std::unordered_map<MeshPtr, MeshPtr> meshes_tt;
	std::unordered_map<MaterialPtr, MaterialPtr> materials_tt;

	// Everything a copied mesh, joint or sampler points at must be this model's
	// copy: one left pointing into the prototype would pose or morph it,
	// and with it every other instance.
	auto remap = [](auto const &table, auto const &source, char const *what) {
		auto found = table.find(source);
		if (found == table.end())
		{
			throw std::logic_error(std::string("model copy: a ") + what + " refers to one its model does not own");
		}
		return found->second;
	};

	materials_.reserve(model.materials().size());
	for (MaterialPtr const &source : model.materials())
	{
		MaterialPtr material = source ? source->clone() : MaterialPtr();
		materials_.push_back(material);
		if (source)
		{
			materials_tt.insert(std::make_pair(source, material));
		}
	}

	joints_.reserve(model.joints().size());
	animations_.reserve(model.animations().size());
	meshes_.reserve(model.meshes().size());

	for (auto source : model.joints())
	{
			JointPtr joint(new Joint(*source));

		joints_.push_back(joint);
		joints_tt.insert(std::make_pair(source, joint));
	}

	for (auto const &animation : model.animations())
	{
		animations_.push_back(std::make_shared<Animation>(*animation));
	}

	for (auto source : model.meshes())
	{
		MeshPtr mesh = source->shallow_copy();
		mesh->set_model(this);

		// the copy's groups draw with the copies of their materials
		auto remapped = [&](Mesh::triangles_list_type list) {
			for (Mesh::Triangles &triangles : list)
			{
				if (triangles.material_ptr)
				{
					triangles.material_ptr = remap(materials_tt, triangles.material_ptr, "mesh's material");
				}
			}
			return list;
		};
		mesh->set_triangles(remapped(mesh->triangles()));
		std::vector<Mesh::Lod> lods = mesh->lods();
		for (Mesh::Lod &lod : lods)
		{
			lod.triangles = remapped(std::move(lod.triangles));
		}
		mesh->set_lods(std::move(lods));

		meshes_.push_back(mesh);

		meshes_tt.insert(std::make_pair(source, mesh));
	}

	for (auto &joint : joints())
	{
		if (JointPtr parent = joint->parent.lock())
		{
			joint->parent = remap(joints_tt, parent, "joint's parent");
		}

		for (JointWeakPtr &child : joint->children)
		{
			child = remap(joints_tt, child.lock(), "joint's child");
		}

		if (MeshPtr mesh_ptr = joint->mesh.lock())
		{
			joint->mesh = remap(meshes_tt, mesh_ptr, "joint's mesh");
		}
	}

	for (auto &animation : animations())
	{
		for (Sampler &sampler : animation->samplers)
		{
			if (JointPtr joint = sampler.joint())
			{
				sampler.set_joint(remap(joints_tt, joint, "sampler's joint"));
			}
		}

		for (WeightsSampler &sampler : animation->weight_samplers)
		{
			if (MeshPtr mesh = sampler.mesh())
			{
				sampler.set_mesh(remap(meshes_tt, mesh, "weights sampler's mesh"));
			}
		}
	}
}

Model::~Model() = default;

JointPtr Model::add_joint()
{
	joints_.push_back(std::make_shared<Joint>(joints().size()));
	return joints().back();
}

void Model::clear()
{
	joints_.clear();
	meshes_.clear();
	animations_.clear();
	materials_.clear();
	always_visible_ = false;
	cached_pose_.reset();
	joints_stale_ = false;
}

void Model::bind_animations()
{
	auto self = shared_from_this();
	for (auto &animation : animations())
		animation->model = self;
}

void Model::load_from_gltf(std::string const &filename, GltfOptions const &options)
{
	load(GltfLoader::read(filename, options, mesh_serialization_type()), options.keep_cpu_copy, false);
	// the boxes of the poses its clips make, which a .edge file has worked out already: they pose the model, and leave it as the
	// glTF has it
	compute_pose_bounds(options.pose_margin);
	forget_posing();
}

void Model::compute_pose_bounds(float)
{
}

void Model::save_to_file(std::string const &filename) const
{
	write_file(serialize(), filename);
}

SerializedModel Model::serialize() const
{
	SerializedModel serialized;
	serialized.pipeline = pipeline_hash();
	TextureTable textures;
	std::unordered_map<Material const *, std::int64_t> material_index_by_ptr;

	auto const add_material = [&](MaterialPtr const &material_ptr) {
		if (!material_ptr)
		{
			throw std::logic_error("a null material in the model");
		}
		if (material_index_by_ptr.find(material_ptr.get()) == material_index_by_ptr.end())
		{
			material_index_by_ptr[material_ptr.get()] = static_cast<std::int64_t>(serialized.materials.size());
			serialized.materials.push_back(export_material(material_ptr, textures));
		}
	};
	for (MaterialPtr const &material_ptr : materials())
	{
		add_material(material_ptr);
	}
	// a material only a mesh holds is kept all the same; triangles may have none
	for (MeshPtr const &mesh_ptr : meshes())
	{
		for (Mesh::Triangles const &triangles : mesh_ptr->triangles())
		{
			if (triangles.material_ptr)
			{
				add_material(triangles.material_ptr);
			}
		}
		for (Mesh::Lod const &lod : mesh_ptr->lods())
		{
			for (Mesh::Triangles const &triangles : lod.triangles)
			{
				if (triangles.material_ptr)
				{
					add_material(triangles.material_ptr);
				}
			}
		}
	}

	serialized.meshes.reserve(meshes().size());
	for (MeshPtr const &mesh_ptr : meshes())
	{
		SerializedMesh serialized_mesh;
		serialized_mesh.serialization_type = mesh_ptr->serialization_type();
		if (serialized_mesh.serialization_type.empty())
		{
			throw std::runtime_error("mesh returned empty serialization type");
		}

		serialized_mesh.id = mesh_ptr->id();
		serialized_mesh.name = mesh_ptr->name();
		serialized_mesh.bind_shape_matrix = mesh_ptr->bind_shape_matrix();
		serialized_mesh.joint_index_transform.assign(mesh_ptr->joint_index_transform().begin(), mesh_ptr->joint_index_transform().end());
		serialized_mesh.inverted_bind_matrices = mesh_ptr->inverted_bind_matrices();
		mesh_ptr->save_geometry(serialized_mesh);
		for (auto const &entry : mesh_ptr->custom_attributes())
		{
			if (entry.second.values.size() != entry.second.components * serialized_mesh.vertices.size())
			{
				throw std::logic_error("custom attribute " + entry.first + " of mesh '" + mesh_ptr->name() + "' has " +
					std::to_string(entry.second.values.size()) + " numbers for " + std::to_string(serialized_mesh.vertices.size()) +
					" vertices of " + std::to_string(entry.second.components));
			}
			serialized_mesh.custom_attributes.push_back(SerializedCustomAttribute{entry.first, entry.second.components, entry.second.values});
		}

		auto const serialize_triangles = [&](Mesh::triangles_list_type const &list) {
			std::vector<SerializedTriangles> result;
			result.reserve(list.size());
			for (Mesh::Triangles const &triangles : list)
			{
				SerializedTriangles serialized_triangles;
				serialized_triangles.material_index = triangles.material_ptr ? material_index_by_ptr.at(triangles.material_ptr.get()) : -1;
				serialized_triangles.minimal_vertex_index = triangles.minimal_vertex_index;
				serialized_triangles.vertices_count = triangles.vertices_count;
				serialized_triangles.first_index_offset = triangles.first_index_offset;
				serialized_triangles.triangles_count = triangles.triangles_count;
				result.push_back(serialized_triangles);
			}
			return result;
		};
		serialized_mesh.triangles = serialize_triangles(mesh_ptr->triangles());
		for (Mesh::Lod const &lod : mesh_ptr->lods())
		{
			serialized_mesh.lods.push_back(SerializedLod{lod.error, serialize_triangles(lod.triangles)});
		}

		serialized.meshes.push_back(serialized_mesh);
	}

	std::unordered_map<Mesh const *, std::int64_t> mesh_index_by_ptr;
	for (std::size_t i = 0; i < meshes().size(); ++i)
	{
		mesh_index_by_ptr[meshes()[i].get()] = static_cast<std::int64_t>(i);
	}

	std::unordered_map<Joint const *, std::int64_t> joint_index_by_ptr;
	for (std::size_t i = 0; i < joints().size(); ++i)
	{
		joint_index_by_ptr[joints()[i].get()] = static_cast<std::int64_t>(i);
	}

	serialized.joints.reserve(joints().size());
	for (JointPtr const &joint_ptr : joints())
	{
		SerializedJoint serialized_joint;
		serialized_joint.index = joint_ptr->index;
		serialized_joint.id = joint_ptr->id;
		serialized_joint.name = joint_ptr->name;
		serialized_joint.matrix = joint_ptr->matrix;

		serialized_joint.parent_index = -1;
		if (JointPtr parent_ptr = joint_ptr->parent.lock())
		{
			serialized_joint.parent_index = joint_index_by_ptr.at(parent_ptr.get());
		}

		for (JointWeakPtr const &child : joint_ptr->children)
		{
			JointPtr const child_ptr = child.lock();
			if (!child_ptr)
			{
				throw std::logic_error("joint '" + joint_ptr->name + "' has a child that is gone");
			}
			serialized_joint.child_indices.push_back(joint_index_by_ptr.at(child_ptr.get()));
		}

		serialized_joint.mesh_index = -1;
		if (MeshPtr mesh_ptr = joint_ptr->mesh.lock())
		{
			serialized_joint.mesh_index = mesh_index_by_ptr.at(mesh_ptr.get());
		}

		for (Joint::Transform const &transform : joint_ptr->transforms)
		{
			serialized_joint.transforms.push_back(SerializedJointTransform{transform.sid, transform.matrix});
		}

		serialized.joints.push_back(serialized_joint);
	}

	for (AnimationPtr const &animation : animations())
	{
		SerializedAnimation serialized_animation;
		serialized_animation.name = animation->name;
		for (Sampler const &sampler : animation->samplers)
		{
			SerializedSampler serialized_sampler;
			JointPtr const joint = sampler.joint();
			auto const found = joint ? joint_index_by_ptr.find(joint.get()) : joint_index_by_ptr.end();
			if (found == joint_index_by_ptr.end())
			{
				throw std::logic_error("clip '" + animation->name + "' moves a joint that is not in the model");
			}
			serialized_sampler.joint_index = found->second;
			serialized_sampler.sid = sampler.sid();
			for (std::size_t k = 0; k < sampler.samples_count(); ++k)
			{
				SerializedKeyframe keyframe;
				Sampler::Key const &key = sampler.sample(k);
				keyframe.time = key.time;
				keyframe.values = {key.translation.x, key.translation.y, key.translation.z,
					key.rotation.x, key.rotation.y, key.rotation.z, key.rotation.w, key.scale.x, key.scale.y, key.scale.z};
				serialized_sampler.keyframes.push_back(keyframe);
			}
			serialized_animation.samplers.push_back(serialized_sampler);
		}
		for (WeightsSampler const &sampler : animation->weight_samplers)
		{
			SerializedWeightsSampler serialized_sampler;
			MeshPtr const mesh = sampler.mesh();
			auto const found = mesh ? mesh_index_by_ptr.find(mesh.get()) : mesh_index_by_ptr.end();
			if (found == mesh_index_by_ptr.end())
			{
				throw std::logic_error("clip '" + animation->name + "' moves the weights of a mesh that is not in the model");
			}
			serialized_sampler.mesh_index = found->second;
			serialized_sampler.interpolation = static_cast<int>(sampler.interpolation());
			for (std::size_t k = 0; k < sampler.samples_count(); ++k)
			{
				serialized_sampler.keyframes.push_back(SerializedKeyframe{sampler.sample_time(k), sampler.sample_weights(k)});
			}
			serialized_animation.weight_samplers.push_back(serialized_sampler);
		}
		serialized.animations.push_back(serialized_animation);
	}
	serialized.textures = textures.textures();
	return serialized;
}

void Model::write_file(SerializedModel const &serialized, std::string const &filename)
{
	std::ofstream stream(filename.c_str(), std::ios::binary | std::ios::trunc);
	if (!stream.is_open())
	{
		throw std::runtime_error("failed to open model file for writing: " + filename);
	}
	cereal::BinaryOutputArchive archive(stream);
	archive(serialized);
}

SerializedModel Model::read_file(std::string const &filename)
{
	std::ifstream stream(filename.c_str(), std::ios::binary);
	if (!stream.is_open())
	{
		throw std::runtime_error("failed to open model file for reading: " + filename);
	}
	std::vector<char> const data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
	return read_bytes(std::string_view(data.data(), data.size()), filename);
}

SerializedModel Model::read_bytes(std::string_view bytes, std::string const &name)
{
	SerializedModel serialized;
	BoundedInputArchive archive(bytes.data(), bytes.size());
	try
	{
		archive(serialized);
	}
	catch (cereal::Exception const &e)
	{
		throw std::runtime_error("model file " + name + " is damaged: " + e.what());
	}
	if (archive.left() != 0)
	{
		throw std::runtime_error("model file " + name + " has bytes after the end of the model");
	}
	return serialized;
}

void Model::load_from_file(std::string const &filename, bool keep_cpu_copy)
{
	SerializedModel const serialized = read_file(filename);
	try
	{
		load(serialized, keep_cpu_copy);
	}
	catch (std::runtime_error const &e)
	{
		throw std::runtime_error("model file " + filename + ": " + e.what());
	}
}

void Model::load_from_bytes(std::string_view bytes, std::string const &name, bool keep_cpu_copy)
{
	SerializedModel const serialized = read_bytes(bytes, name);
	try
	{
		load(serialized, keep_cpu_copy);
	}
	catch (std::runtime_error const &e)
	{
		throw std::runtime_error("model file " + name + ": " + e.what());
	}
}

void Model::load(SerializedModel const &serialized, bool keep_cpu_copy)
{
	load(serialized, keep_cpu_copy, true);
}

namespace
{

/// What a mesh of a file says has to be possible: the file may have come from anyone, and past here these numbers index the mesh's arrays
/// and reach the GPU unchecked.  std::runtime_error naming the mesh.
void validate_mesh(SerializedMesh const &mesh, std::size_t joints_count)
{
	auto const fail = [&](std::string const &what) { return std::runtime_error("mesh '" + mesh.name + "': " + what); };
	std::size_t const indices = mesh.indices.size(), vertices = mesh.vertices.size();
	auto const check_groups = [&](std::vector<SerializedTriangles> const &groups) {
		for (SerializedTriangles const &group : groups)
		{
			// (each on its own side of the comparison: from a file, the sum and the product can wrap)
			if (group.triangles_count > indices / 3 || group.first_index_offset > indices - group.triangles_count * 3)
				throw fail("a group of " + std::to_string(group.triangles_count) + " triangles from " + std::to_string(group.first_index_offset) +
					" runs past its " + std::to_string(indices) + " indices");
			if (group.minimal_vertex_index > vertices || group.vertices_count > vertices - group.minimal_vertex_index)
				throw fail("a group of " + std::to_string(group.vertices_count) + " vertices from " + std::to_string(group.minimal_vertex_index) +
					" runs past its " + std::to_string(vertices) + " vertices");
			if (group.triangles_count > 0 && vertices == 0)
				throw fail("triangles and no vertices");
		}
	};
	check_groups(mesh.triangles);
	for (SerializedLod const &lod : mesh.lods)
		check_groups(lod.triangles);

	for (unsigned const index : mesh.indices)
		if (index >= vertices)
			throw fail("index " + std::to_string(index) + " of " + std::to_string(vertices) + " vertices");

	for (std::uint64_t const joint : mesh.joint_index_transform)
	{
		if (joint >= joints_count)
			throw fail("a skin of joint " + std::to_string(joint) + " in a model of " + std::to_string(joints_count) + " joints");
		if (joint >= mesh.inverted_bind_matrices.size())
			throw fail("a skin of joint " + std::to_string(joint) + " and " + std::to_string(mesh.inverted_bind_matrices.size()) + " bind matrices");
	}

	GenericVertex::format_type const format = serialized_vertex_format(mesh.serialization_type);
	bool const weighted = (format & GenericVertex::FORMAT_JOINT_INDICES) != 0 && (format & GenericVertex::FORMAT_JOINT_WEIGHTS) != 0;
	for (GenericVertex const &vertex : mesh.vertices)
	{
		if (!rove::is_finite(vertex.position))
			throw fail("a vertex position that is not finite");
		if (weighted)
			for (std::size_t k = 0; k < vertex.joints.dimensions; ++k)
				if (vertex.weights.i[k] != 0 && vertex.joints.i[k] >= mesh.joint_index_transform.size())
					throw fail("a vertex of skin joint " + std::to_string(vertex.joints.i[k]) + " in a skin of " +
						std::to_string(mesh.joint_index_transform.size()) + " joints");
	}
}

}

void Model::load(SerializedModel const &serialized, bool keep_cpu_copy, bool pose_boxes_given)
{
	for (SerializedMesh const &mesh : serialized.meshes)
		validate_mesh(mesh, serialized.joints.size());

	clear();

	std::vector<TexturePtr> textures;
	textures.reserve(serialized.textures.size());
	for (SerializedTexture const &texture : serialized.textures)
	{
		textures.push_back(import_texture(texture, renderer_, keep_cpu_copy));
	}

	materials_.reserve(serialized.materials.size());
	for (SerializedMaterial const &material : serialized.materials)
	{
		materials_.push_back(import_material(material, textures, renderer_));
	}

	std::vector<MeshPtr> loaded_meshes;
	loaded_meshes.reserve(serialized.meshes.size());
	for (SerializedMesh const &serialized_mesh : serialized.meshes)
	{
		MeshPtr const mesh_ptr = add_mesh();
		if (!mesh_ptr)
		{
			throw std::runtime_error("add_mesh returned null while loading model");
		}

		std::string const loaded_type = mesh_ptr->serialization_type();
		if (serialized_mesh.serialization_type != loaded_type)
		{
			auto describe = [](std::string const &type_str) -> std::string {
				return type_str.substr(0, type_str.rfind(':') + 1) + format_components_string(serialized_vertex_format(type_str));
			};
			throw std::runtime_error("serialized mesh type mismatch: expected " + describe(loaded_type) +
				", got " + describe(serialized_mesh.serialization_type));
		}

		mesh_ptr->set_id(serialized_mesh.id);
		mesh_ptr->set_name(serialized_mesh.name);
		mesh_ptr->set_bind_shape_matrix(serialized_mesh.bind_shape_matrix);
		mesh_ptr->set_joint_index_transform(Mesh::joint_index_transform_type(serialized_mesh.joint_index_transform.begin(),
			serialized_mesh.joint_index_transform.end()));
		mesh_ptr->set_inverted_bind_matrices(serialized_mesh.inverted_bind_matrices);
		std::map<std::string, Mesh::CustomAttribute> custom_attributes;
		for (SerializedCustomAttribute const &attribute : serialized_mesh.custom_attributes)
		{
			if (attribute.components < 1 || attribute.components > 4 ||
				attribute.values.size() != attribute.components * serialized_mesh.vertices.size())
			{
				throw std::runtime_error("custom attribute " + attribute.name + " of mesh '" + serialized_mesh.name + "' has " +
					std::to_string(attribute.values.size()) + " numbers for " + std::to_string(serialized_mesh.vertices.size()) +
					" vertices of " + std::to_string(attribute.components) + " components");
			}
			if (!custom_attributes.emplace(attribute.name, Mesh::CustomAttribute{attribute.components, attribute.values}).second)
			{
				throw std::runtime_error("custom attribute " + attribute.name + " is twice in mesh '" + serialized_mesh.name + "'");
			}
		}
		mesh_ptr->set_custom_attributes(std::move(custom_attributes));

		auto const deserialize_triangles = [&](std::vector<SerializedTriangles> const &list) {
			Mesh::triangles_list_type result;
			result.reserve(list.size());
			for (SerializedTriangles const &serialized_triangles : list)
			{
				Mesh::Triangles triangles;
				if (serialized_triangles.material_index >= 0)
				{
					if (static_cast<std::size_t>(serialized_triangles.material_index) >= materials().size())
					{
						throw std::runtime_error("model file contains invalid material index");
					}
					triangles.material_ptr = materials_[static_cast<std::size_t>(serialized_triangles.material_index)];
				}
				triangles.minimal_vertex_index = serialized_triangles.minimal_vertex_index;
				triangles.vertices_count = serialized_triangles.vertices_count;
				triangles.first_index_offset = serialized_triangles.first_index_offset;
				triangles.triangles_count = serialized_triangles.triangles_count;
				result.push_back(triangles);
			}
			return result;
		};
		mesh_ptr->set_triangles(deserialize_triangles(serialized_mesh.triangles));
		std::vector<Mesh::Lod> lods;
		for (SerializedLod const &serialized_lod : serialized_mesh.lods)
		{
			if (serialized_lod.triangles.size() != serialized_mesh.triangles.size())
			{
				throw std::runtime_error("mesh '" + serialized_mesh.name + "' has a level of detail of " + std::to_string(serialized_lod.triangles.size()) +
					" triangle groups where the mesh has " + std::to_string(serialized_mesh.triangles.size()));
			}
			for (SerializedTriangles const &group : serialized_lod.triangles)
			{
				if (group.first_index_offset + group.triangles_count * 3 > serialized_mesh.indices.size())
				{
					throw std::runtime_error("mesh '" + serialized_mesh.name + "' has a level of detail that runs past its indices");
				}
			}
			lods.push_back(Mesh::Lod{serialized_lod.error, deserialize_triangles(serialized_lod.triangles)});
		}
		mesh_ptr->set_lods(std::move(lods));

		mesh_ptr->load_geometry(serialized_mesh, pose_boxes_given);
		loaded_meshes.push_back(mesh_ptr);
	}

	std::vector<JointPtr> loaded_joints;
	loaded_joints.reserve(serialized.joints.size());
	for (std::size_t i = 0; i < serialized.joints.size(); ++i)
	{
		loaded_joints.push_back(add_joint());
	}

	for (std::size_t i = 0; i < serialized.joints.size(); ++i)
	{
		SerializedJoint const &serialized_joint = serialized.joints[i];
		JointPtr const &joint_ptr = loaded_joints[i];
		joint_ptr->index = serialized_joint.index;
		joint_ptr->id = serialized_joint.id;
		joint_ptr->name = serialized_joint.name;
		joint_ptr->matrix = serialized_joint.matrix;
		joint_ptr->parent.reset();
		joint_ptr->children.clear();
		joint_ptr->mesh.reset();
		joint_ptr->transforms.clear();
		joint_ptr->transforms.reserve(serialized_joint.transforms.size());
		for (SerializedJointTransform const &serialized_transform : serialized_joint.transforms)
		{
			joint_ptr->transforms.push_back(Joint::Transform{serialized_transform.sid, serialized_transform.matrix});
		}
	}

	for (std::size_t i = 0; i < serialized.joints.size(); ++i)
	{
		SerializedJoint const &serialized_joint = serialized.joints[i];
		JointPtr const &joint_ptr = loaded_joints[i];

		if (serialized_joint.parent_index >= 0)
		{
			std::size_t const parent_index = static_cast<std::size_t>(serialized_joint.parent_index);
			if (parent_index >= loaded_joints.size())
			{
				throw std::runtime_error("model file contains invalid joint parent index");
			}
			joint_ptr->parent = loaded_joints[parent_index];
		}

		for (std::int64_t const child_index : serialized_joint.child_indices)
		{
			if (child_index < 0 || static_cast<std::size_t>(child_index) >= loaded_joints.size() ||
				serialized.joints[static_cast<std::size_t>(child_index)].parent_index != static_cast<std::int64_t>(i))
			{
				throw std::runtime_error("model file contains invalid joint child index");
			}
			joint_ptr->children.push_back(loaded_joints[static_cast<std::size_t>(child_index)]);
		}

		if (serialized_joint.mesh_index >= 0)
		{
			std::size_t const mesh_index = static_cast<std::size_t>(serialized_joint.mesh_index);
			if (mesh_index >= loaded_meshes.size())
			{
				throw std::runtime_error("model file contains invalid joint mesh index");
			}
			joint_ptr->mesh = loaded_meshes[mesh_index];
		}
	}

	for (SerializedAnimation const &serialized_animation : serialized.animations)
	{
		AnimationPtr const animation = std::make_shared<Animation>();
		animation->name = serialized_animation.name;
		for (SerializedSampler const &serialized_sampler : serialized_animation.samplers)
		{
			if (serialized_sampler.joint_index < 0 || static_cast<std::size_t>(serialized_sampler.joint_index) >= loaded_joints.size())
			{
				throw std::runtime_error("clip '" + animation->name + "' in the model file moves a joint that is not there");
			}
			Sampler sampler;
			sampler.set_target(loaded_joints[static_cast<std::size_t>(serialized_sampler.joint_index)], serialized_sampler.sid);
			for (SerializedKeyframe const &keyframe : serialized_sampler.keyframes)
			{
				if (keyframe.values.size() != 10)
				{
					throw std::runtime_error("clip '" + animation->name + "' in the model file has a keyframe of " + std::to_string(keyframe.values.size()) +
						" numbers, not a translation, rotation and scale");
				}
				std::vector<float> const &v = keyframe.values;
				float const norm_sq = v[3] * v[3] + v[4] * v[4] + v[5] * v[5] + v[6] * v[6];
				if (!(norm_sq > rove::scalar_traits<float>::zero_length_sq) || !std::isfinite(norm_sq))
				{
					throw std::runtime_error("clip '" + animation->name + "' in the model file has a rotation keyframe at " +
						std::to_string(keyframe.time) + " that is no rotation (squared norm " + std::to_string(norm_sq) + ")");
				}
				sampler.add_sample({keyframe.time, rove::vec<3>(v[0], v[1], v[2]), rove::quaternion<>(v[3], v[4], v[5], v[6]),
					rove::vec<3>(v[7], v[8], v[9])});
			}
			animation->samplers.push_back(sampler);
		}
		for (SerializedWeightsSampler const &serialized_sampler : serialized_animation.weight_samplers)
		{
			if (serialized_sampler.mesh_index < 0 || static_cast<std::size_t>(serialized_sampler.mesh_index) >= loaded_meshes.size())
			{
				throw std::runtime_error("clip '" + animation->name + "' in the model file moves the weights of a mesh that is not there");
			}
			WeightsSampler sampler;
			sampler.set_target(loaded_meshes[static_cast<std::size_t>(serialized_sampler.mesh_index)]);
			sampler.set_interpolation(static_cast<WeightsSampler::Interpolation>(serialized_sampler.interpolation));
			for (SerializedKeyframe const &keyframe : serialized_sampler.keyframes)
			{
				sampler.add_sample(keyframe.time, keyframe.values);
			}
			animation->weight_samplers.push_back(sampler);
		}
		animations_.push_back(animation);
	}

	bind_animations();
	// A joint's matrix is its node's local transform until the hierarchy is
	// composed; draw(), the bounds and the octree read the composed one.
	rebuild_joints();
	forget_posing();
}

void Model::check()
{
	for (auto const &mesh : meshes())
	{
		mesh->check();
	}
}

JointPtr Model::joint_by_id(std::string const &id)
{
	ensure_joints();
	for (auto &joint : joints()) {
		if (joint->id == id) return joint;
	}

	throw std::runtime_error("joint with id = " + id + " not found in model");
}

JointPtr Model::joint_by_name(std::string const &name)
{
	ensure_joints();
	for (auto &joint : joints()) {
		if (joint->name == name) return joint;
	}

	throw std::runtime_error("joint with name = " + name + " not found");
}

MeshPtr Model::mesh_by_id(std::string const &id)
{
	for (auto &mesh : meshes()) {
		if (mesh->id() == id) return mesh;
	}

	throw std::runtime_error("mesh with id = " + id + " not found");
}

MeshPtr Model::mesh_by_name(std::string const &name)
{
	for (auto &mesh : meshes()) {
		if (mesh->name() == name) return mesh;
	}

	throw std::runtime_error("mesh with name = " + name + " not found");
}

MaterialPtr Model::material_by_name(std::string const &name)
{
	for (auto &material : materials()) {
		if (material->name() == name) return material;
	}

	throw std::runtime_error("material with name = " + name + " not found");
}

std::uint64_t Model::next_pose_serial()
{
	static std::atomic<std::uint64_t> last{0};
	return ++last;
}

void Model::rebuild_joints()
{
	pose_serial_ = next_pose_serial();
	if (!building_cached_pose_)
	{
		posed_by_hand_ = true;
		cached_pose_.reset();
		joints_stale_ = false;
	}
	for (auto &joint : joints()) {
		if (!joint->parent.lock()) {
			joint->build_frame_transforms();
		}
	}
}

void Model::set_cached_pose(std::size_t animation, rove::scalar time)
{
	if (posed_by_hand_)
		throw std::logic_error("Model::set_cached_pose: the model was posed by hand, its pose is not the clips'");
	if (animation >= animations().size())
		throw std::invalid_argument("Model::set_cached_pose: clip " + std::to_string(animation) + " of " + std::to_string(animations().size()));
	if (!(time >= 0 && time <= animations()[animation]->duration()))
		throw std::invalid_argument("Model::set_cached_pose: time " + std::to_string(time) + " is not in clip '" + animations()[animation]->name +
			"' (0 to " + std::to_string(animations()[animation]->duration()) + ")");
	cached_pose_ = PoseKey{std::uint32_t(animation), std::uint32_t(std::floor(double(time) * 100))};
	joints_stale_ = true;
}

void Model::ensure_joints() const
{
	if (!joints_stale_)
		return;
	Model &self = const_cast<Model &>(*this);
	self.joints_stale_ = false;
	self.building_cached_pose_ = true;
	try
	{
		self.animations()[cached_pose_->animation]->apply(cached_pose_->time());
		self.rebuild_joints();
	}
	catch (...)
	{
		self.building_cached_pose_ = false;
		self.joints_stale_ = true;
		throw;
	}
	self.building_cached_pose_ = false;
}

void Model::pose(rove::scalar t)
{
	for (auto &animation : animations())
		animation->pose(t);

	// For models with no animations, rebuild_joints() won't have been called above.
	if (animations().empty())
		rebuild_joints();
}

void Model::set_always_visible(bool flag)
{
	always_visible_ = flag;
	for (auto const &mesh : meshes())
	{
		mesh->set_always_visible(flag);
	}
}

double Model::animation_duration() const
{
	double d = 0.0;
	for (auto const &a : animations())
		d = std::max(d, static_cast<double>(a->duration()));
	return d;
}

void Model::set_materials_shader_program(ShaderProgramPtr const &program)
{
	for (auto const &material : materials())
	{
		if (material)
			material->set_shader_program(program);
	}
}

void Model::clear_materials_shader_program()
{
	for (auto const &material : materials())
	{
		if (material)
			material->set_shader_program(ShaderProgramPtr());
	}
}

void Model::set_materials_extra_parameter(size_t index, rove::vec<4> const &value)
{
	for (auto const &material : materials())
	{
		if (material)
			material->set_extra_parameter(index, value);
	}
}

/// Draws the model by submitting one draw call per (mesh × material) pair to
/// the scheduler.  Per-draw-call frustum culling happens later in
/// Scheduler::flush() — no model-level frustum test is needed here.
void Model::draw(CameraPtr const &camera, rove::matrix<4,4> const &world_matrix, unsigned lod)
{
	for (auto &joint : joints())
	{
		if (MeshPtr mesh_ptr = joint->mesh.lock())
		{
			mesh_ptr->draw(joint.get(), camera, world_matrix, lod);
		}
	}
}

void Model::draw(CameraPtr const &camera, rove::matrix<4,4> const &world_matrix, LodChooser const &chooser)
{
	ensure_joints();
	for (auto &joint : joints())
	{
		if (MeshPtr mesh_ptr = joint->mesh.lock())
		{
			mesh_ptr->draw(joint.get(), camera, world_matrix, chooser(*mesh_ptr, mesh_ptr->conservative_bounds(joints(), *joint, world_matrix)));
		}
	}
}

void Model::draw_instances(CameraPtr const &camera, std::vector<rove::matrix<4,4>> const &world_matrices, unsigned lod)
{
	for (auto &joint : joints())
	{
		if (MeshPtr mesh_ptr = joint->mesh.lock())
		{
			mesh_ptr->draw_instances(joint.get(), camera, world_matrices, lod);
		}
	}
}

void Model::reset_previous()
{
	for (MeshPtr const &mesh : meshes())
	{
		if (mesh)
			mesh->reset_previous();
	}
}

unsigned Model::lod_count() const
{
	std::size_t count = 1;
	for (MeshPtr const &mesh : meshes())
	{
		count = std::max(count, mesh->lod_count());
	}
	return static_cast<unsigned>(count);
}

float Model::lod_error(unsigned lod) const
{
	float error = 0;
	for (MeshPtr const &mesh : meshes())
	{
		error = std::max(error, mesh->lod_error(lod));
	}
	return error;
}

rove::aabb<3> Model::bounds(rove::matrix<4,4> const &world_matrix) const
{
	ensure_joints();
	rove::aabb<3> bounds;
	bounds.null();
	bool found = false;

	for (auto const &joint : joints())
	{
		if (MeshPtr mesh_ptr = joint->mesh.lock())
		{
			bounds.extend(mesh_ptr->exact_bounds(joints(), *joint, world_matrix));
			found = true;
		}
	}

	if (!found)
	{
		throw std::runtime_error("failed to compute bounds: no meshes found in model");
	}

	return bounds;
}

Model::octree_type Model::build_octree(rove::matrix<4,4> const &world_matrix) const
{
	octree_type octree(bounds(world_matrix), 16, rove::scalar(1e-5));
	int mesh_id = 0;
	for (auto const &joint : joints())
	{
		if (MeshPtr mesh_ptr = joint->mesh.lock())
		{
			mesh_ptr->insert_triangles(octree, mesh_id++, joints(), *joint, world_matrix);
		}
	}

	if (octree.size() == 0)
	{
		throw std::runtime_error("failed to build octree: no mesh triangles found in model");
	}

	octree.build();
	return octree;
}

}
