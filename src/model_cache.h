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
#include "scheduler.h"

namespace edge
{

/// Reads each model file once per renderer and hands out instances of it.  Of one thread at a time, as everything of its renderer is
/// (README: Threads): nothing here locks.
///
/// The first load of a (model type, filename) parses the file and uploads it
/// into a prototype that is never drawn; every load, the first included,
/// returns a new instance of that prototype (Model(Model const &,
/// SchedulerPtr const &)): it shares the prototype's GPU vertex and index
/// buffers and its textures, and owns its joints, animations, materials and
/// mesh flags, so posing, relabelling or hiding one instance leaves every
/// other as it was.  The instances' skinned meshes share the poses they work out (AnimatedMeshCache), so a pose set with
/// Model::set_cached_pose() is sampled once for all of them.  A mesh with morph targets gets a vertex buffer of its
/// own, since morph weights are written into it.
///
/// The file is read on the first request only: a file rewritten on disk
/// afterwards is not seen until clear().  Instances keep what they share
/// alive, so clear() never invalidates one.
class ModelCache {
public:
	/// glTF files are read with @p gltf_options; a .edge file takes from them whether its textures keep a CPU copy (the rest of
	/// what the options say it has already been settled when it was made from the glTF).
	explicit ModelCache(RendererPtr const &renderer, GltfOptions const &gltf_options = GltfOptions());

	/// A new instance of @p filename as a model of type @p M, drawing through
	/// @p scheduler, which must belong to this cache's renderer.  A glTF file
	/// is read with load_from_gltf(), a .edge file with load_from_file().
	/// A file that fails to load leaves nothing cached.
	template <class M>
	std::shared_ptr<M> load(std::string const &filename, SchedulerPtr const &scheduler);

	/// How a .edge file is read from here on: its bytes are what @p reader gives for the name load() was given, and the disk is not
	/// asked (a program that keeps its models in an archive of its own).  What the reader throws reaches load()'s caller, and nothing is
	/// cached.  A glTF is read from its file either way.  An empty reader reads the files again.
	using FileReader = std::function<std::string(std::string const &filename)>;
	void read_edge_files_with(FileReader reader) { reader_ = std::move(reader); }

	RendererPtr const &renderer() const { return renderer_; }
	std::size_t prototypes_count() const { return prototypes_.size(); }
	void clear() { prototypes_.clear(); }

private:
	RendererPtr const renderer_;
	GltfOptions const gltf_options_;
	/// The prototypes' own scheduler: they are never drawn.
	SchedulerPtr const scheduler_;
	std::map<std::pair<std::type_index, std::string>, ModelPtr> prototypes_;
	FileReader reader_;

	void check_scheduler(SchedulerPtr const &scheduler) const;
	void load_file(Model &model, std::string const &filename) const;
};

template <class M>
std::shared_ptr<M> ModelCache::load(std::string const &filename, SchedulerPtr const &scheduler)
{
	check_scheduler(scheduler);

	auto const key = std::make_pair(std::type_index(typeid(M)), filename);
	auto it = prototypes_.find(key);
	if (it == prototypes_.end())
	{
		auto prototype = std::make_shared<M>(renderer_, scheduler_);
		load_file(*prototype, filename);
		it = prototypes_.emplace(key, prototype).first;
	}

	auto instance = std::make_shared<M>(static_cast<M const &>(*it->second), scheduler);
	instance->bind_animations();
	return instance;
}

}
