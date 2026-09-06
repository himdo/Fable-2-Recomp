# Wrapper project to build the ReXGlue SDK's Vulkan GPU plugin.
#
# Why a wrapper? Building the SDK directly as a top-level project runs its
# install/export step (cmake/rexglue_install.cmake). For the Vulkan config that
# step appends the glslang targets to its install list by their *raw* names
# (e.g. "SPIRV"), but those targets only exist namespaced (glslang::SPIRV), so
# configure fails with:  install TARGETS given target "SPIRV" which does not
# exist.  (The SDK's top-level Vulkan-on-Windows install path is incomplete.)
#
# Consuming the SDK via add_subdirectory() makes PROJECT_IS_TOP_LEVEL false,
# which skips that install step entirely. We only need the built
# rexgpu-xenos.dll (Vulkan), so skipping install/packaging is fine. The
# wrapper modifies nothing - the SDK tree it consumes is stock upstream plus
# this project's tracked local patch (thirdparty/rexglue-sdk-local.patch).
cmake_minimum_required(VERSION 3.25)

# C/CXX enabled so the SDK's C and C++ dependencies configure. The compiler
# itself is selected via -DCMAKE_C(CXX)_COMPILER on the command line, which
# must precede this project() call.
project(rexglue_vulkan_plugin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES Release)
endif()

# Both backends on. The resulting rexruntime.dll is a SUPERSET that exports both
# the D3D12 symbols (so the game + the D3D12 plugin load against it) and the
# Vulkan symbols (so the source Vulkan plugin loads against it). The prebuilt
# 0.10.0 runtime is D3D12-only and is missing the 16 Vulkan symbols the plugin
# needs, so the game must run against this source-built runtime instead.
set(REXGLUE_USE_VULKAN ON CACHE BOOL "Enable Vulkan graphics backend" FORCE)
set(REXGLUE_USE_D3D12 ON CACHE BOOL "Enable D3D12 graphics backend" FORCE)

# SDK source: default to thirdparty/rexglue-sdk-src in this repo (fetched +
# patched by tools/setup_sdk_src.cmd); override with -DREXGLUE_SDK_SOURCE=...
if(NOT REXGLUE_SDK_SOURCE OR NOT EXISTS "${REXGLUE_SDK_SOURCE}")
    set(REXGLUE_SDK_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/rexglue-sdk-src")
endif()
message(STATUS "REXGLUE_SDK_SOURCE: ${REXGLUE_SDK_SOURCE}")
if(NOT EXISTS "${REXGLUE_SDK_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "SDK source not found at ${REXGLUE_SDK_SOURCE} - run tools\setup_sdk_src.cmd first")
endif()

# Add PDB debug info to the Release build so crash dumps can be symbolized with
# cdb. -g emits CodeView debug info into the objects (does not change the -O3
# machine code, so the PDBs match the DLLs); /DEBUG makes the linker emit the
# .pdb. (Directory-level, so it inherits into the SDK's targets.)
add_compile_options($<$<CONFIG:RELEASE>:-g>)
# /DEBUG must reach lld-link via -Xlinker (a bare /DEBUG is read by the clang
# driver as a file path). Only Release is built in this wrapper.
add_link_options(-Xlinker /DEBUG)

add_subdirectory(${REXGLUE_SDK_SOURCE} "${CMAKE_BINARY_DIR}/sdk")
