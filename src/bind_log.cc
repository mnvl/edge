// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <filesystem>
#include <optional>
#include <string>

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>

#include "log.h"

using namespace nb::literals;

void bind_log(nb::module_ &m)
{
	// src/log.h: where the native side's log goes. pyedge/__init__.py
	// calls this at import with $EDGE_LOG (None when unset); a caller may
	// call it again to move the file or turn on debug lines.
	m.def(
		"init_logging",
		[](std::optional<std::string> const &file, std::string const &level) {
			std::optional<std::filesystem::path> path;
			if (file.has_value()) path = std::filesystem::path(*file);
			edge::init_logging(path, level);
		},
		"file"_a.none(), "level"_a = "info",
		"Write the native edge log to `file` (rotating, its directory created as needed) at `level` "
		"('debug', 'info', 'warn', 'error'); None keeps warnings and errors on stderr only. "
		"A level outside those is a ValueError.");
}
