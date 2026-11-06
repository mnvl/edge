// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "log.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace edge
{

namespace
{

// The shape of Python logging's
// "[%(asctime)s %(filename)s:%(lineno)d %(levelname)s] %(message)s", plus
// the thread id, so native and Python logs read alike.
constexpr char const *kPattern = "[%Y-%m-%d %H:%M:%S.%e %s:%# %l] [thread %t] %v";
constexpr size_t kMaxFileBytes = 5 * 1024 * 1024;
constexpr size_t kMaxFiles = 5;

spdlog::level::level_enum parse_level(std::string const &level)
{
	if (level == "debug") return spdlog::level::debug;
	if (level == "info") return spdlog::level::info;
	if (level == "warn" || level == "warning") return spdlog::level::warn;
	if (level == "error") return spdlog::level::err;
	throw std::invalid_argument("init_logging: level must be debug, info, warn or error, got '" + level + "'");
}

std::shared_ptr<spdlog::logger> make_logger(std::optional<std::filesystem::path> const &file,
	spdlog::level::level_enum level)
{
	std::vector<spdlog::sink_ptr> sinks;
	auto stderr_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
	stderr_sink->set_level(std::max(level, spdlog::level::warn));
	sinks.push_back(stderr_sink);
	if (file.has_value())
	{
		// Throws spdlog_ex when the file cannot be opened: a log that
		// silently goes nowhere is exactly what this must not be.
		auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(file->string(), kMaxFileBytes,
			kMaxFiles);
		file_sink->set_level(level);
		sinks.push_back(file_sink);
	}
	auto logger = std::make_shared<spdlog::logger>("edge", sinks.begin(), sinks.end());
	logger->set_level(std::min(level, spdlog::level::warn));
	logger->set_pattern(kPattern);
	// Info lines are lifecycle events (a model loaded, a renderer created),
	// rare and worth having on disk when the process dies right after;
	// debug lines are the per-mesh ones and ride along with the next flush.
	logger->flush_on(spdlog::level::info);
	return logger;
}

std::mutex &logger_mutex()
{
	static std::mutex mutex;
	return mutex;
}

std::shared_ptr<spdlog::logger> &logger_slot()
{
	static std::shared_ptr<spdlog::logger> logger;
	return logger;
}

} // namespace

std::shared_ptr<spdlog::logger> log()
{
	std::lock_guard<std::mutex> lock(logger_mutex());
	auto &slot = logger_slot();
	if (!slot) slot = make_logger(std::nullopt, spdlog::level::info);
	return slot;
}

void init_logging(std::optional<std::filesystem::path> const &file, std::string const &level)
{
	auto const parsed = parse_level(level);
	auto logger = make_logger(file, parsed);
	{
		std::lock_guard<std::mutex> lock(logger_mutex());
		logger_slot() = logger;
	}
	if (file.has_value())
	{
		EDGE_LOG_INFO("logging to {} at level {}", file->string(), level);
	}
}

} // namespace edge
