// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <format>
#include <functional>
#include <string_view>

namespace edge
{

// The library's one log: loaders, meshes, textures, shaders and the renderer report through it. Its lines at or above the level
// (warn until set_log_level() says otherwise) go to std::cerr as "[warning gltf_reader.cc:120] message", or to the sink a program gives
// set_log_sink() to take them into a log of its own.
//
// Use the EDGE_LOG_* macros below: they record the source file and line, and a line below the level neither formats nor evaluates its
// arguments. Arguments are std::format's: EDGE_LOG_INFO("{} x {}", w, h).
enum class LogLevel
{
	debug,
	info,
	warn,
	error,
};

void set_log_level(LogLevel level);

// `file` is the source file's name without its directory, a string that lives as long as the program. Called on whichever thread
// logs, perhaps on several at once; it must not log through edge, and must not throw (the exception would leave the EDGE_LOG_* line,
// which may be in a destructor). An empty sink (nullptr) gives the lines back to std::cerr.
using LogSink = std::function<void(LogLevel level, std::string_view message, char const *file, int line)>;
void set_log_sink(LogSink sink);

namespace detail
{
bool log_enabled(LogLevel level) noexcept;
void log_write(LogLevel level, char const *file, int line, std::string_view message);
} // namespace detail

} // namespace edge

#define EDGE_LOG_AT(level, ...) \
	do \
	{ \
		if (::edge::detail::log_enabled(level)) \
			::edge::detail::log_write(level, __FILE__, __LINE__, std::format(__VA_ARGS__)); \
	} while (false)
#define EDGE_LOG_DEBUG(...) EDGE_LOG_AT(::edge::LogLevel::debug, __VA_ARGS__)
#define EDGE_LOG_INFO(...) EDGE_LOG_AT(::edge::LogLevel::info, __VA_ARGS__)
#define EDGE_LOG_WARN(...) EDGE_LOG_AT(::edge::LogLevel::warn, __VA_ARGS__)
#define EDGE_LOG_ERROR(...) EDGE_LOG_AT(::edge::LogLevel::error, __VA_ARGS__)
