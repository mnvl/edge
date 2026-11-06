// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <spdlog/spdlog.h>

namespace edge
{

// The library's one logger.
// Loaders, meshes, textures, shaders and the renderer report through it,
// so logs/edge.log is one time-ordered account of what the native side
// did. Warnings and errors always also go to stderr, where the Python log
// already is; the file gets everything at or above the init_logging()
// level (default: nothing on disk until init_logging() runs, which
// edge/pyedge/__init__.py does at import with <repo>/logs/edge.log).
//
// Use the EDGE_LOG_* macros below rather than log() directly: they record
// the source file and line, and an EDGE_LOG_DEBUG whose level is off
// formats nothing. Arguments are fmt-style: EDGE_LOG_INFO("{} x {}", w, h).
std::shared_ptr<spdlog::logger> log();

// Points the logger at `file` (a rotating log, 5 MB x 5, its directory
// created as needed) at `level`: "debug", "info", "warn"/"warning" or
// "error"; anything else, "off" included, is an error, not a fallback.
// std::nullopt keeps warnings and errors on stderr and writes no file.
// Callable again to move the log; threads already
// running pick up the new destination on their next line.
void init_logging(std::optional<std::filesystem::path> const &file, std::string const &level = "info");

} // namespace edge

#define EDGE_LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(::edge::log(), __VA_ARGS__)
#define EDGE_LOG_INFO(...) SPDLOG_LOGGER_INFO(::edge::log(), __VA_ARGS__)
#define EDGE_LOG_WARN(...) SPDLOG_LOGGER_WARN(::edge::log(), __VA_ARGS__)
#define EDGE_LOG_ERROR(...) SPDLOG_LOGGER_ERROR(::edge::log(), __VA_ARGS__)
