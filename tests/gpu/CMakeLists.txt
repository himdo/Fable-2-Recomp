# Opt-in GPU backend test (needs real GPU drivers). Configured against a staged
# runtime SDK by tests/run_gpu_tests.cmd; not part of the game build.
cmake_minimum_required(VERSION 3.25)
project(fable2_gpu_tests CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(rexglue CONFIG REQUIRED)

add_executable(test_gpu_backends test_gpu_backends.cpp)
target_include_directories(test_gpu_backends PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/core)
target_link_libraries(test_gpu_backends PRIVATE rex::runtime)
rexglue_apply_target_settings(test_gpu_backends)
# rexruntime (+ its runtime DLLs) and the GPU plugin, which LoadGpuPlugin
# looks for next to the executable.
add_custom_command(TARGET test_gpu_backends POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:test_gpu_backends>
            $<TARGET_FILE:rex::gpu-xenos>
            $<TARGET_FILE_DIR:test_gpu_backends>
    COMMAND_EXPAND_LISTS
    VERBATIM)
