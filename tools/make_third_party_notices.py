#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Writes THIRD_PARTY_NOTICES.md from the licences of what a build fetched: make_third_party_notices.py <build>/_deps

MIT, BSD-3-Clause and Apache-2.0 all ask that the copyright line and the licence text go with a binary that carries the code.  Run it when a
dependency changes.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# project: the file of its licence, under the deps directory (the fmt that spdlog bundles has its licence at the head of a header)
PROJECTS = {
    "fastgltf": ["fastgltf-src/LICENSE.md"],
    # (fastgltf fetches simdjson's two sources without their licence: kept here, of the version it pins)
    "simdjson": [str(ROOT / "etc/licenses/simdjson-LICENSE")],
    "cereal": ["cereal-src/LICENSE"],
    "stb": ["stb-src/LICENSE"],
    "spdlog": ["spdlog-src/LICENSE"],
    "meshoptimizer": ["meshoptimizer-src/LICENSE.md"],
    "bc7enc_rdo": ["bc7enc_rdo-src/LICENSE"],
    "astc-encoder": ["astcenc-src/LICENSE.txt"],
    "xatlas": ["xatlas-src/LICENSE"],
    "nanobind": ["nanobind-src/LICENSE"],
}

FMT = """Copyright (c) 2012 - present, Victor Zverovich

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

--- Optional exception to the license ---

As an exception, if, as a result of your compiling your source code, portions of this Software are embedded into a machine-executable object
form of such source code, you may redistribute such embedded portions in such object form without including the above copyright and permission
notices.
"""


def main(deps: Path) -> None:
    out = ["# Third-party notices\n", "The licences of the code a pyedge wheel and an install of edge carry. See THIRD_PARTY.md for what each is used for.\n"]
    for project, candidates in PROJECTS.items():
        path = next((deps / c for c in candidates if (deps / c).exists()), None)
        if path is None:
            raise SystemExit(f"no licence of {project} under {deps}: tried {candidates}")
        out.append(f"## {project}\n\n```\n{path.read_text(encoding='utf-8', errors='replace').strip()}\n```\n")
    out.append(f"## fmt\n\nBundled in spdlog.\n\n```\n{FMT.strip()}\n```\n")
    (ROOT / "THIRD_PARTY_NOTICES.md").write_text("\n".join(out), encoding="utf-8")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
