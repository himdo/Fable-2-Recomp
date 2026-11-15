// With realtime_texture_morphing on, the render loop's morph release
// (8235E0D8 at 0x8236C7E8) flushes the pending morph jobs after the final
// pass, and their EDRAM draws clobber the frame Present resolves (a band on
// the last loading-screen frame). Run it right after Present instead.
#pragma once

#include <cstdint>

#include <rex/ppc/context.h>

#include "fable2_config.h"

namespace fable2::morph_release_defer {

struct Pending {
  bool set = false;
  uint32_t r3 = 0;
  uint32_t r4 = 0;
};

inline thread_local Pending g_pending;

}  // namespace fable2::morph_release_defer

extern "C" void ProcessAndProcessAndProcess1410_8235E0D8(PPCContext& ctx,
                                                         uint8_t* base) {
  auto& p = fable2::morph_release_defer::g_pending;
  if (uint32_t(ctx.lr) == 0x8236C7E8 && !p.set &&
      fable2::config::Get().realtime_texture_morphing) {
    p = {true, ctx.r3.u32, ctx.r4.u32};
    return;
  }
  __imp__ProcessAndProcessAndProcess1410_8235E0D8(ctx, base);
}

extern "C" void ProcessAndProcessAndProcess1265_82B69488(PPCContext& ctx,
                                                         uint8_t* base) {
  __imp__ProcessAndProcessAndProcess1265_82B69488(ctx, base);
  auto& p = fable2::morph_release_defer::g_pending;
  if (!p.set) return;
  p.set = false;
  const uint64_t r3 = ctx.r3.u64, r4 = ctx.r4.u64, lr = ctx.lr;
  ctx.r3.u64 = p.r3;
  ctx.r4.u64 = p.r4;
  ctx.lr = 0x8236C7E8;
  __imp__ProcessAndProcessAndProcess1410_8235E0D8(ctx, base);
  ctx.r3.u64 = r3;
  ctx.r4.u64 = r4;
  ctx.lr = lr;
}
