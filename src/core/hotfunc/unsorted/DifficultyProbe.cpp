// TEMPORARY research probe for the difficulty settings feature
// (docs/feature/difficulty_settings/). It lives on feature/game-difficulty only:
// remove this file, and the TEMPORARY fable2_probe_* midasm hooks in
// fable_2_manifest.toml, before any PR to master. Every line it logs starts with
// "[difficulty-probe]".
//
// Tier 1, why the game's own difficulty gates never fired:
//   A. LuaPlayer_SetDifficultyLevel_823C9458: which object the Lua binding writes,
//      compared with player 1's type-33 (player) component.
//   B. fable2_probe_gate_*: midasm hooks right after each gate's
//      `lwz r11,80(rX)` (rX = the type-33 component the gate found, r11 = the
//      level it read). They only run when the gate found a component.
//   C. (moved) The price, gold and multiplier overrides now belong to the feature,
//      src/core/hotfunc/difficulty/Difficulty.cpp, which logs "[difficulty]" lines.
//      fable2_probe_buy_factors / _sell_factors still run but nothing reads them.
//
// Tier 2, first calls per caller (a caller = the return address in lr):
//   F. HealthComp_Modify_8268BD90: the first 3 per caller and target kind.
//   G. Saves: SaveManager_Save_824E8990, LuaSaveManager_AutoSaveGame_824E8D28,
//      _SaveFailQuestSave_824E8D18, _LoadFailQuestSave_824ECCB8 and
//      PauseSaveBox_HandleEvent_82413988 (events 46/47 only).
//   H. FrontMenu_StartNewGame_8237A0C8 and FrontMenu_HandleCardEvent_826C9188.
//   I. FrontMenu_SelectLoadCard_826C9008.
//
// Component lookup (read in the generated code of Item_GetBuyPrice_828E7F78,
// Shopkeeper_GetBuyPrice_827AF5A0 and Shopkeeper_GetStockComponent_827B0358 for
// types 33, 47, 69 and 124): presence bit `type & 31` of the word at
// entity + 36 + 4 * (type >> 5); then either the fast table at entity+140 (a slot
// byte per type into the {type, comp} pairs at entity+72) or a binary search of
// those 8-byte pairs in [entity+72, entity+76).
#include "fable_2_pch.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <fmt/format.h>
#include <rex/logging/macros.h>
#include <rex/ppc/context.h>

#include "fable2_patches.h"  // fable2::patches::GuestBase

extern "C" void __imp__LuaPlayer_SetDifficultyLevel_823C9458(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__SaveManager_Save_824E8990(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__LuaSaveManager_AutoSaveGame_824E8D28(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__LuaSaveManager_SaveFailQuestSave_824E8D18(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__LuaSaveManager_LoadFailQuestSave_824ECCB8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__PauseSaveBox_HandleEvent_82413988(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__HealthComp_Modify_8268BD90(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__FrontMenu_StartNewGame_8237A0C8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__FrontMenu_HandleCardEvent_826C9188(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__FrontMenu_SelectLoadCard_826C9008(PPCContext& ctx, uint8_t* base);

static_assert(std::is_trivially_copyable_v<PPCContext>, "saved and restored by copy");

namespace {

// --- Guest memory (big-endian), usable from midasm hooks, which get no base ---

uint8_t* Mem(uint32_t a) {
  return fable2::patches::GuestBase() + a + (a >= 0xE0000000u ? 0x1000u : 0u);
}
uint32_t U32(uint32_t a) {
  uint32_t v;
  std::memcpy(&v, Mem(a), 4);
  return __builtin_bswap32(v);
}
uint8_t U8(uint32_t a) { return *Mem(a); }
float F32(uint32_t a) { return std::bit_cast<float>(U32(a)); }

// Only follow pointers the game itself would follow, and never page zero.
bool Ptr(uint32_t a) { return a >= 0x10000u; }

// Reads a short C string only from the XEX image, which is always mapped.
std::string ImageString(uint32_t a) {
  if (a < 0x82000000u || a >= 0x83700000u) return {};
  std::string s;
  for (uint32_t i = 0; i < 64; ++i) {
    const char c = static_cast<char>(U8(a + i));
    if (c == 0) break;
    if (c < 0x20 || c > 0x7E) return {};
    s.push_back(c);
  }
  return s;
}

uint32_t FindComponent(uint32_t ent, uint32_t type) {
  if (!Ptr(ent)) return 0;
  if (((U32(ent + 36 + 4 * (type >> 5)) >> (type & 31)) & 1u) == 0) return 0;
  const uint32_t vec = U32(ent + 72);
  const uint32_t end = U32(ent + 76);
  const uint32_t table = U32(ent + 140);
  if (!Ptr(vec)) return 0;
  if (Ptr(table)) return U32(vec + 8u * U8(table + type) + 4);
  uint32_t lo = vec;
  int32_t n = static_cast<int32_t>(end - vec) / 8;
  while (n > 0) {
    const int32_t half = n >> 1;
    const uint32_t mid = lo + 8u * static_cast<uint32_t>(half);
    if (static_cast<int32_t>(U32(mid)) < static_cast<int32_t>(type)) {
      lo = mid + 8;
      n -= half + 1;
    } else {
      n = half;
    }
  }
  return (lo != end && U32(lo) == type) ? U32(lo + 4) : 0;
}

// Player 1's entity, the way Item_GetBuyPrice_828E7F78 and Item_GetBuyOpinionFactor_828E8C80 get it:
// the player object *(*(*(*(*(0x83496920)+12)+88)+4)+0), then the weak handle at
// +104 (Player_GetEntity_8222C210, inlined in Item_GetBuyOpinionFactor_828E8C80).
uint32_t Player1Entity() {
  if (fable2::patches::GuestBase() == nullptr) return 0;
  uint32_t a = U32(0x83496920u);
  for (const uint32_t off : {12u, 88u, 4u, 0u}) {
    if (!Ptr(a)) return 0;
    a = U32(a + off);
  }
  if (!Ptr(a)) return 0;
  const uint32_t handle = U32(a + 108);
  return Ptr(handle) ? U32(handle) : 0;
}

// Counts calls per key; Take() is true for the first `limit` calls of a key.
class PerKey {
 public:
  bool Take(uint64_t key, uint32_t limit) {
    std::lock_guard lock(mu_);
    auto it = counts_.find(key);
    if (it == counts_.end()) {
      if (counts_.size() >= 512) return false;
      it = counts_.emplace(key, 0).first;
    }
    return ++it->second <= limit;
  }

 private:
  std::mutex mu_;
  std::unordered_map<uint64_t, uint32_t> counts_;
};

const char* Yes(bool b) { return b ? "yes" : "no"; }

// --- B. Gate hooks -----------------------------------------------------------

constexpr std::array<const char*, 5> kGateSite = {"DamageCaused(idx0)", "DamageReceived(idx1)",
                                                  "Experience(idx2)", "Buy(idx4)", "Sell(idx4)"};
std::array<std::atomic<uint32_t>, 5> g_gate_hits{};
std::array<std::atomic<int32_t>, 5> g_gate_level{};
std::array<std::atomic<uint32_t>, 5> g_gate_comp{};

// What the price wrappers print after the call (same thread as the hooks).
struct PriceTrace {
  bool gate = false;
  int32_t gate_level = 0;
  bool factors = false;
  double econ = 0, mult = 0, base = 0, shop = 0;
  uint32_t owned = 0;
};
thread_local PriceTrace t_price;

void GateHit(int site, uint32_t comp, int32_t level) {
  if (site >= 3) {
    t_price.gate = true;
    t_price.gate_level = level;
  }
  const uint32_t n = g_gate_hits[site].fetch_add(1);
  const int32_t prev_level = g_gate_level[site].exchange(level);
  const uint32_t prev_comp = g_gate_comp[site].exchange(comp);
  if (n < 3 || prev_level != level || prev_comp != comp) {
    const uint32_t owner = Ptr(comp) ? U32(comp + 4) : 0;
    const uint32_t p1 = Player1Entity();
    REXSYS_INFO(
        "[difficulty-probe] gate {} hit #{}: comp={:08X} owner={:08X} owner-is-p1={} "
        "level(+80)={} changed(+38)={}",
        kGateSite[site], n + 1, comp, owner, Yes(owner != 0 && owner == p1), level,
        Ptr(comp) ? U8(comp + 38) : 0);
  }
}

}  // namespace

void fable2_probe_gate_damage_caused(PPCRegister& r11, PPCRegister& r31) {
  GateHit(0, r31.u32, r11.s32);
}
void fable2_probe_gate_damage_received(PPCRegister& r10, PPCRegister& r11) {
  GateHit(1, r10.u32, r11.s32);
}
void fable2_probe_gate_experience(PPCRegister& r10, PPCRegister& r11) {
  GateHit(2, r10.u32, r11.s32);
}
void fable2_probe_gate_buy(PPCRegister& r10, PPCRegister& r11) { GateHit(3, r10.u32, r11.s32); }
void fable2_probe_gate_sell(PPCRegister& r10, PPCRegister& r11) { GateHit(4, r10.u32, r11.s32); }

void fable2_probe_buy_factors(PPCRegister& f1, PPCRegister& f26, PPCRegister& f27,
                              PPCRegister& f31, PPCRegister& r27, PPCRegister& /*r29*/) {
  t_price.factors = true;
  t_price.econ = f1.f64;
  t_price.mult = f26.f64;
  t_price.base = f27.f64;
  t_price.shop = f31.f64;
  t_price.owned = r27.u32 & 0xFF;
}

void fable2_probe_sell_factors(PPCRegister& f1, PPCRegister& f30, PPCRegister& f31,
                               PPCRegister& /*r29*/) {
  t_price.factors = true;
  t_price.econ = f1.f64;
  t_price.mult = f30.f64;
  t_price.base = f31.f64;
}

namespace {

PerKey g_health_calls;
std::atomic<uint32_t> g_card_events{0};
std::atomic<uint32_t> g_load_cards{0};

}  // namespace

// --- A. SetDifficultyLevel -----------------------------------------------------

extern "C" void LuaPlayer_SetDifficultyLevel_823C9458(PPCContext& __restrict ctx,
                                                     uint8_t* base) {
  const uint32_t obj = ctx.r3.u32;
  const int32_t level = ctx.r4.s32;
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const int32_t old_level = Ptr(obj) ? static_cast<int32_t>(U32(obj + 80)) : -1;
  const uint32_t obj_owner = Ptr(obj) ? U32(obj + 4) : 0;
  __imp__LuaPlayer_SetDifficultyLevel_823C9458(ctx, base);
  const uint32_t p1 = Player1Entity();
  const uint32_t p1c33 = FindComponent(p1, 33);
  REXSYS_INFO(
      "[difficulty-probe] SetDifficultyLevel from {:08X}: obj={:08X} level {} -> {} "
      "changed(+38)={} obj+4={:08X} | p1={:08X} p1-type33={:08X} obj-is-p1-type33={} "
      "p1-type33+80={}",
      caller, obj, old_level, level, Ptr(obj) ? U8(obj + 38) : 0, obj_owner, p1, p1c33,
      Yes(obj != 0 && obj == p1c33), Ptr(p1c33) ? static_cast<int32_t>(U32(p1c33 + 80)) : -1);
}

// --- F. Health -------------------------------------------------------------------

extern "C" void HealthComp_Modify_8268BD90(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  const uint32_t comp = ctx.r3.u32, source = ctx.r5.u32;
  const double delta = ctx.f1.f64;
  const uint32_t target = Ptr(comp) ? U32(comp + 4) : 0;
  const float before = Ptr(comp) ? F32(comp + 20) : 0.0f;
  __imp__HealthComp_Modify_8268BD90(ctx, base);
  const uint32_t p1 = Player1Entity();
  const bool target_p1 = target != 0 && target == p1;
  const bool source_p1 = source != 0 && source == p1;
  const uint64_t key = (uint64_t{lr} << 2) | (uint64_t{target_p1} << 1) | uint64_t{source_p1};
  if (g_health_calls.Take(key, 3)) {
    REXSYS_INFO(
        "[difficulty-probe] health {:+.2f} from {:08X} target={:08X} target-is-p1={} "
        "source={:08X} source-is-p1={}: {:.2f} -> {:.2f} (max {:.2f})",
        delta, lr, target, Yes(target_p1), source, Yes(source_p1), before, Ptr(comp) ? F32(comp + 20) : 0.0f, Ptr(comp) ? F32(comp + 28) : 0.0f);
  }
}

// --- G. Saves --------------------------------------------------------------------

extern "C" void SaveManager_Save_824E8990(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const uint32_t mgr = ctx.r3.u32;
  const uint32_t r4 = ctx.r4.u32, r5 = ctx.r5.u32, r6 = ctx.r6.u32, r7 = ctx.r7.u32;
  const uint32_t allowed = Ptr(mgr) ? U8(mgr + 2093) : 0xFFu;
  __imp__SaveManager_Save_824E8990(ctx, base);
  REXSYS_INFO(
      "[difficulty-probe] save from {:08X} r4={} r5={} r6={} r7={} allowed(+2093)={} -> r3={}",
      caller, r4, r5, r6, r7, allowed, ctx.r3.u32);
}

extern "C" void LuaSaveManager_AutoSaveGame_824E8D28(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  const uint32_t mgr = ctx.r3.u32;
  const uint32_t defer = ctx.r4.u32 & 0xFF, force = ctx.r5.u32 & 0xFF;
  const uint32_t last = Ptr(mgr) ? U32(mgr + 2008) : 0;
  const uint32_t pending = Ptr(mgr) ? U8(mgr + 2088) : 0;
  __imp__LuaSaveManager_AutoSaveGame_824E8D28(ctx, base);
  REXSYS_INFO(
      "[difficulty-probe] AutoSaveGame from {:08X} defer={} force={} pending(+2088)={} "
      "FrameLastAutoSaved(+2008)={} -> {}",
      caller, defer, force, pending, last, Ptr(mgr) ? U32(mgr + 2008) : 0);
}

extern "C" void LuaSaveManager_SaveFailQuestSave_824E8D18(PPCContext& __restrict ctx,
                                                         uint8_t* base) {
  REXSYS_INFO("[difficulty-probe] SaveFailQuestSave from {:08X}", static_cast<uint32_t>(ctx.lr));
  __imp__LuaSaveManager_SaveFailQuestSave_824E8D18(ctx, base);
}

extern "C" void LuaSaveManager_LoadFailQuestSave_824ECCB8(PPCContext& __restrict ctx,
                                                         uint8_t* base) {
  REXSYS_INFO("[difficulty-probe] LoadFailQuestSave from {:08X}", static_cast<uint32_t>(ctx.lr));
  __imp__LuaSaveManager_LoadFailQuestSave_824ECCB8(ctx, base);
}

extern "C" void PauseSaveBox_HandleEvent_82413988(PPCContext& __restrict ctx, uint8_t* base) {
  const int32_t event = ctx.r4.s32;
  if (event == 46 || event == 47) {
    REXSYS_INFO("[difficulty-probe] Save box event {} sub-code {} from {:08X}", event, ctx.r5.s32,
                static_cast<uint32_t>(ctx.lr));
  }
  __imp__PauseSaveBox_HandleEvent_82413988(ctx, base);
}

// --- H. New game ----------------------------------------------------------------

extern "C" void FrontMenu_StartNewGame_8237A0C8(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t r4 = ctx.r4.u32;
  std::string name = ImageString(r4);
  if (name.empty() && r4 >= 0x82000000u && r4 < 0x83700000u) name = ImageString(U32(r4));
  REXSYS_INFO("[difficulty-probe] StartNewGame from {:08X} r3={:08X} r4={:08X} \"{}\"",
              static_cast<uint32_t>(ctx.lr), ctx.r3.u32, r4, name);
  __imp__FrontMenu_StartNewGame_8237A0C8(ctx, base);
}

extern "C" void FrontMenu_HandleCardEvent_826C9188(PPCContext& __restrict ctx, uint8_t* base) {
  if (g_card_events.fetch_add(1) < 200) {
    // The event hash is at r4+4; the names are the globals at 0x834A0000 - offset.
    static constexpr std::array<std::pair<int32_t, const char*>, 8> kEvents = {{
        {-20456, "BOY_ACTIVE"},
        {-20452, "GIRL_ACTIVE"},
        {-20448, "BOY_SELECTED"},
        {-20444, "GIRL_SELECTED"},
        {-20440, "LOAD_CARD_ACTIVE"},
        {-20436, "LOAD_CARD_SELECTED"},
        {-20432, "UNFREEZE"},
        {-20428, "DIALOG_ACTIVE"},
    }};
    const uint32_t ev = ctx.r4.u32;
    const uint32_t hash = Ptr(ev) ? U32(ev + 4) : 0;
    const char* name = "?";
    for (const auto& [off, n] : kEvents) {
      if (hash != 0 && hash == U32(0x834A0000u + static_cast<uint32_t>(off))) name = n;
    }
    const uint32_t state = Ptr(ctx.r3.u32) ? U32(ctx.r3.u32 + 24) : 0;
    REXSYS_INFO("[difficulty-probe] card event {} (hash {:08X}) state(+24)={} from {:08X}", name,
                hash, state, static_cast<uint32_t>(ctx.lr));
  }
  __imp__FrontMenu_HandleCardEvent_826C9188(ctx, base);
}

// --- I. Slot label ----------------------------------------------------------------

extern "C" void FrontMenu_SelectLoadCard_826C9008(PPCContext& __restrict ctx, uint8_t* base) {
  if (g_load_cards.fetch_add(1) < 200) {
    REXSYS_INFO("[difficulty-probe] SelectLoadCard {} from {:08X}", ctx.r4.s32,
                static_cast<uint32_t>(ctx.lr));
  }
  __imp__FrontMenu_SelectLoadCard_826C9008(ctx, base);
}
