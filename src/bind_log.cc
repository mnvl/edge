// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "python_bindings.h"

#include <stdexcept>
#include <string>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include "log.h"

using namespace nb::literals;

namespace
{

edge::LogLevel parse_level(std::string const &level)
{
	if (level == "debug") return edge::LogLevel::debug;
	if (level == "info") return edge::LogLevel::info;
	if (level == "warn" || level == "warning") return edge::LogLevel::warn;
	if (level == "error") return edge::LogLevel::error;
	throw std::invalid_argument("set_log_level: level must be debug, info, warn or error, got '" + level + "'");
}

} // namespace

void bind_log(nb::module_ &m)
{
	// src/log.h: the native side's lines go to stderr
	m.def(
		"set_log_level", [](std::string const &level) { edge::set_log_level(parse_level(level)); }, "level"_a,
		"Let the native edge log's lines at or above `level` ('debug', 'info', 'warn', 'error') through to stderr; "
		"'warn' until this is called. A level outside those is a ValueError, and leaves the level as it was.");
}
