// Native memcpy / memmove / memset for the game's CRT memory routines.
//
// These SDK-runtime routines are called from thousands of sites (memcpy
// 0x82CA2C60: ~1300 direct callers, memset 0x82CA3190: ~670) during loading
// and every frame. Recompiled, they copy 1-4 bytes per guest instruction;
// here they become one host memcpy/memset/memmove on guest RAM.
//
//   0x82CA2C60 MemCpy_SdkRuntime   memcpy(r3 dst, r4 src, r5 n) -> r3 = dst.
//                                  Saves dst with `std r3,-8(r1)` (done here too).
//   0x82CA30E8 MemCpy_Aligned      same copy, r3 left = dst, no stack write.
//   0x82CAA2E0 MemMove_SdkRuntime  memmove(r3 dst, r4 src, r5 n) -> r3 = dst.
//   0x82CA3190 MemSet_SdkRuntime   memset(r3 dst, r4 value, r5 n) -> r3 = dst.
//
// Fidelity: the guest memcpy copies forward, so an overlapping memcpy has a
// defined (if odd) result; those calls, and anything touching MMIO or
// crossing the 0xE0000000 physical-mapping boundary, run the original code.
// The guest memset fills the middle with a word built from r4 by two
// rlwimi; if the caller passed garbage above the low byte that word is not
// a byte repeat, so those calls run the original code too.
#include "fable_2_pch.h"

#include <cstring>

extern "C" void __imp__MemCpy_SdkRuntime_82CA2C60(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__MemCpy_Aligned_82CA30E8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__MemMove_SdkRuntime_82CAA2E0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__MemSet_SdkRuntime_82CA3190(PPCContext& ctx, uint8_t* base);

namespace {

// A guest range we can touch as one flat host block.
inline bool Flat(uint32_t a, uint32_t n) {
  if (n == 0) return true;
  const uint64_t end = uint64_t(a) + n;
  if (end > 0x100000000ull) return false;
  if (a < 0x80000000u && end > 0x7F000000u) return false;  // MMIO
  if (a < 0xE0000000u && end > 0xE0000000u) return false;  // physical offset changes
  return true;
}

inline bool Overlap(uint32_t d, uint32_t s, uint32_t n) {
  return n && (d < s ? uint64_t(d) + n > s : uint64_t(s) + n > d);
}

// The 32-bit fill word the guest memset builds from r4.
inline uint32_t FillWord(uint32_t v) {
  v = (v & ~0x0000FF00u) | ((v << 8) & 0x0000FF00u);
  v = (v & ~0xFFFF0000u) | ((v << 16) & 0xFFFF0000u);
  return v;
}

// Returns false when the call must run the original code.
inline bool NativeCopy(PPCContext& ctx, uint8_t* base) {
  const uint32_t d = ctx.r3.u32, s = ctx.r4.u32, n = ctx.r5.u32;
  if (Overlap(d, s, n) || !Flat(d, n) || !Flat(s, n)) return false;
  std::memcpy(REX_RAW_ADDR(d), REX_RAW_ADDR(s), n);
  return true;  // r3 stays = dst, as in both guest routines
}

}  // namespace

extern "C" void MemCpy_SdkRuntime_82CA2C60(PPCContext& __restrict ctx, uint8_t* base) {
  const uint64_t v = __builtin_bswap64(ctx.r3.u64);  // std r3,-8(r1)
  std::memcpy(REX_RAW_ADDR(ctx.r1.u32 - 8), &v, 8);
  if (!NativeCopy(ctx, base)) __imp__MemCpy_SdkRuntime_82CA2C60(ctx, base);
}

extern "C" void MemCpy_Aligned_82CA30E8(PPCContext& __restrict ctx, uint8_t* base) {
  if (!NativeCopy(ctx, base)) __imp__MemCpy_Aligned_82CA30E8(ctx, base);
}

extern "C" void MemMove_SdkRuntime_82CAA2E0(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t d = ctx.r3.u32, s = ctx.r4.u32, n = ctx.r5.u32;
  // dst < src (signed, like the guest's cmpw) tail-calls memcpy, which the
  // override above already handles - keep that path exactly.
  if (static_cast<int32_t>(d) < static_cast<int32_t>(s) || !Flat(d, n) || !Flat(s, n)) {
    __imp__MemMove_SdkRuntime_82CAA2E0(ctx, base);
    return;
  }
  std::memmove(REX_RAW_ADDR(d), REX_RAW_ADDR(s), n);
  ctx.r3.u64 = d;
}

extern "C" void MemSet_SdkRuntime_82CA3190(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t d = ctx.r3.u32, n = ctx.r5.u32;
  const uint32_t word = FillWord(ctx.r4.u32);
  const uint8_t byte = static_cast<uint8_t>(word);
  if (word != byte * 0x01010101u || !Flat(d, n)) {
    __imp__MemSet_SdkRuntime_82CA3190(ctx, base);
    return;
  }
  std::memset(REX_RAW_ADDR(d), byte, n);
}
