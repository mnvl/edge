// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "model_cache.h"

namespace edge
{

template <template <class> class Holder>
BasicModelCache<Holder>::BasicModelCache(std::shared_ptr<Renderer> const &renderer, GltfOptions const &gltf_options):
	renderer_(renderer),
	gltf_options_(gltf_options)
{
	if (!renderer_)
	{
		throw std::invalid_argument("ModelCache needs a renderer");
	}
}

template <template <class> class Holder>
std::size_t BasicModelCache<Holder>::prototypes_count() const
{
	if constexpr (strong)
	{
		return prototypes_.size();
	}
	else
	{
		std::size_t count = 0;
		for (auto const &entry : prototypes_)
		{
			if (!entry.second.expired())
				++count;
		}
		return count;
	}
}

template <template <class> class Holder>
std::size_t BasicModelCache<Holder>::sweep()
{
	std::size_t dropped = 0;
	for (auto it = prototypes_.begin(); it != prototypes_.end();)
	{
		bool unused;
		if constexpr (strong)
			unused = it->second.use_count() == 1;  // the cache's own
		else
			unused = it->second.expired();
		if (unused)
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

template <template <class> class Holder>
void BasicModelCache<Holder>::load_file(Model &model, std::string const &filename) const
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

template class BasicModelCache<std::weak_ptr>;
template class BasicModelCache<std::shared_ptr>;

}
