#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# The rove the workflows check out is the rove edge's own build fetches: a release must not build against whatever rove's main is that day.
set -eu
root=${1:-.}
pinned=$(sed -n 's/^set(EDGE_ROVE_GIT_TAG "\([^"]*\)".*/\1/p' "$root/CMakeLists.txt")
for workflow in "$root"/.github/workflows/*.yml; do
    if grep -q "ROVE_REF" "$workflow" && ! grep -q "ROVE_REF || '$pinned'" "$workflow"; then
        echo "$workflow does not check out rove $pinned, what CMakeLists.txt pins" >&2
        exit 1
    fi
done
echo "the workflows check out rove $pinned"
