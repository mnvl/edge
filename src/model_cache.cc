// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "model_cache.h"

namespace edge
{

ModelCache::ModelCache(RendererPtr const &renderer, GltfOptions const &gltf_options):
	renderer_(renderer),
	gltf_options_(gltf_options),
	scheduler_(renderer ? std::make_shared<Scheduler>(renderer) : SchedulerPtr())
{
	if (!renderer_)
	{
		throw std::invalid_argument("ModelCache needs a renderer");
	}
}

void ModelCache::check_scheduler(SchedulerPtr const &scheduler) const
{
	if (!scheduler)
	{
		throw std::invalid_argument("ModelCache::load needs a scheduler");
	}
	// An instance draws the prototype's buffers, which live on this cache's
	// renderer; submitted to another one they are someone else's memory.
	if (scheduler->renderer() != renderer_)
	{
		throw std::invalid_argument("ModelCache::load: the scheduler belongs to another renderer than the cache");
	}
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
