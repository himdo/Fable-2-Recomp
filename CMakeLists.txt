# fable_2 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(fable_2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(FABLE2_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(fable_2 WIN32 ${FABLE2_SOURCES})
else()
    add_executable(fable_2 ${FABLE2_SOURCES})
endif()

# Stage the Xenos GPU emulation plugin next to the exe (rexglue picks the
# per-configuration variant: rexgpu-xenosd.dll for Debug, rexgpu-xenos.dll
# for Release). Without it the game runs with no GPU emulation.
rexglue_setup_target(fable_2 GPU_PLUGINS xenos)

# NOTE: the build does NOT copy the game content (default.xex, data/, ...) into
# the build directory. fable_2.exe looks for its content in its own directory
# only; to run in place, stage it manually (one-time ~6.5 GB copy):
#   tools\stage_content.cmd <project root> out\build\<preset>
# or point the exe at an existing content tree with --game_data_root=<path>.
# saves/, cache/, logs/, and fable_2.toml are always written next to the exe.

# --- PDB for the Release build (crash debugging) --------------------------
# The recompiled guest functions are named sub_<guest_address>, so a PDB lets
# the debugger resolve a crash PC directly to the offending guest function
# (see tools/crash_capture.cmd + cdb). -g emits CodeView info without
# changing -O3 codegen; /DEBUG makes lld-link emit the .pdb.
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(fable_2 PRIVATE -g)
    target_link_options(fable_2 PRIVATE -Xlinker /DEBUG)
    if(TARGET fable_2_recomp)
        target_compile_options(fable_2_recomp PRIVATE -g)
    endif()
endif()

# --- Vulkan renderer: Stage 2 (source-built plugin, swappable at launch) --
# The prebuilt plugin above (GPU_PLUGINS xenos) is D3D12-only. The Vulkan
# backend is built from the (unmodified) SDK source via
# tools/build_sdk_vulkan.cmd (a Release build) into a separate tree. Stage it
# next to the exe as rexgpu-xenos-vulkan.dll so the game can swap to Vulkan at
# launch with --gpu_plugin=xenos-vulkan (see Fable2App::OnPreSetup) - no
# rebuild of the game required to switch backends. Release-only: the source
# plugin is a Release build (imports rexruntime.dll), which matches only the
# Release game (the Debug game links rexruntimed.dll). Skipped (with a note) if
# the source plugin hasn't been built yet.
# SDK locations, resolved relative to this repo (no hardcoded paths).
# Override on the command line with:
#   -DREXGLUE_SDK_ROOT=<prebuilt SDK root>    (win-amd64 layout: bin/, include/, lib/)
#   -DREXGLUE_SDK_SOURCE=<SDK source tree>    (optional; Vulkan plugin only)
# The defaults are created by tools/setup_sdk.cmd (prebuilt, auto-run by
# build.cmd) and tools/setup_sdk_src.cmd (source, only for the Vulkan path).
if(NOT DEFINED REXGLUE_SDK_ROOT)
    set(REXGLUE_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/win-amd64")
endif()
if(NOT DEFINED REXGLUE_SDK_SOURCE)
    set(REXGLUE_SDK_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk-src")
endif()
set(FABLE2_SDK_SRC "${REXGLUE_SDK_SOURCE}")
set(FABLE2_SDK_ROOT "${REXGLUE_SDK_ROOT}")
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    # The SDK stages its outputs under its own out/win-amd64/Release/ tree
    # (REXGLUE_ROOT based). We need both the source plugin AND the source
    # (both-backends) runtime: the prebuilt 0.10.0 runtime is D3D12-only and is
    # missing the 16 Vulkan symbols the source plugin imports, so the Vulkan
    # path must run against the source runtime.
    file(GLOB FABLE2_VULKAN_PLUGIN_CANDIDATES "${FABLE2_SDK_SRC}/out/win-amd64/Release/rexgpu-xenos.dll")
    file(GLOB FABLE2_VULKAN_RUNTIME_CANDIDATES "${FABLE2_SDK_SRC}/out/win-amd64/Release/rexruntime.dll")
    if(FABLE2_VULKAN_PLUGIN_CANDIDATES AND FABLE2_VULKAN_RUNTIME_CANDIDATES)
        list(GET FABLE2_VULKAN_PLUGIN_CANDIDATES 0 FABLE2_VULKAN_PLUGIN)
        list(GET FABLE2_VULKAN_RUNTIME_CANDIDATES 0 FABLE2_VULKAN_RUNTIME)
        # Stage, next to the exe:
        #   rexgpu-xenos-vulkan.dll  - source dual-backend plugin (D3D12+Vulkan;
        #                              fable2.cmd copies it over rexgpu-xenos.dll)
        #   rexruntime-vulkan.dll    - source (both-backends) runtime
        #   rexruntime-d3d12.dll     - prebuilt 0.10.0 runtime (D3D12 backup)
        #   rexgpu-xenos-prebuilt.dll- prebuilt 0.10.0 D3D12 plugin (pristine
        #                              backup for `fable2.cmd prebuilt`)
        #   fable2.cmd               - launcher that picks the runtime + backend
        # The default rexruntime.dll (staged above by GPU_PLUGINS xenos) is the
        # prebuilt one, so a plain `fable_2.exe` launch stays D3D12 (0.10.0)
        # until fable2.cmd restages a source pair.
        add_custom_command(TARGET fable_2 POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FABLE2_VULKAN_PLUGIN}"  "$<TARGET_FILE_DIR:fable_2>/rexgpu-xenos-vulkan.dll"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FABLE2_VULKAN_RUNTIME}" "$<TARGET_FILE_DIR:fable_2>/rexruntime-vulkan.dll"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FABLE2_SDK_ROOT}/bin/rexruntime.dll" "$<TARGET_FILE_DIR:fable_2>/rexruntime-d3d12.dll"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FABLE2_SDK_ROOT}/bin/rexgpu-xenos.dll" "$<TARGET_FILE_DIR:fable_2>/rexgpu-xenos-prebuilt.dll"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/tools/fable2.cmd" "$<TARGET_FILE_DIR:fable_2>/fable2.cmd"
          COMMENT "Staging Vulkan plugin/runtime + D3D12 backups + launcher")
    else()
        message(STATUS "Vulkan SDK not fully built (need rexgpu-xenos.dll + rexruntime.dll under ${FABLE2_SDK_SRC}/out/win-amd64/Release) - run tools/build_sdk_vulkan.cmd to enable 'fable2.cmd vulkan'")
    endif()
endif()

# --- Vulkan renderer: Stage 1 smoke test ----------------------------------
# A self-contained Vulkan pipeline (vendored Khronos headers in
# thirdparty/vulkan + a dynamic vulkan-1.dll loader) that creates a window,
# inits Vulkan, and presents a clear color. Proves the Vulkan foundation in
# this repo before Stage 2 builds the Xenos->Vulkan command processor so the
# game itself renders. Standalone: no SDK/rexruntime dependency.
#   build: build.cmd fable_2_vulkan_smoke
if(WIN32)
    # GUI (WIN32) subsystem on purpose: a console-subsystem process is blocked
    # from creating top-level windows in this environment (CreateWindowExW
    # returns NULL with error 0), whereas the game (GUI/SDL) works.
    add_executable(fable_2_vulkan_smoke WIN32 src/vulkan_smoke.cpp)
    target_include_directories(fable_2_vulkan_smoke PRIVATE
      thirdparty/vulkan
      ${FABLE2_SDK_ROOT}/include
    )
    # Use the SDK's UI framework (rex::ui::Window / SDLWindowedAppContext) from
    # the SHARED rexruntime.dll — the game's proven window path. The SDL_*
    # symbols are NOT exported, but the rex::ui::* ones are, so link the import
    # lib and stage the DLL next to the exe.
    target_link_libraries(fable_2_vulkan_smoke PRIVATE
      ${FABLE2_SDK_ROOT}/lib/rexruntime.lib
      user32 gdi32
    )
    add_custom_command(TARGET fable_2_vulkan_smoke POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${FABLE2_SDK_ROOT}/bin/rexruntime.dll
              $<TARGET_FILE_DIR:fable_2_vulkan_smoke>
    )
endif()
