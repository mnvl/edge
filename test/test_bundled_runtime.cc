// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

// use_bundled_runtime points edge and the Vulkan loader at a bundle that edge_bundle_runtime made (etc/cmake/EdgeRuntime.cmake; the
// test build makes one at EDGE_TEST_BUNDLE_DIR): a renderer made after it draws with the bundle's shaders and, on macOS, the
// bundle's driver.  A bundle that lacks one of them is refused, and the environment is left as it was.

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <boost/test/unit_test.hpp>

#include "bundled_runtime.h"
#include "camera.h"
#include "draw_call.h"
#include "material.h"
#include "pixel.h"
#include "render_target.h"
#include "vertex_buffer.h"
#include "vulkan_renderer.h"

namespace
{

std::filesystem::path const BUNDLE = EDGE_TEST_BUNDLE_DIR;

/// The variables use_bundled_runtime sets, as they were before each test, and again after it.
struct Fixture
{
	std::optional<std::string> const shader_dir = get("EDGE_VK_SHADER_DIR"), driver = get("VK_DRIVER_FILES");
	std::filesystem::path const dir = std::filesystem::temp_directory_path() / "edge_bundled_runtime";

	Fixture()
	{
		std::filesystem::remove_all(dir);
		std::filesystem::create_directories(dir);
	}

	~Fixture()
	{
		restore("EDGE_VK_SHADER_DIR", shader_dir);
		restore("VK_DRIVER_FILES", driver);
		std::error_code ignored;
		std::filesystem::remove_all(dir, ignored);
	}

	static std::optional<std::string> get(char const *name)
	{
		char const *value = std::getenv(name);
		return value ? std::optional<std::string>(value) : std::nullopt;
	}

	static void restore(char const *name, std::optional<std::string> const &value)
	{
		if (value)
			setenv(name, value->c_str(), 1);
		else
			unsetenv(name);
	}

	void check_unchanged() const
	{
		BOOST_CHECK(get("EDGE_VK_SHADER_DIR") == shader_dir);
		BOOST_CHECK(get("VK_DRIVER_FILES") == driver);
	}
};

/// A frame of one triangle of the default material, edge's own shaders, on a renderer made now.
void draw_a_frame()
{
	auto const renderer = std::make_shared<edge::VulkanRenderer>(16, 16);
	edge::VertexBufferPtr const vb = renderer->create_vertex_buffer(edge::VertexTraits::of<edge::VERTEX_PT>(), sizeof(edge::VERTEX_PT) * 3);
	auto vb_mapping = vb->write(edge::Contents::DISCARD);
	std::memset(vb_mapping.data(), 0, sizeof(edge::VERTEX_PT) * 3);
	vb_mapping.unmap();
	edge::DrawCallPtr const draw = renderer->create_draw_call();
	draw->set_vertex_buffer(vb);
	draw->set_material(renderer->create_material());
	draw->set_camera(std::make_shared<edge::Camera>());
	draw->set_triangles_count(1);
	draw->set_always_visible(true);
	draw->commit();
	renderer->begin_frame();
	renderer->begin_pass(renderer->create_render_target(16, 16, edge::PixelFormat::A8R8G8B8));
	draw->execute();
	renderer->end_pass();
	renderer->finish_frame();
}

std::string message_of(std::filesystem::path const &dir)
{
	try
	{
		edge::use_bundled_runtime(dir);
	}
	catch (std::runtime_error const &e)
	{
		return e.what();
	}
	BOOST_FAIL("a bundle that lacks a part was taken");
	return "";
}

}

BOOST_FIXTURE_TEST_SUITE(bundled_runtime, Fixture)

BOOST_AUTO_TEST_CASE(a_bundle_points_edge_at_its_shaders_and_on_macos_the_loader_at_its_driver)
{
	std::filesystem::path const shaders = edge::use_bundled_runtime(BUNDLE);
	BOOST_CHECK_EQUAL(shaders, BUNDLE / "shaders" / "edge");
	BOOST_CHECK_EQUAL(std::string(std::getenv("EDGE_VK_SHADER_DIR")), shaders.string());
#if defined(__APPLE__)
	BOOST_CHECK_EQUAL(std::string(std::getenv("VK_DRIVER_FILES")), (BUNDLE / "vulkan" / "MoltenVK_icd.json").string());
#else
	BOOST_CHECK(get("VK_DRIVER_FILES") == driver);
#endif
	draw_a_frame();
}

BOOST_AUTO_TEST_CASE(a_renderer_made_after_it_reads_the_bundle_s_shaders_and_no_others)
{
	// the shaders copied, the driver the test bundle's (a second copy of MoltenVK in the process would clash with the first)
	std::filesystem::create_directories(dir / "bundle");
	std::filesystem::copy(BUNDLE / "shaders", dir / "bundle" / "shaders", std::filesystem::copy_options::recursive);
	std::filesystem::remove(dir / "bundle" / "shaders" / "edge" / "fragment.spv");
	std::filesystem::create_directories(dir / "bundle" / "vulkan");
	std::ofstream(dir / "bundle" / "vulkan" / "MoltenVK_icd.json") << "{\"file_format_version\": \"1.0.0\", \"ICD\": {\"library_path\": \""
		<< (BUNDLE / "vulkan" / "libMoltenVK.dylib").string() << "\", \"api_version\": \"1.4.0\", \"is_portability_driver\": true}}";
	edge::use_bundled_runtime(dir / "bundle");
	try
	{
		draw_a_frame();
		BOOST_FAIL("drew without the bundle's fragment shader");
	}
	catch (std::runtime_error const &e)
	{
		BOOST_CHECK_MESSAGE(std::string(e.what()).find((dir / "bundle" / "shaders" / "edge" / "fragment.spv").string()) != std::string::npos, e.what());
	}
}

BOOST_AUTO_TEST_CASE(a_bundle_without_edge_s_shaders_is_refused_and_changes_nothing)
{
	std::string const message = message_of(dir);
	BOOST_CHECK_MESSAGE(message.find((dir / "shaders" / "edge").string()) != std::string::npos, message);
	check_unchanged();
}

#if defined(__APPLE__)
BOOST_AUTO_TEST_CASE(a_bundle_without_the_driver_is_refused_on_macos_and_changes_nothing)
{
	std::filesystem::copy(BUNDLE / "shaders", dir / "shaders", std::filesystem::copy_options::recursive);
	std::string const message = message_of(dir);
	BOOST_CHECK_MESSAGE(message.find("MoltenVK") != std::string::npos, message);
	check_unchanged();
}
#endif

BOOST_AUTO_TEST_SUITE_END()
