cmake_minimum_required(VERSION 3.25)
project(FableMetalGame LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
if(NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  message(FATAL_ERROR "The Metal backend requires Apple Silicon")
endif()
if(NOT FABLE2_METAL_DEPS)
  message(FATAL_ERROR "Run tools/build_macos_metal.py to prepare pinned Metal dependencies")
endif()
find_package(rexglue CONFIG REQUIRED)
set(SDK "${FABLE2_METAL_DEPS}/sdk-source")

set(DEPS "${FABLE2_METAL_DEPS}")
set(MSC "${DEPS}/metal-shader-converter")
set(DXC "${DEPS}/DirectXShaderCompiler")
include(common-sources.cmake)
include(metal-sources.cmake)
add_library(rexgpu-metal SHARED ${COMMON_SOURCES} ${METAL_SOURCES} plugin_main.cpp metal_flags.cpp)
set_target_properties(rexgpu-metal PROPERTIES CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
target_include_directories(rexgpu-metal PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/port" "${FABLE2_METAL_DEPS}/generated"
    "${SDK}/include" "${SDK}" "${CMAKE_CURRENT_SOURCE_DIR}" "${FABLE2_METAL_DEPS}"
    "${MSC}/include" "${MSC}/include/metal_irconverter" "${MSC}/include/metal_irconverter_runtime"
    "${DXC}/include" "${DXC}/projects/dxilconv/include")
target_compile_definitions(rexgpu-metal PRIVATE REX_PLATFORM_MAC=1 METAL_SHADER_CONVERTER_AVAILABLE=1
    IR_RUNTIME_METALCPP=1 REXGLUE_BUILD_CONFIG="$<CONFIG>")
target_compile_options(rexgpu-metal PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/compat.h"
    -fno-strict-aliasing -ffp-model=strict -fno-char8_t -fblocks -ferror-limit=8)
target_link_libraries(rexgpu-metal PRIVATE rex::runtime rex::xxhash
    "${FABLE2_METAL_DEPS}/build-dxilconv/lib/libdxilconv.dylib" "${MSC}/lib/libmetalirconverter.dylib"
    "-framework Metal" "-framework Foundation" "-framework AppKit"
    "-framework QuartzCore" "-framework MetalKit" "-framework MetalFX")
