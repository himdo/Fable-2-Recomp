// TEMPORARY research probe for the difficulty settings feature (context/difficulty
// settings/). Not for commit: delete after the in-game checks.
//
// 1. sub_823C92F0, the HeroCombatBalanceParams.DifficultyLevelMultipliers reader
//    (r3 = field index 0..4, returns f1). Its five callers only call it when the
//    hero's level (player component +80) is above 1. On the first call it reads
//    all five fields once and logs them; it also logs the first call per index
//    with its caller (lr).
// 2. sub_824E8990, where every save goes (r3 = save manager). Logs every call
//    with its caller and arguments, to identify the native autosave callers
//    (region change, sleep, property). Saves are rare, so logging each is fine.
#include "fable_2_pch.h"

#include <array>
#include <atomic>
#include <type_traits>

#include <rex/logging/macros.h>

extern "C" void __imp__sub_823C92F0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_824E8990(PPCContext& ctx, uint8_t* base);

static_assert(std::is_trivially_copyable_v<PPCContext>, "saved and restored by copy");

namespace {
constexpr std::array<const char*, 5> kFields = {"DamageCaused", "DamageReceived",
                                                "ExperienceReceived", "SpellManaCost",
                                                "StockItemPrice"};
std::atomic<bool> g_table_logged{false};
std::array<std::atomic<bool>, 5> g_idx_seen{};
}  // namespace

extern "C" void sub_823C92F0(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t idx = ctx.r3.u32;
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  if (!g_table_logged.exchange(true)) {
    std::array<double, 5> v{};
    for (uint32_t i = 0; i < 5; ++i) {
      PPCContext saved = ctx;
      ctx.r3.u64 = i;
      __imp__sub_823C92F0(ctx, base);
      v[i] = ctx.f1.f64;
      saved.fpscr = ctx.fpscr;  // keep the live flush mode, as GuestFrame does
      ctx = saved;
    }
    REXSYS_INFO(
        "[difficulty-probe] DifficultyLevelMultipliers: {}={:.4f} {}={:.4f} {}={:.4f} "
        "{}={:.4f} {}={:.4f}",
        kFields[0], v[0], kFields[1], v[1], kFields[2], v[2], kFields[3], v[3], kFields[4], v[4]);
  }
  __imp__sub_823C92F0(ctx, base);
  if (idx < 5 && !g_idx_seen[idx].exchange(true)) {
    REXSYS_INFO("[difficulty-probe] first read of {} (idx {}) from {:08X} -> {:.4f}",
                kFields[idx], idx, caller, ctx.f1.f64);
  }
}

extern "C" void sub_824E8990(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const uint32_t mgr = ctx.r3.u32;
  const uint32_t r4 = ctx.r4.u32, r5 = ctx.r5.u32, r6 = ctx.r6.u32, r7 = ctx.r7.u32;
  const uint32_t allowed = mgr != 0 ? REX_LOAD_U8(mgr + 2093) : 0xFFu;
  __imp__sub_824E8990(ctx, base);
  REXSYS_INFO(
      "[difficulty-probe] save from {:08X} r4={} r5={} r6={} r7={} allowed(+2093)={} -> r3={}",
      caller, r4, r5, r6, r7, allowed, ctx.r3.u32);
}
