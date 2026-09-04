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
    set(FABLE2_SDK_ROOT "D:/projects/hacking/Windows/rexglue-sdk-0.10.0-win-amd64/win-amd64")
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
