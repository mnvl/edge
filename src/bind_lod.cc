// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/stl/vector.h>

#include "lod_choice.h"

using namespace nb::literals;

void bind_lod(nb::module_ &m)
{
	// src/lod_choice.h: the level of detail by pixel error
	m.def("pixel_pitch", &edge::pixel_pitch, "projection"_a, "height_pixels"_a,
		"Metres a pixel of a picture `height_pixels` high spans one metre along the view through `projection` (a mat4, as "
		"Camera.get_projection_matrix() gives it): 2 / (|projection[1][1]| height); through an orthographic projection, at any depth. "
		"ValueError for no pixels or no vertical scale.");
	m.def("lod_level",
		[](std::vector<float> const &errors, float metres_per_pixel, float tolerance_pixels) {
			return edge::lod_level(std::span<float const>(errors), metres_per_pixel, tolerance_pixels);
		},
		"errors"_a, "metres_per_pixel"_a, "tolerance_pixels"_a,
		"The coarsest level of `errors` (each level's error in metres, level 0's 0 first, never falling: a model's lod_error(k) times "
		"its scale) whose error is at most tolerance_pixels * metres_per_pixel (pixel_pitch() times the depth, 0 at the eye). "
		"ValueError for errors that are not such a ladder, or a span or tolerance below 0 or not finite.");
}
