# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""What `cmake --install` puts in a prefix: edge's own files and what its public headers need, nothing a program built against
edge would have to get from elsewhere without being told.  The build ctest runs in is installed into a temporary prefix once.

ctest passes the build through the environment: EDGE_BUILD_DIR, EDGE_BUILD_CONFIG, CMAKE_COMMAND, and the compiler the build uses
(EDGE_CXX, EDGE_CXX_ID).
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

# The logger is edge's own: nothing of spdlog, or of the fmt it bundles, is in edge's install
GONE = ("spdlog", "fmt")


@pytest.fixture(scope="module")
def prefix(tmp_path_factory) -> Path:
    prefix = tmp_path_factory.mktemp("prefix")
    command = [CMAKE, "--install", str(BUILD), "--prefix", str(prefix)]
    if CONFIG:
        command += ["--config", CONFIG]
    subprocess.run(command, check=True, capture_output=True, text=True)
    assert (prefix / "include" / "edge" / "log.h").is_file()
    return prefix


def test_nothing_of_spdlog_or_fmt_is_installed(prefix):
    found = [str(path.relative_to(prefix)) for path in prefix.rglob("*")
             if any(part == name or part.startswith("lib" + name) or part.startswith(name + "-") for part in path.parts for name in GONE)]
    assert found == []


def test_the_package_does_not_ask_for_spdlog(prefix):
    package = list((prefix / "lib").glob("**/cmake/edge/*.cmake")) + list((prefix / "lib64").glob("**/cmake/edge/*.cmake"))
    assert package, "no edge package in the install"
    for file in package:
        text = file.read_text(encoding="utf-8")
        assert "spdlog" not in text.lower(), file


def test_no_installed_edge_header_includes_spdlog_or_fmt(prefix):
    headers = list((prefix / "include" / "edge").glob("*.h"))
    assert headers
    for header in headers:
        text = header.read_text(encoding="utf-8")
        for name in GONE:
            assert f"<{name}/" not in text, header
        assert "spdlog::" not in text and "SPDLOG_" not in text, header


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
