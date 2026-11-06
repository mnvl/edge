// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

namespace edge
{

/// How often a vertex or index buffer is written: once (STATIC), or again and again (DYNAMIC, a hint the GLES backend passes on).
enum class BufferUsage
{
	STATIC,
	DYNAMIC,
};

}
