# native_inflate.cmake - host zlib-ng for src/core/hotfunc/zlib/NativeInflate.cpp
# (native replacement of the game's recompiled zlib 1.2.3 and 1.1.4 inflate).
# Included from the end of CMakeLists.txt. zlib-ng is fetched at configure
# time (needs git + network on the first configure) and built static, in its
# native zng_* API so it cannot clash with any other zlib in the link.
include(FetchContent)

if(DEFINED BUILD_SHARED_LIBS)
    set(_ni_saved_shared "${BUILD_SHARED_LIBS}")
endif()
set(BUILD_SHARED_LIBS OFF)
set(ZLIB_COMPAT OFF CACHE BOOL "" FORCE)
set(ZLIB_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ZLIBNG_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_GTEST OFF CACHE BOOL "" FORCE)
set(WITH_GZFILEOP OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(zlibng
    GIT_REPOSITORY https://github.com/zlib-ng/zlib-ng.git
    GIT_TAG 2.2.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zlibng)

if(DEFINED _ni_saved_shared)
    set(BUILD_SHARED_LIBS "${_ni_saved_shared}")
else()
    unset(BUILD_SHARED_LIBS)
endif()

target_link_libraries(fable_2 PRIVATE zlib)
