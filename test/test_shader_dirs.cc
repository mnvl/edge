// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// Where edge's compiled shaders are looked for (shader_dirs.h): the variable when it is set, else beside the binary edge is in (a
// bundle's shaders/edge, an install's ../share/edge/shaders), else the build's directory, else the install's; the first that holds
// the set's file is taken, and none holding it is an error that names every place tried.  The layouts are made in a temporary
// directory, each as the lookup would find it from a binary there.

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <boost/test/unit_test.hpp>

#include "shader_dirs.h"

#if defined(EDGE_USE_VULKAN)
#include "vulkan_pipeline_builder.h"
#endif

namespace
{

namespace fs = std::filesystem;

struct Fixture
{
	fs::path const root = fs::temp_directory_path() / "edge_shader_dirs";
	edge::ShaderLookup lookup{"the test's shaders", "EDGE_TEST_SHADER_DIR_VARIABLE", "vertex.spv", {"shaders/edge", "../share/edge/shaders"},
		root / "build" / "shaders", root / "prefix" / "share" / "edge" / "shaders"};

	Fixture()
	{
		fs::remove_all(root);
		fs::create_directories(root / "bin");
		fs::create_directories(root / "lib");
	}

	~Fixture()
	{
		std::error_code ignored;
		fs::remove_all(root, ignored);
	}

	/// @p dir made, with the lookup's file in it.
	fs::path holding(fs::path const &dir) const
	{
		fs::create_directories(dir);
		std::ofstream(dir / "vertex.spv") << "spir-v";
		return dir;
	}

	std::string message_of(char const *variable, fs::path const &binary_dir) const
	{
		try
		{
			edge::find_shader_dir(lookup, variable, binary_dir);
		}
		catch (std::runtime_error const &e)
		{
			return e.what();
		}
		BOOST_FAIL("a lookup that should fail found " + edge::find_shader_dir(lookup, variable, binary_dir).string());
		return "";
	}
};

}

BOOST_FIXTURE_TEST_SUITE(shader_dirs, Fixture)

BOOST_AUTO_TEST_CASE(the_variable_is_taken_as_it_is_whether_or_not_it_holds_them)
{
	holding(root / "bin" / "shaders" / "edge");
	BOOST_CHECK_EQUAL(edge::find_shader_dir(lookup, (root / "elsewhere").c_str(), root / "bin"), root / "elsewhere");
}

BOOST_AUTO_TEST_CASE(the_variable_set_but_empty_is_refused)
{
	holding(root / "bin" / "shaders" / "edge");
	BOOST_CHECK_NE(message_of("", root / "bin").find("EDGE_TEST_SHADER_DIR_VARIABLE is set but empty"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(a_bundle_beside_the_binary_comes_before_an_install_the_build_and_the_prefix)
{
	fs::path const bundle = holding(root / "bin" / "shaders" / "edge");
	holding(root / "share" / "edge" / "shaders");
	holding(lookup.build_dir);
	holding(lookup.install_dir);
	BOOST_CHECK_EQUAL(edge::find_shader_dir(lookup, nullptr, root / "bin"), bundle);
}

BOOST_AUTO_TEST_CASE(an_install_is_found_from_its_bin_and_its_lib)
{
	fs::path const share = holding(root / "share" / "edge" / "shaders");
	holding(lookup.build_dir);
	BOOST_CHECK_EQUAL(edge::find_shader_dir(lookup, nullptr, root / "bin"), share);
	BOOST_CHECK_EQUAL(edge::find_shader_dir(lookup, nullptr, root / "lib"), share);
}

BOOST_AUTO_TEST_CASE(a_directory_without_the_file_is_passed_over)
{
	fs::create_directories(root / "bin" / "shaders" / "edge");
	std::ofstream(root / "bin" / "shaders" / "edge" / "fragment.spv") << "spir-v";
	fs::path const share = holding(root / "share" / "edge" / "shaders");
	BOOST_CHECK_EQUAL(edge::find_shader_dir(lookup, nullptr, root / "bin"), share);
}

BOOST_AUTO_TEST_CASE(nothing_beside_the_binary_is_the_build_s_then_the_install_s)
{
	holding(lookup.build_dir);
	holding(lookup.install_dir);
	BOOST_CHECK_EQUAL(edge::find_shader_dir(lookup, nullptr, root / "bin"), lookup.build_dir);
	fs::remove(lookup.build_dir / "vertex.spv");
	BOOST_CHECK_EQUAL(edge::find_shader_dir(lookup, nullptr, root / "bin"), lookup.install_dir);
}

BOOST_AUTO_TEST_CASE(a_binary_whose_directory_is_not_known_is_the_build_s)
{
	holding(lookup.build_dir);
	BOOST_CHECK_EQUAL(edge::find_shader_dir(lookup, nullptr, fs::path()), lookup.build_dir);
}

BOOST_AUTO_TEST_CASE(where_no_place_holds_them_the_error_names_every_place_tried)
{
	std::string const message = message_of(nullptr, root / "bin");
	BOOST_TEST_MESSAGE(message);
	for (fs::path const &place : {root / "bin" / "shaders" / "edge", root / "share" / "edge" / "shaders", lookup.build_dir, lookup.install_dir})
		BOOST_CHECK_MESSAGE(message.find(place.string()) != std::string::npos, place << " is not in: " << message);
	BOOST_CHECK_NE(message.find("the test's shaders"), std::string::npos);
	BOOST_CHECK_NE(message.find("vertex.spv"), std::string::npos);
	BOOST_CHECK_NE(message.find("$EDGE_TEST_SHADER_DIR_VARIABLE (unset)"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(a_set_without_a_build_or_an_install_directory_skips_them)
{
	lookup.build_dir.clear();
	lookup.install_dir.clear();
	std::string const message = message_of(nullptr, root / "bin");
	BOOST_CHECK_EQUAL(message.find("the build's"), std::string::npos);
	BOOST_CHECK_EQUAL(message.find("the install's"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(the_binary_edge_is_in_is_this_test_s_executable)
{
	fs::path const dir = edge::edge_binary_dir();
	BOOST_TEST_MESSAGE(dir);
	BOOST_REQUIRE(!dir.empty());
	BOOST_CHECK(dir.is_absolute());
	BOOST_CHECK(fs::is_regular_file(dir / "test_edge") || fs::is_regular_file(dir / "test_edge.exe"));
#if defined(__linux__)
	BOOST_CHECK_EQUAL(dir, fs::canonical("/proc/self/exe").parent_path());
#endif
}

#if defined(EDGE_USE_VULKAN)
BOOST_AUTO_TEST_CASE(edge_s_spirv_is_the_build_s_and_a_copy_beside_a_binary_is_taken_over_it)
{
	edge::ShaderLookup const vulkan = edge::vulkan_shader_lookup();
	BOOST_REQUIRE(!vulkan.build_dir.empty());
	// (beside a directory with no copy, not beside test_edge: a parent project may bundle a copy there)
	BOOST_CHECK_EQUAL(edge::find_shader_dir(vulkan, nullptr, root / "bin"), vulkan.build_dir);
	fs::create_directories(root / "bin" / "shaders");
	fs::copy(vulkan.build_dir, root / "bin" / "shaders" / "edge", fs::copy_options::recursive);
	BOOST_CHECK_EQUAL(edge::find_shader_dir(vulkan, nullptr, root / "bin"), root / "bin" / "shaders" / "edge");
	BOOST_CHECK_EQUAL(edge::find_shader_dir(vulkan, nullptr, root / "lib" / ".." / "bin"), root / "bin" / "shaders" / "edge");
}

BOOST_AUTO_TEST_CASE(vulkan_shader_dir_is_the_variable_or_the_lookup)
{
	char const *was = std::getenv("EDGE_VK_SHADER_DIR");
	std::string const saved = was ? was : "";
	unsetenv("EDGE_VK_SHADER_DIR");
	BOOST_CHECK_EQUAL(fs::path(edge::vulkan_shader_dir()), edge::find_shader_dir(edge::vulkan_shader_lookup(), nullptr, edge::edge_binary_dir()));
	setenv("EDGE_VK_SHADER_DIR", root.c_str(), 1);
	BOOST_CHECK_EQUAL(fs::path(edge::vulkan_shader_dir()), root);
	if (was)
		setenv("EDGE_VK_SHADER_DIR", saved.c_str(), 1);
	else
		unsetenv("EDGE_VK_SHADER_DIR");
}

BOOST_AUTO_TEST_CASE(the_view_shaders_are_the_build_s)
{
	edge::ShaderLookup const view = edge::view_shader_lookup();
	BOOST_REQUIRE(!view.build_dir.empty());
	BOOST_CHECK(fs::is_regular_file(edge::find_shader_dir(view, nullptr, edge::edge_binary_dir()) / "lit.frag.spv"));
}
#endif

BOOST_AUTO_TEST_SUITE_END()
