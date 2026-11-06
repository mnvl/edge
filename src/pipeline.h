// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

namespace edge
{

/// What made this edge's .edge files, as 16 hexadecimal digits: the hash of the sources that decide what a file holds (the loader, the
/// model and its meshes, the converter's steps, the format, rove, the pins of the libraries fetched; not the GPU backends or the
/// bindings), worked out at build time (etc/cmake/edge_pipeline.cmake).  The same sources give the same hash on every build.  A .edge file
/// keeps it (SerializedModel::pipeline), edge_convert --version says it: a build remakes a model when it is not the file's.
char const *pipeline_hash();

}
