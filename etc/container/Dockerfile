# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# Linux, for building and testing edge where the host cannot: the OpenGL ES backend on a Mac.  Mesa draws both backends on the
# CPU (llvmpipe for GLES through EGL, lavapipe for Vulkan), so every test runs with no GPU.  etc/container/test.sh builds the image
# and runs the tests in it.
FROM docker.io/library/debian:trixie

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake git ninja-build pkg-config \
        python3 python3-dev python3-numpy python3-pil python3-pytest \
        fonts-dejavu-core gdb \
        libboost-dev libboost-test-dev \
        libvulkan-dev mesa-vulkan-drivers vulkan-validationlayers glslang-tools \
        libgles-dev libegl-dev libegl-mesa0 libgl1-mesa-dri libsdl3-dev \
    && rm -rf /var/lib/apt/lists/*

# the checkouts belong to another uid than root's, which git refuses to read otherwise
RUN git config --global --add safe.directory '*'
