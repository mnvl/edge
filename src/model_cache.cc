// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "model_cache.h"

namespace edge
{

ModelCache::ModelCache(std::shared_ptr<Renderer> const &renderer, GltfOptions const &gltf_options, Keep keep):
	renderer_(renderer),
	gltf_options_(gltf_options),
	keep_(keep)
{
	if (!renderer_)
	{
		throw std::invalid_argument("ModelCache needs a renderer");
	}
}

std::size_t ModelCache::prototypes_count() const
{
	std::size_t count = 0;
	for (auto const &entry : prototypes_)
	{
		if (!entry.second.model.expired())
			++count;
	}
	return count;
}

std::size_t ModelCache::sweep()
{
	std::size_t dropped = 0;
	for (auto it = prototypes_.begin(); it != prototypes_.end();)
	{
		// kept always: let go of a model only the cache holds, which then goes as one kept while used goes with its last instance
		if (it->second.kept && it->second.kept.use_count() == 1)
			it->second.kept.reset();
		if (it->second.model.expired())
		{
			it = prototypes_.erase(it);
			++dropped;
		}
		else
		{
			++it;
		}
	}
	return dropped;
}

void ModelCache::load_file(Model &model, std::string const &filename) const
{
	std::string const edge_suffix = ".edge";
	if (filename.size() >= edge_suffix.size() &&
		filename.compare(filename.size() - edge_suffix.size(), edge_suffix.size(), edge_suffix) == 0)
	{
		if (reader_)
			model.load_from_bytes(reader_(filename), filename, gltf_options_.keep_cpu_copy);
		else
			model.load_from_file(filename, gltf_options_.keep_cpu_copy);
	}
	else
	{
		model.load_from_gltf(filename, gltf_options_);
	}
}

}
