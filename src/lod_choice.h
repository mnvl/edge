// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

// Which level of detail to draw a thing at, by the error its levels stray by (Model::lod_error, Mesh::lod_error, made by add_lods()):
// the coarsest level whose error, where the thing is, spans no more pixels than a tolerance.
//
// A pixel spans `metres_per_pixel` metres where the thing is: pixel_pitch(projection, height) times its depth through a perspective
// (the distance from the eye to its nearest point is a safe stand-in; 0 at the eye or inside it), and pixel_pitch(...) itself through an
// orthographic projection.  A level is chosen when its error is at most `tolerance_pixels` times that.

#include <span>

#include <rove/matrix.h>

#include "forward.h"

namespace edge
{

class Mesh;

/// How many metres a pixel of a picture `height_pixels` high spans one metre along the view from the eye, through `projection`: 2 /
/// (|projection[1][1]| height), [1][1] being 1 / tan of half a perspective's vertical field, or 2 / an orthographic view's height (a
/// turned-over view's is negative).  std::invalid_argument for no pixels, or a projection of no vertical scale or of one not a number.
float pixel_pitch(rove::matrix<4,4> const &projection, unsigned height_pixels);

/// The coarsest level of `errors` (each level's error in metres, level 0's 0 first, never falling) whose error is at most
/// `tolerance_pixels * metres_per_pixel`.  A level that strays nothing is as good as level 0, so it is chosen even at no span.
/// std::invalid_argument for errors that are none, do not begin at 0, fall or are not finite, and for a `metres_per_pixel` or
/// `tolerance_pixels` below 0 or not finite.
unsigned lod_level(std::span<float const> errors, float metres_per_pixel, float tolerance_pixels);

/// The same for a model's levels, their errors (in the model's units) times `scale`, the model's world matrix's.  std::invalid_argument
/// for a scale that is not above 0 or not finite, and as above.
unsigned lod_level(Model const &model, float scale, float metres_per_pixel, float tolerance_pixels);

/// The same for one mesh's levels (Model::draw's LodChooser gives each mesh its own).
unsigned lod_level(Mesh const &mesh, float scale, float metres_per_pixel, float tolerance_pixels);

}
