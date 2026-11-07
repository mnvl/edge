// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "log.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "static_model.h"
#include "test_fixture.h"

namespace
{

namespace fs = std::filesystem;

fs::path fresh_file(std::string const &name)
{
	return fs::temp_directory_path() / ("edge_test_log_" + name + "_" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
}

std::string slurp(fs::path const &file)
{
	std::ifstream in(file);
	std::stringstream out;
	out << in.rdbuf();
	return out.str();
}

// What the process writes to file descriptor 2 while it lives: the log's default is the process's stderr
class StderrCapture
{
public:
	explicit StderrCapture(std::string const &name): file_(fresh_file(name))
	{
		std::fflush(stderr);
		saved_ = dup(2);
		BOOST_REQUIRE(saved_ >= 0);
		std::FILE *target = std::fopen(file_.string().c_str(), "wb");
		BOOST_REQUIRE(target != nullptr);
		BOOST_REQUIRE(dup2(fileno(target), 2) >= 0);
		std::fclose(target);
	}
	StderrCapture(StderrCapture const &) = delete;
	StderrCapture &operator=(StderrCapture const &) = delete;
	~StderrCapture()
	{
		restore();
		fs::remove(file_);
	}

	std::string text()
	{
		restore();
		return slurp(file_);
	}

private:
	void restore()
	{
		if (saved_ < 0) return;
		std::fflush(stderr);
		dup2(saved_, 2);
		close(saved_);
		saved_ = -1;
	}

	fs::path file_;
	int saved_ = -1;
};

struct Received
{
	edge::LogLevel level;
	std::string message;
	std::string file;
	int line;
};

// A sink that keeps what it is given; the log is as the process starts again once the test is over
class RecordingSink
{
public:
	explicit RecordingSink(edge::LogLevel level)
	{
		edge::set_log_level(level);
		edge::set_log_sink([this](edge::LogLevel level, std::string_view message, char const *file, int line) {
			std::lock_guard<std::mutex> lock(mutex_);
			lines_.push_back({level, std::string(message), file, line});
		});
	}
	RecordingSink(RecordingSink const &) = delete;
	RecordingSink &operator=(RecordingSink const &) = delete;
	~RecordingSink()
	{
		edge::set_log_sink(nullptr);
		edge::set_log_level(edge::LogLevel::warn);
	}

	std::vector<Received> lines()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return lines_;
	}

	bool has(std::string_view message)
	{
		for (auto const &line : lines())
			if (line.message.find(message) != std::string::npos) return true;
		return false;
	}

private:
	std::mutex mutex_;
	std::vector<Received> lines_;
};

} // namespace

BOOST_AUTO_TEST_SUITE(logger)

BOOST_AUTO_TEST_CASE(stderr_gets_the_lines_at_or_above_the_level_with_their_source)
{
	edge::set_log_level(edge::LogLevel::warn);
	StderrCapture capture("default");
	EDGE_LOG_DEBUG("stderr-debug");
	EDGE_LOG_INFO("stderr-info");
	EDGE_LOG_WARN("stderr-warn {} {:.1f}", 42, 2.5);
	int const warn_line = __LINE__ - 1;
	EDGE_LOG_ERROR("stderr-error");
	std::string const text = capture.text();
	BOOST_TEST(text.find("stderr-debug") == std::string::npos, text);
	BOOST_TEST(text.find("stderr-info") == std::string::npos, text);
	BOOST_TEST(text.find("[warning test_log.cc:" + std::to_string(warn_line) + "] stderr-warn 42 2.5\n") != std::string::npos, text);
	BOOST_TEST(text.find("[error test_log.cc:") != std::string::npos, text);
	BOOST_TEST(text.find("] stderr-error\n") != std::string::npos, text);
}

BOOST_AUTO_TEST_CASE(the_level_lets_more_or_fewer_lines_through)
{
	StderrCapture capture("levels");
	edge::set_log_level(edge::LogLevel::debug);
	EDGE_LOG_DEBUG("shown-at-debug");
	edge::set_log_level(edge::LogLevel::error);
	EDGE_LOG_WARN("hidden-at-error");
	EDGE_LOG_ERROR("shown-at-error");
	edge::set_log_level(edge::LogLevel::warn);
	std::string const text = capture.text();
	BOOST_TEST(text.find("[debug test_log.cc:") != std::string::npos, text);
	BOOST_TEST(text.find("shown-at-debug") != std::string::npos, text);
	BOOST_TEST(text.find("hidden-at-error") == std::string::npos, text);
	BOOST_TEST(text.find("shown-at-error") != std::string::npos, text);
}

BOOST_AUTO_TEST_CASE(a_sink_gets_each_line_with_its_level_and_source_and_stderr_gets_none)
{
	StderrCapture capture("sink");
	std::vector<Received> lines;
	int warn_line = 0;
	{
		RecordingSink sink(edge::LogLevel::info);
		EDGE_LOG_DEBUG("sink-debug");
		EDGE_LOG_INFO("sink-info {}", 1);
		EDGE_LOG_WARN("sink-warn {}", "x");
		warn_line = __LINE__ - 1;
		EDGE_LOG_ERROR("sink-error");
		lines = sink.lines();
	}
	std::string const text = capture.text();
	BOOST_TEST(text.find("sink-") == std::string::npos, text);
	BOOST_TEST_REQUIRE(lines.size() == 3u);
	BOOST_TEST((lines[0].level == edge::LogLevel::info));
	BOOST_TEST(lines[0].message == "sink-info 1");
	BOOST_TEST((lines[1].level == edge::LogLevel::warn));
	BOOST_TEST(lines[1].message == "sink-warn x");
	BOOST_TEST(lines[1].file == "test_log.cc");
	BOOST_TEST(lines[1].line == warn_line);
	BOOST_TEST((lines[2].level == edge::LogLevel::error));
	BOOST_TEST(lines[2].message == "sink-error");
}

BOOST_AUTO_TEST_CASE(clearing_the_sink_gives_the_lines_back_to_stderr)
{
	{
		RecordingSink sink(edge::LogLevel::warn);
		EDGE_LOG_WARN("to-the-sink");
		BOOST_TEST(sink.has("to-the-sink"));
	}
	StderrCapture capture("cleared");
	EDGE_LOG_WARN("back-on-stderr");
	std::string const text = capture.text();
	BOOST_TEST(text.find("back-on-stderr") != std::string::npos, text);
}

BOOST_AUTO_TEST_CASE(a_line_below_the_level_does_not_evaluate_its_arguments)
{
	RecordingSink sink(edge::LogLevel::info);
	int evaluated = 0;
	auto const counted = [&evaluated] { return ++evaluated; };
	EDGE_LOG_DEBUG("counted {}", counted());
	BOOST_TEST(evaluated == 0);
	EDGE_LOG_INFO("counted {}", counted());
	BOOST_TEST(evaluated == 1);
	edge::set_log_level(edge::LogLevel::debug);
	EDGE_LOG_DEBUG("counted {}", counted());
	BOOST_TEST(evaluated == 2);
	BOOST_TEST(sink.lines().size() == 2u);
}

BOOST_AUTO_TEST_CASE(logging_from_many_threads_loses_no_line)
{
	RecordingSink sink(edge::LogLevel::debug);
	constexpr int kThreads = 8;
	constexpr int kLines = 200;
	std::vector<std::thread> threads;
	for (int t = 0; t < kThreads; ++t)
	{
		threads.emplace_back([t] {
			for (int i = 0; i < kLines; ++i) EDGE_LOG_DEBUG("thread {} line {}", t, i);
		});
	}
	for (auto &t : threads) t.join();
	std::vector<std::vector<int>> seen(kThreads, std::vector<int>(kLines, 0));
	for (auto const &line : sink.lines())
	{
		int t = -1, i = -1;
		BOOST_TEST_REQUIRE(std::sscanf(line.message.c_str(), "thread %d line %d", &t, &i) == 2, line.message);
		++seen.at(t).at(i);
	}
	for (int t = 0; t < kThreads; ++t)
		for (int i = 0; i < kLines; ++i) BOOST_TEST(seen[t][i] == 1, "thread " << t << " line " << i);
}

// The library's own lines go through the same log: a glTF load is told at info with the file it came from, the debug level adds the
// typed static mesh's vertex, index and bounds lines.
BOOST_AUTO_TEST_CASE(a_gltf_load_is_in_the_log)
{
	std::string const asset = edge::TestFixture::graphic_sources_path() + "test/data/test_gltf_loder_plane.glb";
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	{
		RecordingSink sink(edge::LogLevel::info);
		std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler)->load_from_gltf(asset);
		BOOST_TEST(sink.has("glTF primitive(s) from " + asset));
		BOOST_TEST(!sink.has("vertices: count = ")); // debug, off at info
		bool from_the_reader = false;
		for (auto const &line : sink.lines()) from_the_reader = from_the_reader || line.file == "gltf_reader.cc";
		BOOST_TEST(from_the_reader);
	}
	{
		RecordingSink sink(edge::LogLevel::debug);
		std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler)->load_from_gltf(asset);
		BOOST_TEST(sink.has("vertices: count = "));
		BOOST_TEST(sink.has("bounds = ("));
	}
}

BOOST_AUTO_TEST_SUITE_END()
