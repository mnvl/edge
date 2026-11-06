#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# Builds edge with the OpenGL ES backend in the Linux of etc/container/Dockerfile (podman) and runs its tests there:
#   etc/container/test.sh [ctest arguments...]
# edge and rove (../rove, or $ROVE) are mounted as overlays: the build writes nothing into them.  The build tree is kept in the
# volume edge-linux-build between runs.
set -eu
cd "$(dirname "$0")/../.."
rove=${ROVE:-$PWD/../rove}
podman build -q -t edge-linux -f etc/container/Dockerfile etc/container >/dev/null
podman run --rm -v "$PWD:/src/edge:O" -v "$rove:/src/rove:O" -v edge-linux-build:/build edge-linux sh -c "
    set -e
    cmake -S /src/edge -B /build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEDGE_GLES=ON -DEDGE_ROVE_SOURCE_DIR=/src/rove >/build/configure.log || { tail -40 /build/configure.log; exit 1; }
    cmake --build /build
    ctest --test-dir /build -j\$(nproc) --output-on-failure $*"
