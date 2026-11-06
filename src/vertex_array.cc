// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "renderer.h"
#include "vertex_array.h"

namespace edge
{

template class VertexArray<VERTEX_PT>;
template class VertexArray<VERTEX_PNT>;
template class VertexArray<VERTEX_PTBNT>;
template class VertexArray<VERTEX_PTBNTC>;
template class VertexArray<VERTEX_PNJWT>;
template class VertexArray<VERTEX_PTBNJWT>;
template class VertexArray<VERTEX_PTBNJWTC>;

}
