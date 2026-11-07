// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "log.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace edge
{

namespace
{

std::atomic<LogLevel> level{LogLevel::warn};

// (made at the first use and never destroyed: a line logged from a static's constructor or destructor finds them there)
std::mutex &sink_mutex()
{
	static std::mutex *const mutex = new std::mutex();
	return *mutex;
}

std::shared_ptr<LogSink const> &sink()
{
	static auto *const sink = new std::shared_ptr<LogSink const>();
	return *sink;
}

char const *level_name(LogLevel level)
{
	switch (level)
	{
	case LogLevel::debug: return "debug";
	case LogLevel::info: return "info";
	case LogLevel::warn: return "warning";
	case LogLevel::error: return "error";
	}
	throw std::logic_error("edge log: a level that is none of debug, info, warn and error");
}

char const *base_name(char const *file)
{
	char const *base = file;
	for (char const *c = file; *c != '\0'; ++c)
		if (*c == '/' || *c == '\\') base = c + 1;
	return base;
}

} // namespace

void set_log_level(LogLevel new_level)
{
	level.store(new_level, std::memory_order_relaxed);
}

void set_log_sink(LogSink new_sink)
{
	auto shared = new_sink ? std::make_shared<LogSink const>(std::move(new_sink)) : nullptr;
	std::lock_guard<std::mutex> lock(sink_mutex());
	sink() = std::move(shared);
}

namespace detail
{

bool log_enabled(LogLevel line_level) noexcept
{
	return line_level >= level.load(std::memory_order_relaxed);
}

void log_write(LogLevel line_level, char const *file, int line, std::string_view message)
{
	char const *const name = base_name(file);
	std::shared_ptr<LogSink const> current;
	{
		std::lock_guard<std::mutex> lock(sink_mutex());
		current = sink();
	}
	if (current)
		(*current)(line_level, message, name, line);
	else
		std::cerr << std::format("[{} {}:{}] {}\n", level_name(line_level), name, line, message) << std::flush;
}

} // namespace detail

} // namespace edge
