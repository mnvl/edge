// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <utility>

#include "forward.h"
#include "model.h"

namespace edge
{

/// Reads each model file once per renderer and hands out instances of it.  Of one thread at a time, as everything of its renderer is
/// (README: Threads): nothing here locks.
///
/// The first load of a (model type, filename) parses the file and uploads it
/// into a prototype that is never drawn; every load, the first included,
/// returns a new instance holding that prototype (Model(std::shared_ptr<Model
/// const> const &)): it shares the prototype's GPU vertex and index
/// buffers and its textures, and owns its joints, animations, materials and
/// mesh flags, so posing, relabelling or hiding one instance leaves every
/// other as it was.  The instances' skinned meshes share the poses they work out (AnimatedMeshCache), so a pose set with
/// Model::set_cached_pose() is sampled once for all of them.  A mesh with morph targets gets a vertex buffer of its
/// own, since morph weights are written into it.
///
/// Each instance holds its prototype (Model::prototype()), and so do the meshes' CPU copies of the geometry: instances share
/// those as they share the GPU buffers.  How long the cache itself holds a prototype is its Keep:
///
/// - Keep::WHILE_USED (the default): a file is read once for as long as any instance of it lives, and a prototype -- with the
///   GPU buffers and textures its instances share -- goes with its last instance; the next load() reads the file again.  So a
///   cache kept for a process holds what the process draws now, not every file it ever drew.  sweep() drops the entries of
///   released prototypes (their memory went with them; the entries are only names).
/// - Keep::ALWAYS: the cache holds them itself, and a file is read once for the cache's life whether or not an instance of it
///   lives (a scene generator places a few hundred models in a scene, lets them go and places them again in the next).
///   sweep() releases the prototypes nothing but the cache holds.
///
/// A file rewritten on disk while it is cached is not seen until clear() (or, kept while used, until its instances are gone).
/// Instances keep what they share alive, so clear() never invalidates one.
class ModelCache {
public:
	/// How long the cache holds a model it has read (above).
	enum class Keep { WHILE_USED, ALWAYS };

	/// glTF files are read with @p gltf_options; a .edge file takes from them whether its textures keep a CPU copy (the rest of
	/// what the options say it has already been settled when it was made from the glTF).
	explicit ModelCache(std::shared_ptr<Renderer> const &renderer, GltfOptions const &gltf_options = GltfOptions(),
		Keep keep = Keep::WHILE_USED);

	/// A new instance of @p filename as a model of type @p M, of this cache's
	/// renderer (it draws into any scheduler of it).  A glTF file is read with
	/// load_from_gltf(), a .edge file with load_from_file().  A file that fails
	/// to load leaves nothing cached.
	template <class M>
	std::shared_ptr<M> load(std::string const &filename);

	/// How a .edge file is read from here on: its bytes are what @p reader gives for the name load() was given, and the disk is not
	/// asked (a program that keeps its models in an archive of its own).  What the reader throws reaches load()'s caller, and nothing is
	/// cached.  A glTF is read from its file either way.  An empty reader reads the files again.
	using FileReader = std::function<std::string(std::string const &filename)>;
	void read_edge_files_with(FileReader reader) { reader_ = std::move(reader); }

	std::shared_ptr<Renderer> const &renderer() const { return renderer_; }
	Keep keep() const { return keep_; }
	/// Prototypes a load() takes without reading a file: kept while used, those some instance still holds; kept always, all.
	std::size_t prototypes_count() const;
	/// Forgets the prototypes nothing but the cache holds; returns how many.  Kept while used, they are gone already and only
	/// their entries go; kept always, they go with their entries.
	std::size_t sweep();
	void clear() { prototypes_.clear(); }

private:
	struct Entry
	{
		std::weak_ptr<Model> model;
		std::shared_ptr<Model> kept;        // Keep::ALWAYS: the model, held by the cache too
	};
	std::shared_ptr<Renderer> const renderer_;
	GltfOptions const gltf_options_;
	Keep const keep_;
	std::map<std::pair<std::type_index, std::string>, Entry> prototypes_;
	FileReader reader_;

	void load_file(Model &model, std::string const &filename) const;
};

template <class M>
std::shared_ptr<M> ModelCache::load(std::string const &filename)
{
	auto const key = std::make_pair(std::type_index(typeid(M)), filename);
	std::shared_ptr<Model> prototype;
	auto it = prototypes_.find(key);
	if (it != prototypes_.end())
		prototype = it->second.model.lock();
	if (!prototype)
	{
		auto loaded = std::make_shared<M>(renderer_);
		load_file(*loaded, filename);
		prototypes_[key] = Entry{loaded, keep_ == Keep::ALWAYS ? loaded : nullptr};
		prototype = loaded;
	}

	// The instance holds the prototype from here on (Model::prototype()).
	auto instance = std::make_shared<M>(std::static_pointer_cast<M const>(prototype));
	instance->bind_animations();
	return instance;
}

}
