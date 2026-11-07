#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# The rove the workflows check out, and the pyrove pyedge requires, are the rove edge's own build fetches: a release must not build against
# whatever rove's main is that day, or install beside a pyrove it was not released with.
set -eu
root=${1:-.}
pinned=$(sed -n 's/^set(EDGE_ROVE_GIT_TAG "\([^"]*\)".*/\1/p' "$root/CMakeLists.txt")
for workflow in "$root"/.github/workflows/*.yml; do
    if grep -q "ROVE_REF" "$workflow" && ! grep -q "ROVE_REF || '$pinned'" "$workflow"; then
        echo "$workflow does not check out rove $pinned, what CMakeLists.txt pins" >&2
        exit 1
    fi
done
# pyedge's requirement says the same version, exactly: a range would let pip pair it with a pyrove it cannot pass types to
if ! grep -q "^dependencies = .*\"pyrove==${pinned#v}\"" "$root/pyproject.toml"; then
    echo "pyproject.toml does not require pyrove==${pinned#v}, what CMakeLists.txt pins" >&2
    exit 1
fi
echo "the workflows check out rove $pinned, and pyedge requires pyrove==${pinned#v}"
