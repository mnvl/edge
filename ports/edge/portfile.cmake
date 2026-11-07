# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

# edge's vcpkg port, as an overlay port beside rove's: vcpkg install edge --overlay-ports=<rove>/ports --overlay-ports=<edge>/ports.
# EDGE_SOURCE_DIR (passed through with VCPKG_KEEP_ENV_VARS=EDGE_SOURCE_DIR) builds a checkout instead of the release, which is how
# CI tests the port before a version is tagged.
#
# fastgltf, which edge patches, and the asset tools' encoders (bc7enc_rdo, astc-encoder, xatlas), which vcpkg has not, are fetched by
# edge's CMake at build time (git): the build needs network access, and turns FetchContent's downloads back on.
if(DEFINED ENV{EDGE_SOURCE_DIR})
    set(SOURCE_PATH "$ENV{EDGE_SOURCE_DIR}")
else()
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO mnvl/edge
        REF "v${VERSION}"
        # the SHA512 of the release's archive: vcpkg prints it when this is wrong
        SHA512 c3dd19caaef12453f49a14286ffb5b847d83dfd3bb015620180554c7c3c30a39cb5b5fd0efa9abfea38d0c59289e0746d020486202d01aaeeae606f2a1825c48
        HEAD_REF main
    )
endif()

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        asset-tools EDGE_ASSET_TOOLS
        imgui EDGE_IMGUI
        sdl3 EDGE_SDL
)

vcpkg_find_acquire_program(GIT)
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DEDGE_SYSTEM_DEPS=ON
        -DEDGE_INSTALL=ON
        -DEDGE_TESTS=OFF
        -DEDGE_PYTHON=OFF
        -DEDGE_GLES=OFF
        -DEDGE_SHOT=OFF
        -DFETCHCONTENT_FULLY_DISCONNECTED=OFF
        "-DGIT_EXECUTABLE=${GIT}"
        "-DGLSLANG_VALIDATOR=${CURRENT_HOST_INSTALLED_DIR}/tools/glslang/glslangValidator${VCPKG_HOST_EXECUTABLE_SUFFIX}"
    MAYBE_UNUSED_VARIABLES
        EDGE_SHOT
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/edge)
# the programs in tools/edge (the package's targets are rewritten to them by vcpkg_cmake_config_fixup); shader_cross is the Metal
# build's, which the helpers find through edgeConfig.cmake
if("asset-tools" IN_LIST FEATURES)
    vcpkg_copy_tools(TOOL_NAMES edge_convert AUTO_CLEAN)
endif()
if(EXISTS "${CURRENT_PACKAGES_DIR}/bin/edge_shader_cross${VCPKG_TARGET_EXECUTABLE_SUFFIX}")
    vcpkg_copy_tools(TOOL_NAMES edge_shader_cross AUTO_CLEAN)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/share/${PORT}/edgeConfig.cmake"
        [[set(_edge_bin_dir "${PACKAGE_PREFIX_DIR}/bin")]] [[set(_edge_bin_dir "${PACKAGE_PREFIX_DIR}/tools/edge")]])
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
