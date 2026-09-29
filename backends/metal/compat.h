#pragma once

// Keep the adapted XeniOS sources close to their original API. These aliases
// map platform and flag declarations onto ReXGlue; they are private to this
// plugin and must not leak into the game or the installed SDK.
#include <rex/cvar.h>
#include <rex/memory.h>
#include <rex/thread.h>
#include <rex/string/buffer.h>
namespace rex {
template <typename T> constexpr T clear_lowest_bit(T value) { return value & (value - 1); }
}
#define XE_PLATFORM_APPLE 1
#define XE_PLATFORM_MAC 1
#define XE_PLATFORM_MACOS 1
#define XE_PLATFORM_IOS 0
#define XE_PLATFORM_WIN32 0
#define XE_ARCH_ARM64 1
#define XE_TARGET_ARM64 1
#define XE_RESTRICT __restrict
#define XE_FORCEINLINE inline __attribute__((always_inline))
#define XE_NOINLINE __attribute__((noinline))
#define XE_NOALIAS
#define XE_COLD __attribute__((cold))
#define XE_LIKELY(x) __builtin_expect(!!(x), 1)
#define XE_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define DEFINE_bool(n,v,d,c) REXCVAR_DEFINE_BOOL(n,v,c,d)
#define DEFINE_int32(n,v,d,c) REXCVAR_DEFINE_INT32(n,v,c,d)
#define DEFINE_uint32(n,v,d,c) REXCVAR_DEFINE_UINT32(n,v,c,d)
#define DEFINE_int64(n,v,d,c) REXCVAR_DEFINE_INT64(n,v,c,d)
#define DEFINE_uint64(n,v,d,c) REXCVAR_DEFINE_UINT64(n,v,c,d)
#define DEFINE_double(n,v,d,c) REXCVAR_DEFINE_DOUBLE(n,v,c,d)
#define DEFINE_string(n,v,d,c) REXCVAR_DEFINE_STRING(n,v,c,d)
#define DEFINE_path(n,v,d,c) REXCVAR_DEFINE_STRING(n,v,c,d)
#define DECLARE_bool(n) REXCVAR_DECLARE(bool,n)
#define DECLARE_int32(n) REXCVAR_DECLARE(int32_t,n)
#define DECLARE_uint32(n) REXCVAR_DECLARE(uint32_t,n)
#define DECLARE_int64(n) REXCVAR_DECLARE(int64_t,n)
#define DECLARE_uint64(n) REXCVAR_DECLARE(uint64_t,n)
#define DECLARE_double(n) REXCVAR_DECLARE(double,n)
#define DECLARE_string(n) REXCVAR_DECLARE(std::string,n)
#define DECLARE_path(n) REXCVAR_DECLARE(std::string,n)
#define XE_COMPILER_HAS_GNU_EXTENSIONS 1
#define XE_ARCH_AMD64 0
#define XE_MAYBE_UNUSED [[maybe_unused]]
#define XE_HOST_CACHE_LINE_SIZE 128
#define xenia_assert assert_true
#include "xenia_helpers.h"
#include "metal_flags.h"

#define XELOGGPU REXLOG_DEBUG

#define XE_MSVC_OPTIMIZE_SMALL()
#define XE_MSVC_OPTIMIZE_REVERT()
