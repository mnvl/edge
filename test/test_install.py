# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""What `cmake --install` puts in a prefix: edge's own files and what its public headers need, nothing a program built against
edge would have to get from elsewhere without being told.  The build ctest runs in is installed into a temporary prefix once.

ctest passes the build through the environment: EDGE_BUILD_DIR, EDGE_BUILD_CONFIG, CMAKE_COMMAND, the compiler the build uses
(EDGE_CXX, EDGE_CXX_ID), and the ImGui edge's own tests are built with (EDGE_TEST_IMGUI_DIR), which stands for a consumer's: the
ImGui tests run where the build has edge_imgui (EDGE_IMGUI).  The graphics APIs' headers are where the build found them
(EDGE_TEST_VULKAN_INCLUDE_DIR, EDGE_TEST_GLES_INCLUDE_DIR; empty: the compiler's own path); a backend's tests run where the build has it.
"""

import os
import subprocess
from pathlib import Path

import pytest

BUILD = Path(os.environ["EDGE_BUILD_DIR"])
CONFIG = os.environ["EDGE_BUILD_CONFIG"]
CMAKE = os.environ["CMAKE_COMMAND"]
CXX = os.environ["EDGE_CXX"]
CXX_ID = os.environ["EDGE_CXX_ID"]

# Linked into edge, or gone, and none of them a consumer's business: nothing of them is in edge's install.  (spdlog, and the fmt it
# bundles: the log is edge's own.  cereal: the .edge file's serialization, inside edge; a program reads a file with Model::read_file.)
GONE = ("spdlog", "fmt", "cereal")


@pytest.fixture(scope="module")
def prefix(tmp_path_factory) -> Path:
    prefix = tmp_path_factory.mktemp("prefix")
    command = [CMAKE, "--install", str(BUILD), "--prefix", str(prefix)]
    if CONFIG:
        command += ["--config", CONFIG]
    subprocess.run(command, check=True, capture_output=True, text=True)
    assert (prefix / "include" / "edge" / "log.h").is_file()
    return prefix


def test_nothing_of_spdlog_fmt_or_cereal_is_installed(prefix):
    found = [str(path.relative_to(prefix)) for path in prefix.rglob("*")
             if any(part == name or part.startswith("lib" + name) or part.startswith(name + "-") for part in path.parts for name in GONE)]
    assert found == []


def test_the_package_asks_for_none_of_them(prefix):
    package = list((prefix / "lib").glob("**/cmake/edge/*.cmake")) + list((prefix / "lib64").glob("**/cmake/edge/*.cmake"))
    assert package, "no edge package in the install"
    for file in package:
        text = file.read_text(encoding="utf-8")
        for name in ("spdlog", "cereal"):
            assert name not in text.lower(), (file, name)


def test_no_installed_edge_header_includes_them(prefix):
    headers = list((prefix / "include" / "edge").glob("*.h"))
    assert headers
    for header in headers:
        text = header.read_text(encoding="utf-8")
        for name in GONE:
            assert f"<{name}/" not in text, header
        assert "spdlog::" not in text and "SPDLOG_" not in text and "cereal::" not in text and "CEREAL_" not in text, header


PUBLIC_HEADERS = Path(__file__).resolve().parents[1] / "include" / "edge"


def test_every_installed_edge_header_is_a_public_one(prefix):
    # (include/edge/ has a shim of each public header; a backend's internals, the classes its renderer is built of, are not among them)
    public = {path.name for path in PUBLIC_HEADERS.glob("*.h")}
    assert "vulkan_renderer.h" in public and "renderer.h" in public, public
    installed = {path.name for path in (prefix / "include" / "edge").iterdir()}
    assert installed, "no edge headers installed"
    assert sorted(installed - public) == []


def _include_flag(directory: str) -> list[str]:
    return [("/I" if CXX_ID == "MSVC" else "-I") + directory] if directory else []


def _compile(source: Path, include: Path, *extra: str) -> subprocess.CompletedProcess:
    if CXX_ID == "MSVC":
        command = [CXX, "/nologo", "/std:c++20", "/EHsc", "/Zs", f"/I{include}", *extra, str(source)]
    elif CXX_ID in ("GNU", "Clang", "AppleClang"):
        command = [CXX, "-std=c++20", "-fsyntax-only", f"-I{include}", *extra, str(source)]
    else:
        raise AssertionError(f"no compile command for the compiler {CXX_ID} ({CXX})")
    return subprocess.run(command, capture_output=True, text=True)


CONSUMER = """\
#include <edge/log.h>

#include <string_view>

int main()
{
	edge::set_log_sink([](edge::LogLevel level, std::string_view message, char const *file, int line) {
		(void)level; (void)message; (void)file; (void)line;
	});
	edge::set_log_level(edge::LogLevel::debug);
	EDGE_LOG_INFO("{} {:.1f}", 1, 2.5);
	edge::set_log_sink(nullptr);
}
"""


IMGUI_HEADERS = ("imgui.h", "imconfig.h", "imgui_internal.h", "imstb_rectpack.h", "imstb_textedit.h", "imstb_truetype.h")


def test_nothing_of_imgui_is_installed(prefix):
    # (ImGui is the program's: edge's renderer of it is a header, compiled against the program's ImGui)
    assert (prefix / "include" / "edge" / "imgui_renderer.h").is_file()
    found = [str(path.relative_to(prefix)) for path in prefix.rglob("*")
             if path.name in IMGUI_HEADERS or (path.is_file() and "imgui" in path.name and path.parent.name.startswith("lib"))
             or path == prefix / "include" / "edge" / "imgui"]
    assert found == []


def test_the_package_exports_no_imgui_library(prefix):
    targets = "".join(file.read_text(encoding="utf-8") for file in (prefix / "lib").glob("**/cmake/edge/edgeTargets*.cmake"))
    assert targets
    assert "imgui_core" not in targets
    assert "add_library(edge::imgui INTERFACE IMPORTED)" in targets, targets


IMGUI_CONSUMER = """\
#include <imgui.h>
#include <edge/imgui_renderer.h>

#include <memory>

std::unique_ptr<edge::ImGuiRenderer> make(edge::RendererPtr const &renderer)
{
	ImGui::CreateContext();
	return std::make_unique<edge::ImGuiRenderer>(renderer);
}
"""


def test_the_imgui_renderer_compiles_from_the_prefix_against_the_programs_imgui(prefix, tmp_path):
    imgui = Path(os.environ["EDGE_TEST_IMGUI_DIR"])
    assert (imgui / "imgui.h").is_file(), imgui
    source = tmp_path / "imgui_consumer.cc"
    source.write_text(IMGUI_CONSUMER)
    result = _compile(source, prefix / "include", ("/I" if CXX_ID == "MSVC" else "-I") + str(imgui))
    assert result.returncode == 0, result.stdout + result.stderr


def test_the_log_header_compiles_with_the_prefix_alone(prefix, tmp_path):
    source = tmp_path / "consumer.cc"
    source.write_text(CONSUMER)
    result = _compile(source, prefix / "include")
    assert result.returncode == 0, result.stdout + result.stderr


def test_the_log_header_reads_nothing_of_spdlog_or_fmt(prefix, tmp_path):
    # (a system with spdlog's headers in /usr/include compiles the header even if it still included them: the list of what the
    # compiler read says)
    source = tmp_path / "consumer.cc"
    source.write_text(CONSUMER)
    result = _compile(source, prefix / "include", "/showIncludes" if CXX_ID == "MSVC" else "-H")
    assert result.returncode == 0, result.stdout + result.stderr
    read = (result.stdout + result.stderr).replace("\\", "/")
    assert "log.h" in read, read  # (the list is there)
    for name in GONE:
        assert f"/{name}/" not in read, read


MODEL_CONSUMER = """\
#include <edge/model.h>
#include <edge/serialized_model.h>

#include <string>

std::size_t meshes_of(std::string const &file)
{
	edge::SerializedModel const model = edge::Model::read_file(file);
	return model.meshes.size() + model.materials.size();
}
"""


def test_the_model_file_headers_compile_from_the_prefix_and_read_no_cereal(prefix, tmp_path):
    source = tmp_path / "model_consumer.cc"
    source.write_text(MODEL_CONSUMER)
    result = _compile(source, prefix / "include", "/showIncludes" if CXX_ID == "MSVC" else "-H")
    assert result.returncode == 0, result.stdout + result.stderr
    read = (result.stdout + result.stderr).replace("\\", "/")
    assert "serialized_model.h" in read, read
    assert "/cereal/" not in read, read


# What a program does with the backends' renderers, every inline member among it: each header compiles from the prefix alone (with
# the graphics API's own headers), its internals gone from the install.
VULKAN_CONSUMER = """\
#include <edge/vulkan_renderer.h>
#include <edge/vulkan_device_info.h>
#include <edge/vulkan_instance.h>

#include <memory>

double use(VkSurfaceKHR surface)
{
	auto renderer = std::make_shared<edge::VulkanRenderer>(64, 32, std::string(), std::vector<std::string>(), 2);
	edge::RendererPtr base = renderer;
	renderer->attach_surface(surface, true);
	renderer->set_vsync(false);
	VkInstance instance = renderer->instance();
	VkDevice device = renderer->device();
	VkPipelineLayout layout = renderer->pipeline_layout();
	edge::VulkanRendererPtr self = renderer->shared_from_this();
	(void)instance; (void)device; (void)layout; (void)self;
	renderer->wait_frames();
	return renderer->gpu_wait_ms() + renderer->frames_in_flight() + double(renderer->device_uuid().size())
		+ double(renderer->staging_arena_size()) + double(renderer->frame_serial()) + double(renderer->single_time_submit_count());
}
"""


def test_the_vulkan_renderer_header_compiles_from_the_prefix(prefix, tmp_path):
    source = tmp_path / "vulkan_consumer.cc"
    source.write_text(VULKAN_CONSUMER)
    result = _compile(source, prefix / "include", *_include_flag(os.environ["EDGE_TEST_VULKAN_INCLUDE_DIR"]))
    assert result.returncode == 0, result.stdout + result.stderr


GLES_CONSUMER = """\
#include <edge/gles_renderer.h>

#include <memory>

double use(edge::GLESContextPtr const &context)
{
	auto renderer = std::make_shared<edge::GLESRenderer>(64, 32, "shaders", context);
	edge::RendererPtr base = renderer;
	renderer->show_frame(renderer->create_texture(64, 32, edge::PixelFormat::A8R8G8B8), 640, 320);
	renderer->swap_window();
	return double(renderer->context() != nullptr) + double(renderer->frame_serial()) + double(renderer->previous_block_writes())
		+ renderer->screen_width();
}
"""


def test_the_gles_renderer_header_compiles_from_the_prefix(prefix, tmp_path):
    source = tmp_path / "gles_consumer.cc"
    source.write_text(GLES_CONSUMER)
    result = _compile(source, prefix / "include", *_include_flag(os.environ["EDGE_TEST_GLES_INCLUDE_DIR"]))
    assert result.returncode == 0, result.stdout + result.stderr
