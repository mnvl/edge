// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "gltf_reader.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <variant>

#include "log.h"

namespace edge
{

fastgltf::Extensions gltf_extensions()
{
	return fastgltf::Extensions::KHR_texture_transform
		| fastgltf::Extensions::KHR_materials_emissive_strength
		| fastgltf::Extensions::MSFT_texture_dds;
}

std::vector<std::filesystem::path> gltf_named_files(fastgltf::Asset const &asset, std::string const &filename)
{
	std::filesystem::path const gltf = std::filesystem::absolute(filename).lexically_normal();
	std::vector<std::filesystem::path> files = {gltf};
	auto const named = [&](fastgltf::DataSource const &data, std::string const &what) {
		auto const *uri = std::get_if<fastgltf::sources::URI>(&data);
		if (!uri)
			return;     // in the file: a .glb's chunk, a data: URI the parser decoded, a buffer view
		if (!uri->uri.isLocalPath() || uri->uri.fspath().empty())
			throw GltfLoader::load_error("glTF '" + filename + "' " + what + ": its URI '" + std::string(uri->uri.string()) + "' is not a file");
		std::filesystem::path path = uri->uri.fspath();
		path = (path.is_relative() ? gltf.parent_path() / path : path).lexically_normal();
		if (!std::filesystem::is_regular_file(path))
			throw GltfLoader::load_error("glTF '" + filename + "' " + what + ": " + path.string() + " is not there");
		if (std::find(files.begin(), files.end(), path) == files.end())
			files.push_back(path);
	};
	for (std::size_t i = 0; i < asset.buffers.size(); ++i)
		named(asset.buffers[i].data, "buffer " + std::to_string(i));
	for (std::size_t i = 0; i < asset.images.size(); ++i)
		named(asset.images[i].data, "image " + std::to_string(i));
	return files;
}

fastgltf::Asset read_gltf_json(std::string const &filename)
{
	if (!std::filesystem::is_regular_file(filename))
	{
		throw GltfLoader::load_error("glTF '" + filename + "' is not there");
	}
	auto data = fastgltf::GltfDataBuffer::FromPath(std::filesystem::path(filename));
	if (data.error() != fastgltf::Error::None)
	{
		throw GltfLoader::load_error("glTF '" + filename + "' could not be read: " + std::string(fastgltf::getErrorMessage(data.error())));
	}
	fastgltf::Parser parser(gltf_extensions());
	// (the directory of "robot.glb" is the working directory, which fastgltf only takes by name)
	std::filesystem::path const directory = std::filesystem::path(filename).parent_path();
	fastgltf::Expected<fastgltf::Asset> asset = parser.loadGltf(data.get(), directory.empty() ? std::filesystem::path(".") : directory, fastgltf::Options::None);
	if (asset.error() != fastgltf::Error::None)
	{
		throw GltfLoader::load_error("glTF '" + filename + "' could not be parsed: " + std::string(fastgltf::getErrorMessage(asset.error())));
	}
	return std::move(asset.get());
}

GltfReader::GltfReader(std::string const &filename, GltfOptions const &options, std::string const &mesh_type):
	filename_(filename),
	options_(options),
	mesh_type_(mesh_type),
	basepath_(std::filesystem::path(filename).parent_path().empty() ? std::filesystem::path(".") : std::filesystem::path(filename).parent_path())
{
	if (options_.mipmaps && options_.compress_textures)
	{
		throw std::invalid_argument("glTF options: mipmaps need compress_textures = false");
	}
	if (!(options_.pose_margin >= 0))
	{
		throw std::invalid_argument("glTF options: pose_margin " + std::to_string(options_.pose_margin) + " is negative");
	}
}

SerializedModel GltfReader::read()
{
	if (read_)
	{
		throw std::logic_error("GltfReader::read: a reader reads its file once");
	}
	read_ = true;
	parse();
	check_accessors();
	select_imported_nodes();
	build_joints();
	link_joint_hierarchy();
	build_materials();

	std::size_t const imported_primitives = build_meshes_for_nodes();
	if (imported_primitives == 0)
	{
		throw load_error("glTF file does not contain any mesh primitives that could be imported");
	}

	build_animations();

	EDGE_LOG_INFO("imported {} glTF primitive(s) from {}", imported_primitives, filename_);
	return std::move(model_);
}

void GltfReader::parse()
{
	if (!std::filesystem::is_regular_file(filename_))
	{
		throw load_error("glTF '" + filename_ + "' is not there");
	}
	auto data = fastgltf::GltfDataBuffer::FromPath(std::filesystem::path(filename_));
	if (data.error() != fastgltf::Error::None)
	{
		throw load_error("glTF '" + filename_ + "' could not be read: " + std::string(fastgltf::getErrorMessage(data.error())));
	}

	fastgltf::Parser parser(gltf_extensions());
	fastgltf::Options const options = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::GenerateMeshIndices;
	fastgltf::Expected<fastgltf::Asset> asset = parser.loadGltf(data.get(), basepath_, options);
	if (asset.error() != fastgltf::Error::None)
	{
		// a buffer file that is not there is named (the parser's message does not say which)
		if (asset.error() == fastgltf::Error::MissingExternalBuffer)
			gltf_named_files(read_gltf_json(filename_), filename_);
		throw load_error("glTF '" + filename_ + "' could not be parsed: " + std::string(fastgltf::getErrorMessage(asset.error())));
	}
	asset_ = std::move(asset.get());
}

void GltfReader::check_index(std::size_t index, std::size_t count, char const *what) const
{
	if (index >= count)
	{
		throw load_error("glTF '" + filename_ + "' names " + what + " " + std::to_string(index) + ", which is not there (" +
			std::to_string(count) + ")");
	}
}

void GltfReader::mark_node_recursive(std::size_t node_index)
{
	check_index(node_index, imported_nodes_.size(), "node");
	if (imported_nodes_[node_index])
	{
		return;
	}

	imported_nodes_[node_index] = true;
	for (std::size_t child_index : asset_.nodes[node_index].children)
	{
		mark_node_recursive(child_index);
	}
}

void GltfReader::select_imported_nodes()
{
	imported_nodes_.assign(asset_.nodes.size(), false);
	if (asset_.nodes.empty())
	{
		return;
	}

	auto import_scene = [&](std::size_t scene_index)
	{
		check_index(scene_index, asset_.scenes.size(), "scene");

		for (std::size_t root_node_index : asset_.scenes[scene_index].nodeIndices)
		{
			mark_node_recursive(root_node_index);
		}
	};

	if (!asset_.scenes.empty())
	{
		if (asset_.defaultScene)
		{
			import_scene(static_cast<std::size_t>(*asset_.defaultScene));
		}
		else
		{
			for (std::size_t scene_index = 0; scene_index < asset_.scenes.size(); ++scene_index)
			{
				import_scene(scene_index);
			}
		}
	}
	else
	{
		for (std::size_t node_index = 0; node_index < asset_.nodes.size(); ++node_index)
		{
			mark_node_recursive(node_index);
		}
	}

	std::vector<std::size_t> parent_nodes(asset_.nodes.size(), std::numeric_limits<std::size_t>::max());
	for (std::size_t node_index = 0; node_index < asset_.nodes.size(); ++node_index)
	{
		for (std::size_t child_index : asset_.nodes[node_index].children)
		{
			if (child_index < parent_nodes.size() && parent_nodes[child_index] == std::numeric_limits<std::size_t>::max())
			{
				parent_nodes[child_index] = node_index;
			}
		}
	}

	auto mark_node_and_ancestors = [&](std::size_t node_index)
	{
		check_index(node_index, asset_.nodes.size(), "node");

		mark_node_recursive(node_index);

		std::size_t current = node_index;
		while (current < parent_nodes.size())
		{
			std::size_t const parent = parent_nodes[current];
			if (parent == std::numeric_limits<std::size_t>::max())
			{
				break;
			}

			imported_nodes_[parent] = true;
			current = parent;
		}
	};

	for (std::size_t node_index = 0; node_index < asset_.nodes.size(); ++node_index)
	{
		if (!imported_nodes_[node_index])
		{
			continue;
		}

		fastgltf::Node const &node = asset_.nodes[node_index];
		if (!node.skinIndex)
		{
			continue;
		}

		std::size_t const skin_index = static_cast<std::size_t>(*node.skinIndex);
		check_index(skin_index, asset_.skins.size(), "skin");

		fastgltf::Skin const &skin = asset_.skins[skin_index];
		for (std::size_t joint_node_index : skin.joints)
		{
			mark_node_and_ancestors(joint_node_index);
		}

		if (skin.skeleton)
		{
			mark_node_and_ancestors(static_cast<std::size_t>(*skin.skeleton));
		}
	}

	if (std::find(imported_nodes_.begin(), imported_nodes_.end(), true) == imported_nodes_.end())
	{
		throw load_error("glTF '" + filename_ + "': the scene it shows has no nodes");
	}
}

void GltfReader::build_joints()
{
	node_to_joint_.assign(asset_.nodes.size(), -1);

	for (std::size_t node_index = 0; node_index < asset_.nodes.size(); ++node_index)
	{
		if (!imported_nodes_[node_index])
		{
			continue;
		}

		fastgltf::Node const &node = asset_.nodes[node_index];

		SerializedJoint joint;
		joint.index = model_.joints.size();
		joint.id = "gltf_node_" + std::to_string(node_index);
		joint.name = node.name.empty() ? joint.id : std::string(node.name.c_str());
		rove::matrix<4,4> const local_matrix = convert_matrix(fastgltf::getTransformMatrix(node));
		joint.transforms.push_back(SerializedJointTransform{"gltf_node_transform", local_matrix});
		joint.matrix = local_matrix;

		node_to_joint_[node_index] = std::int64_t(model_.joints.size());
		model_.joints.push_back(std::move(joint));
	}
}

void GltfReader::link_joint_hierarchy()
{
	for (std::size_t parent_node_index = 0; parent_node_index < asset_.nodes.size(); ++parent_node_index)
	{
		if (!imported_nodes_[parent_node_index])
		{
			continue;
		}

		std::int64_t const parent = node_to_joint_[parent_node_index];
		for (std::size_t child_node_index : asset_.nodes[parent_node_index].children)
		{
			// (an imported node's children are imported: mark_node_recursive)
			std::int64_t const child = node_to_joint_.at(child_node_index);
			SerializedJoint &child_joint = model_.joints[std::size_t(child)];
			if (child_joint.parent_index >= 0)
			{
				throw load_error("glTF '" + filename_ + "': node " + std::to_string(child_node_index) + " is the child of two nodes");
			}

			child_joint.parent_index = parent;
			model_.joints[std::size_t(parent)].child_indices.push_back(child);
		}
	}
}

GltfReader::PreparedSkin const *GltfReader::prepare_skin(std::size_t skin_index)
{
	std::string const context = "glTF '" + filename_ + "' skin " + std::to_string(skin_index);
	if (skin_index >= asset_.skins.size())
	{
		throw load_error(context + ": no such skin (" + std::to_string(asset_.skins.size()) + " skins)");
	}

	if (skin_prepared_[skin_index])
	{
		return &skin_cache_[skin_index];
	}

	fastgltf::Skin const &skin = asset_.skins[skin_index];
	if (skin.joints.empty())
	{
		throw load_error(context + ": no joints");
	}
	// a vertex names its joints in a byte, and a draw has MAX_JOINT_MATRICES of them
	if (skin.joints.size() > MAX_JOINT_MATRICES)
	{
		throw load_error(context + ": " + std::to_string(skin.joints.size()) + " joints, a draw has matrices for " +
			std::to_string(MAX_JOINT_MATRICES));
	}

	PreparedSkin prepared_skin;
	{
		rove::matrix<4,4> identity_matrix;
		identity_matrix.identity();
		prepared_skin.inverted_bind_matrices.assign(model_.joints.size(), identity_matrix);
	}

	prepared_skin.joint_index_transform.reserve(skin.joints.size());
	for (std::size_t skin_joint_index = 0; skin_joint_index < skin.joints.size(); ++skin_joint_index)
	{
		std::size_t const joint_node_index = skin.joints[skin_joint_index];
		if (joint_node_index >= node_to_joint_.size() || node_to_joint_[joint_node_index] < 0)
		{
			throw load_error(context + ": joint " + std::to_string(skin_joint_index) + " is node " + std::to_string(joint_node_index) +
				", which is not a joint of the model");
		}

		prepared_skin.joint_index_transform.push_back(model_.joints[std::size_t(node_to_joint_[joint_node_index])].index);
	}

	// (none: identities, as glTF has it)
	if (skin.inverseBindMatrices)
	{
		std::size_t const accessor_index = static_cast<std::size_t>(*skin.inverseBindMatrices);
		if (accessor_index >= asset_.accessors.size())
		{
			throw load_error(context + ": no accessor " + std::to_string(accessor_index) + " for its inverse bind matrices");
		}
		fastgltf::Accessor const &accessor = asset_.accessors[accessor_index];
		if (accessor.count != skin.joints.size())
		{
			throw load_error(context + ": " + std::to_string(accessor.count) + " inverse bind matrices for " +
				std::to_string(skin.joints.size()) + " joints");
		}
		read_accessor_with_index<fastgltf::math::fmat4x4>(accessor,
			[&](fastgltf::math::fmat4x4 const &inv_bind_matrix, std::size_t skin_joint_index)
			{
				prepared_skin.inverted_bind_matrices[prepared_skin.joint_index_transform[skin_joint_index]] = convert_matrix(inv_bind_matrix);
			});
	}

	skin_cache_[skin_index] = std::move(prepared_skin);
	skin_prepared_[skin_index] = true;
	return &skin_cache_[skin_index];
}

void GltfReader::check_accessors()
{
	for (std::size_t index = 0; index < asset_.accessors.size(); ++index)
	{
		fastgltf::Accessor const &accessor = asset_.accessors[index];
		std::string const name = "accessor " + std::to_string(index);
		// What is read is count elements of the file's own size, stride apart, from byteOffset of its buffer view: all of them have to be there.
		auto const check_view = [&](std::size_t view_index, std::size_t offset, std::size_t count, std::size_t element_size, std::size_t forced_stride) {
			fastgltf::span<const std::byte> const view = buffer_view_data(view_index);
			std::size_t const stride = forced_stride ? forced_stride :
				asset_.bufferViews[view_index].byteStride ? *asset_.bufferViews[view_index].byteStride : element_size;
			if (element_size == 0 || (count > 0 && stride < element_size))
				throw load_error("glTF '" + filename_ + "' " + name + ": elements of " + std::to_string(element_size) + " bytes, " + std::to_string(stride) + " apart");
			if (count == 0) return;
			// (from the end, so that neither product nor sum can wrap)
			if (offset > view.size() || element_size > view.size() - offset || count - 1 > (view.size() - offset - element_size) / stride)
				throw load_error("glTF '" + filename_ + "' " + name + ": " + std::to_string(count) + " elements of " + std::to_string(element_size) +
					" bytes from " + std::to_string(offset) + " do not fit a buffer view of " + std::to_string(view.size()));
		};
		std::size_t const element_size = fastgltf::getElementByteSize(accessor.type, accessor.componentType);
		if (accessor.bufferViewIndex)
			check_view(*accessor.bufferViewIndex, accessor.byteOffset, accessor.count, element_size, 0);
		if (accessor.sparse)
		{
			fastgltf::SparseAccessor const &sparse = *accessor.sparse;
			if (sparse.count > accessor.count)
				throw load_error("glTF '" + filename_ + "' " + name + ": " + std::to_string(sparse.count) + " sparse values of " + std::to_string(accessor.count));
			check_view(sparse.indicesBufferView, sparse.indicesByteOffset, sparse.count, fastgltf::getComponentByteSize(sparse.indexComponentType),
				fastgltf::getComponentByteSize(sparse.indexComponentType));
			check_view(sparse.valuesBufferView, sparse.valuesByteOffset, sparse.count, element_size, element_size);
		}
	}
}

fastgltf::span<const std::byte> GltfReader::buffer_data(std::size_t buffer_index)
{
	check_index(buffer_index, asset_.buffers.size(), "buffer");
	fastgltf::Buffer const &buffer = asset_.buffers[buffer_index];
	return std::visit(fastgltf::visitor {
		[&](fastgltf::sources::Array const &array) -> fastgltf::span<const std::byte>
		{
			return fastgltf::span<const std::byte>(array.bytes.data(), array.bytes.size_bytes());
		},
		[&](fastgltf::sources::Vector const &vector) -> fastgltf::span<const std::byte>
		{
			return fastgltf::span<const std::byte>(vector.bytes.data(), vector.bytes.size());
		},
		[&](fastgltf::sources::ByteView const &byte_view) -> fastgltf::span<const std::byte>
		{
			return byte_view.bytes;
		},
		[&](auto const &) -> fastgltf::span<const std::byte>
		{
			throw load_error("glTF '" + filename_ + "' buffer " + std::to_string(buffer_index) + ": a source edge cannot read");
		}
	}, buffer.data);
}

fastgltf::span<const std::byte> GltfReader::buffer_view_data(std::size_t buffer_view_index)
{
	check_index(buffer_view_index, asset_.bufferViews.size(), "buffer view");
	fastgltf::BufferView const &buffer_view = asset_.bufferViews[buffer_view_index];
	fastgltf::span<const std::byte> const buffer_data = this->buffer_data(buffer_view.bufferIndex);
	if (buffer_view.byteOffset > buffer_data.size() || buffer_view.byteLength > buffer_data.size() - buffer_view.byteOffset)
	{
		throw load_error("glTF '" + filename_ + "' buffer view " + std::to_string(buffer_view_index) + ": bytes " +
			std::to_string(buffer_view.byteOffset) + " + " + std::to_string(buffer_view.byteLength) + " of a buffer of " +
			std::to_string(buffer_data.size()));
	}
	return buffer_data.subspan(buffer_view.byteOffset, buffer_view.byteLength);
}

}
