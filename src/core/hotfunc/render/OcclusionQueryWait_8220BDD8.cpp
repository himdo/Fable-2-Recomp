// Hand-written override: OcclusionQueryWait (sub_8220BDD8), sleeping instead of
// spinning while the GPU finishes.
//
// What the guest does. Two render-side loops (sub_8220BCF8, sub_82B26AE0)
// walk arrays of 12-byte occlusion query records and call this function for
// each one. It polls D3D's GetData for the record's query (sub_8220BE78, a
// 16-byte result at r1+80) until GetData returns 0 = finished, then stores the
// visible percentage `samples x 100 / total` into bits 19..25 of the record's
// first word and returns 1 if any sample passed. GetData reports "not
// finished" until the GPU has executed the query and the command processor
// has written its result back. In Bowerstone Old Town that is about one wait
// per frame, spinning through ~137,000 GetData calls (~7 ms, ~40% of the
// "3D Engine" thread). The Deck's APU is power-limited, so the spin also
// takes power from the GPU.
//
// This keeps the guest's behaviour - same GetData calls, same result, the
// game still waits for the real query result (answering early was tried and
// rejected: experiments/OcclusionQueryWait_8220BDD8.cpp.rejected, culling
// flicker) - but after a short burst of polls it sleeps
// [perf] occlusion_wait_sleep_us between polls, so the result is seen at most
// about that much (plus timer slack) later. 0 = the original spin.

#include "fable_2_pch.h"

#include <rex/cvar.h>

#include <cstdint>

#include "../hotfunc_idle_sleep.h"

REXCVAR_DEFINE_INT32(occlusion_wait_sleep_us, 50, "Perf",
                     "While the game waits for an occlusion query result, sleep this many "
                     "microseconds between polls (after a short burst of polls) instead of "
                     "spinning. 0 = spin like the original")
    .range(0, 1000)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

extern "C" void sub_8220BE78(PPCContext& ctx, uint8_t* base);

namespace {

constexpr uint32_t kDoneBit = 0x80000000u;      // record word 0: result already taken
constexpr uint32_t kPercentMask = 0x03F80000u;  // record word 0: bits 19..25
constexpr uint32_t kPollsBeforeSleep = 256;

// One GetData call with the guest's frame layout (112-byte frame, the 16-byte
// result at r1+80). Returns GetData's r3; the result words are copied to
// out[4]. Leaves r1 as it was.
uint32_t GetDataOnce(PPCContext& ctx, uint8_t* base, uint32_t query, uint32_t out[4]) {
  const uint64_t r1 = ctx.r1.u64;
  ctx.r1.u64 = r1 - 112;
  const uint32_t buf = ctx.r1.u32 + 80;
  ctx.r3.u64 = query;
  ctx.r4.u64 = buf;
  ctx.r5.s64 = 16;
  ctx.r6.s64 = 0;
  ctx.lr = 0x8220BE28;
  sub_8220BE78(ctx, base);
  const uint32_t status = ctx.r3.u32;
  for (int i = 0; i < 4; ++i) out[i] = REX_LOAD_U32(buf + 4 * i);
  ctx.r1.u64 = r1;
  return status;
}

// The guest epilogue reloads the return address into r12 and lr.
inline void Return(PPCContext& ctx, uint64_t lr, uint32_t ret) {
  ctx.r12.u64 = uint32_t(lr);
  ctx.lr = lr;
  ctx.r3.u64 = ret;
}

}  // namespace

extern "C" void sub_8220BDD8(PPCContext& __restrict ctx, uint8_t* base) {
  const uint64_t lr = ctx.lr;
  const uint32_t record = ctx.r3.u32;

  if (REX_LOAD_U32(record + 0) & kDoneBit) {
    Return(ctx, lr, 1);
    return;
  }
  const uint32_t query = REX_LOAD_U32(record + 4);

  uint32_t out[4];
  uint32_t polls = 0;
  while (GetDataOnce(ctx, base, query, out) != 0) {
    if (++polls >= kPollsBeforeSleep) {
      const int32_t sleep_us = REXCVAR_GET(occlusion_wait_sleep_us);
      if (sleep_us > 0) {
        fable2::hotfunc::IdleSleepMicros(sleep_us);
      }
    }
  }

  // Finished: the guest's result handling.
  // out[0] = r1+80 (any sample passed), out[1] = r1+84 (passed), out[3] = r1+92 (total).
  if (out[3] != 0) {
    const uint32_t percent = uint32_t(out[1] * 100u) / out[3];  // mulli + divwu: 32-bit
    const uint32_t w0 = REX_LOAD_U32(record + 0);
    REX_STORE_U32(record + 0, (w0 & ~kPercentMask) | ((percent << 19) & kPercentMask));
  }
  Return(ctx, lr, out[0] != 0 ? 1u : 0u);
}
