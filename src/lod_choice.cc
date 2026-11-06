// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "lod_choice.h"

#include <cmath>
#include <stdexcept>
#include <string>

#include "mesh.h"
#include "model.h"

namespace edge
{

float pixel_pitch(rove::matrix<4,4> const &projection, unsigned height_pixels)
{
	float const vertical = std::fabs(projection[1][1]);
	if (height_pixels == 0)
		throw std::invalid_argument("pixel_pitch: a picture of no pixels");
	if (!(vertical > 0.0f) || !std::isfinite(vertical))
		throw std::invalid_argument("pixel_pitch: a projection of no vertical scale ([1][1] is " + std::to_string(projection[1][1]) + ")");
	return 2.0f / (vertical * static_cast<float>(height_pixels));
}

namespace
{

void check_span(float metres_per_pixel, float tolerance_pixels)
{
	if (!(metres_per_pixel >= 0.0f) || !std::isfinite(metres_per_pixel))
		throw std::invalid_argument("lod_level: metres a pixel spans of " + std::to_string(metres_per_pixel) + ", not 0 or more");
	if (!(tolerance_pixels >= 0.0f) || !std::isfinite(tolerance_pixels))
		throw std::invalid_argument("lod_level: a tolerance of " + std::to_string(tolerance_pixels) + " pixels, not 0 or more");
}

void check_scale(float scale)
{
	if (!(scale > 0.0f) || !std::isfinite(scale))
		throw std::invalid_argument("lod_level: a scale of " + std::to_string(scale) + ", not above 0");
}

/// The level of `count` errors read by `error(k)`, checked as lod_level(span) says.
template <class Error>
unsigned choose(std::size_t count, Error const &error, float metres_per_pixel, float tolerance_pixels)
{
	check_span(metres_per_pixel, tolerance_pixels);
	if (count == 0)
		throw std::invalid_argument("lod_level: no levels");
	if (error(0) != 0.0f)
		throw std::invalid_argument("lod_level: level 0 strays by " + std::to_string(error(0)) + ", not 0");
	float const budget = tolerance_pixels * metres_per_pixel;
	unsigned level = 0;
	for (std::size_t k = 1; k < count; ++k)
	{
		float const e = error(k);
		if (!std::isfinite(e) || !(e >= error(k - 1)))
			throw std::invalid_argument("lod_level: the error of level " + std::to_string(k) + " is " + std::to_string(e) +
				", not a number at least level " + std::to_string(k - 1) + "'s");
		if (e <= budget)
			level = static_cast<unsigned>(k);
	}
	return level;
}

}

unsigned lod_level(std::span<float const> errors, float metres_per_pixel, float tolerance_pixels)
{
	return choose(errors.size(), [&](std::size_t k) { return errors[k]; }, metres_per_pixel, tolerance_pixels);
}

unsigned lod_level(Model const &model, float scale, float metres_per_pixel, float tolerance_pixels)
{
	check_scale(scale);
	return choose(model.lod_count(), [&](std::size_t k) { return model.lod_error(static_cast<unsigned>(k)) * scale; },
		metres_per_pixel, tolerance_pixels);
}

unsigned lod_level(Mesh const &mesh, float scale, float metres_per_pixel, float tolerance_pixels)
{
	check_scale(scale);
	return choose(mesh.lod_count(), [&](std::size_t k) { return mesh.lod_error(static_cast<unsigned>(k)) * scale; },
		metres_per_pixel, tolerance_pixels);
}

}
