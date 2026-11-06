// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include <boost/test/unit_test.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "log.h"
#include "null_renderer.h"
#include "scheduler.h"
#include "static_model.h"
#include "test_fixture.h"

namespace
{

namespace fs = std::filesystem;

fs::path fresh_dir(std::string const &name)
{
	auto dir = fs::temp_directory_path() / ("mr_edge_test_log_" + name + "_" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::remove_all(dir);
	return dir;
}

std::string slurp(fs::path const &file)
{
	std::ifstream in(file);
	std::stringstream out;
	out << in.rdbuf();
	return out.str();
}

} // namespace

BOOST_AUTO_TEST_SUITE(logger)

BOOST_AUTO_TEST_CASE(init_logging_writes_the_file_with_the_source_location_and_level)
{
	auto dir = fresh_dir("basic");
	auto file = dir / "nested" / "edge.log"; // the directory does not exist yet
	edge::init_logging(file, "info");
	EDGE_LOG_INFO("hello {} {}", 42, "world");
	EDGE_LOG_WARN("careful");

	BOOST_TEST(fs::exists(file));
	std::string text = slurp(file);
	BOOST_TEST(text.find("hello 42 world") != std::string::npos, text);
	BOOST_TEST(text.find("careful") != std::string::npos, text);
	BOOST_TEST(text.find("test_log.cc:") != std::string::npos, text);
	BOOST_TEST(text.find(" info]") != std::string::npos, text);
	BOOST_TEST(text.find(" warning]") != std::string::npos, text);
	edge::init_logging(std::nullopt);
	fs::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(level_filters_the_file_and_debug_is_available_when_asked)
{
	auto dir = fresh_dir("level");
	auto file = dir / "edge.log";
	edge::init_logging(file, "info");
	EDGE_LOG_DEBUG("hidden-at-info");
	EDGE_LOG_INFO("shown-at-info");
	std::string text = slurp(file);
	BOOST_TEST(text.find("hidden-at-info") == std::string::npos, text);
	BOOST_TEST(text.find("shown-at-info") != std::string::npos, text);

	edge::init_logging(file, "debug");
	EDGE_LOG_DEBUG("shown-at-debug");
	EDGE_LOG_INFO("flush-me");
	text = slurp(file);
	BOOST_TEST(text.find("shown-at-debug") != std::string::npos, text);

	edge::init_logging(file, "error");
	EDGE_LOG_WARN("hidden-at-error");
	EDGE_LOG_ERROR("shown-at-error");
	text = slurp(file);
	BOOST_TEST(text.find("hidden-at-error") == std::string::npos, text);
	BOOST_TEST(text.find("shown-at-error") != std::string::npos, text);
	edge::init_logging(std::nullopt);
	fs::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(an_unknown_level_is_refused_not_defaulted)
{
	auto dir = fresh_dir("badlevel");
	BOOST_CHECK_THROW(edge::init_logging(dir / "edge.log", "loud"), std::invalid_argument);
	BOOST_CHECK_THROW(edge::init_logging(dir / "edge.log", ""), std::invalid_argument);
	BOOST_CHECK_THROW(edge::init_logging(dir / "edge.log", "off"), std::invalid_argument);
	BOOST_CHECK_THROW(edge::init_logging(dir / "edge.log", "INFO"), std::invalid_argument);
	BOOST_TEST(!fs::exists(dir / "edge.log"));
	fs::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(a_file_that_cannot_be_created_is_an_error)
{
	auto dir = fresh_dir("unwritable");
	fs::create_directories(dir);
	std::ofstream(dir / "not_a_dir").put('x');
	BOOST_CHECK_THROW(edge::init_logging(dir / "not_a_dir" / "edge.log", "info"), std::exception);
	fs::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(a_failed_init_leaves_the_previous_destination_in_place)
{
	auto dir = fresh_dir("keep");
	edge::init_logging(dir / "kept.log", "info");
	BOOST_CHECK_THROW(edge::init_logging(dir / "kept.log", "loud"), std::invalid_argument);
	EDGE_LOG_INFO("still-here");
	BOOST_TEST(slurp(dir / "kept.log").find("still-here") != std::string::npos);
	edge::init_logging(std::nullopt);
	fs::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(reinit_moves_later_lines_to_the_new_file)
{
	auto dir = fresh_dir("move");
	edge::init_logging(dir / "first.log", "info");
	EDGE_LOG_INFO("line-one");
	edge::init_logging(dir / "second.log", "info");
	EDGE_LOG_INFO("line-two");
	std::string first = slurp(dir / "first.log");
	std::string second = slurp(dir / "second.log");
	BOOST_TEST(first.find("line-one") != std::string::npos, first);
	BOOST_TEST(first.find("line-two") == std::string::npos, first);
	BOOST_TEST(second.find("line-two") != std::string::npos, second);
	BOOST_TEST(second.find("line-one") == std::string::npos, second);
	edge::init_logging(std::nullopt);
	fs::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(init_without_a_file_keeps_logging_off_disk)
{
	auto dir = fresh_dir("nofile");
	edge::init_logging(dir / "edge.log", "info");
	edge::init_logging(std::nullopt, "info");
	EDGE_LOG_INFO("after-detach");
	EDGE_LOG_WARN("after-detach-warn");
	BOOST_TEST(slurp(dir / "edge.log").find("after-detach") == std::string::npos);
	fs::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(logging_from_many_threads_loses_no_line)
{
	auto dir = fresh_dir("threads");
	auto file = dir / "edge.log";
	edge::init_logging(file, "debug");
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
	EDGE_LOG_INFO("done"); // info flushes
	std::string text = slurp(file);
	for (int t = 0; t < kThreads; ++t)
	{
		for (int i = 0; i < kLines; ++i)
		{
			std::string needle = "thread " + std::to_string(t) + " line " + std::to_string(i) + "\n";
			BOOST_TEST(text.find(needle) != std::string::npos, needle);
		}
	}
	edge::init_logging(std::nullopt);
	fs::remove_all(dir);
}

// The library's own lines go through the same logger: a glTF load is on
// record with the file it came from, the debug level adds the typed static
// mesh's vertex, index and bounds lines.
BOOST_AUTO_TEST_CASE(a_gltf_load_is_in_the_file)
{
	auto dir = fresh_dir("gltf");
	auto file = dir / "edge.log";
	edge::init_logging(file, "info");

	std::string const asset = edge::TestFixture::graphic_sources_path() + "test/data/test_gltf_loder_plane.glb";
	edge::RendererPtr renderer(new edge::NullRenderer(320, 200));
	edge::SchedulerPtr scheduler(new edge::Scheduler(renderer));
	auto model = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	model->load_from_gltf(asset);

	std::string text = slurp(file);
	BOOST_TEST(text.find("glTF primitive(s) from " + asset) != std::string::npos, text);
	BOOST_TEST(text.find("gltf_reader.cc:") != std::string::npos, text);
	BOOST_TEST(text.find("vertices: count = ") == std::string::npos, text); // debug, off at info

	edge::init_logging(file, "debug");
	auto model_2 = std::make_shared<edge::StaticModelPTBNT>(renderer, scheduler);
	model_2->load_from_gltf(asset);
	EDGE_LOG_INFO("flush"); // debug lines ride along with the next info flush
	text = slurp(file);
	BOOST_TEST(text.find("vertices: count = ") != std::string::npos, text);
	BOOST_TEST(text.find("bounds = (") != std::string::npos, text);

	edge::init_logging(std::nullopt);
	fs::remove_all(dir);
}

BOOST_AUTO_TEST_SUITE_END()
