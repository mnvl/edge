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

# project: the file of its licence, under the deps directory
PROJECTS = {
    "fastgltf": ["fastgltf-src/LICENSE.md"],
    # (fastgltf fetches simdjson's two sources without their licence: kept here, of the version it pins)
    "simdjson": [str(ROOT / "etc/licenses/simdjson-LICENSE")],
    "cereal": ["cereal-src/LICENSE"],
    "stb": ["stb-src/LICENSE"],
    "meshoptimizer": ["meshoptimizer-src/LICENSE.md"],
    "bc7enc_rdo": ["bc7enc_rdo-src/LICENSE"],
    "astc-encoder": ["astcenc-src/LICENSE.txt"],
    "xatlas": ["xatlas-src/LICENSE"],
    "nanobind": ["nanobind-src/LICENSE"],
}


def main(deps: Path) -> None:
    out = ["# Third-party notices\n", "The licences of the code a pyedge wheel and an install of edge carry. See THIRD_PARTY.md for what each is used for.\n"]
    for project, candidates in PROJECTS.items():
        path = next((deps / c for c in candidates if (deps / c).exists()), None)
        if path is None:
            raise SystemExit(f"no licence of {project} under {deps}: tried {candidates}")
        out.append(f"## {project}\n\n```\n{path.read_text(encoding='utf-8', errors='replace').strip()}\n```\n")
    (ROOT / "THIRD_PARTY_NOTICES.md").write_text("\n".join(out), encoding="utf-8")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
