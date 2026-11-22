// Difficulty settings (docs/feature/difficulty_settings/, decision 6): applies the
// per-level multipliers to gold gained, prices, the damage the hero takes and the XP
// the hero collects. Enemy health is not done yet (decision 0).
//
// The level is the game's own per-hero difficulty level: the int at +80 of player 1's
// type-33 (player) component, which LuaPlayer_SetDifficultyLevel_823C9458 writes and
// the save keeps. Game level 1 or lower is Easy (the original game, every multiplier
// 1.0), 2 Medium, 3 Hard, 4 or higher Hardcore. Until the New Game cards exist, set it
// from the console: lua Player.SetDifficultyLevel(QuestManager.HeroEntity, 2)
//
// Where each multiplier goes:
//   - Enemy damage and XP: Difficulty_GetMultiplier_823C92F0, which the game's own
//     gates only call above game level 1 (technical.md section 1). The damage taken is
//     damage x DamageReceived (Combat_GetDamageTakenFactor_8249F298 divides the factor
//     by it, the hit handler divides the damage by the factor). XP that a script hands
//     out (Lua Experience.Modify, only the Guild tutorial's 350 Will XP) is not scaled
//     (technical.md 17.1).
//   - Prices: the game's StockItemPrice gate never runs for the hero (16.3), so shop
//     prices are scaled at the return of Item_GetBuyPrice_828E7F78 (x) and
//     Item_GetSellPrice_828E8320 (/), property prices at the return of
//     Property_GetBuyPrice_8274DD78 (x) and Property_GetSellPrice_8274DFD0 (/).
//   - Gold: MoneyComp_Modify_8239E698, positive changes to player 1 only, except sales
//     (already priced), transfers, refunds and network copies (section 2).
//
// Every line it logs starts with "[difficulty]".
#include "fable_2_pch.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

#include <rex/logging/macros.h>
#include <rex/ppc/context.h>

#include "fable2_patches.h"  // fable2::patches::GuestBase

extern "C" void __imp__Difficulty_GetMultiplier_823C92F0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__LuaExperience_Modify_8251C8E0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__LuaPlayer_SetDifficultyLevel_823C9458(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Item_GetBuyPrice_828E7F78(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Item_GetSellPrice_828E8320(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Property_GetBuyPrice_8274DD78(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Property_GetSellPrice_8274DFD0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__MoneyComp_Modify_8239E698(PPCContext& ctx, uint8_t* base);

namespace {

// --- The multipliers (overview decision 6) -----------------------------------------

struct Multipliers {
  const char* name;
  double gold;          // gold gained
  double prices;        // shop items and property: buy x, sell /
  double enemy_damage;  // damage the hero takes
  double enemy_health;  // not applied yet
  double xp;            // XP the hero collects
};

constexpr std::array<Multipliers, 4> kLevels = {{
    {"Easy", 1.0, 1.0, 1.0, 1.0, 1.0},
    {"Medium", 0.85, 1.15, 1.5, 1.25, 0.85},
    {"Hard", 0.7, 1.3, 2.0, 1.5, 0.7},
    {"Hardcore", 0.65, 1.4, 2.5, 2.0, 0.65},
}};

// --- Guest memory (big-endian) ------------------------------------------------------

uint8_t* Mem(uint32_t a) {
  return fable2::patches::GuestBase() + a + (a >= 0xE0000000u ? 0x1000u : 0u);
}
uint32_t U32(uint32_t a) {
  uint32_t v;
  std::memcpy(&v, Mem(a), 4);
  return __builtin_bswap32(v);
}
uint8_t U8(uint32_t a) { return *Mem(a); }
void W32(uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(Mem(a), &v, 4);
}

// Only follow pointers the game itself would follow, and never page zero.
bool Ptr(uint32_t a) { return a >= 0x10000u; }

// An entity's component of the given type, as the game looks it up (read in the
// generated code of Item_GetBuyPrice_828E7F78 and Shopkeeper_GetStockComponent_827B0358):
// presence bit `type & 31` of the word at entity + 36 + 4 * (type >> 5), then the fast
// slot table at entity+140, or a binary search of the 8-byte {type, comp} pairs in
// [entity+72, entity+76).
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

// Player 1's entity: the player object *(*(*(*(*(0x83496920)+12)+88)+4)+0), then the
// weak handle at +104, whose record pointer sits at +108 (Player_GetEntity_8222C210,
// inlined the same way in Item_GetBuyOpinionFactor_828E8C80). Same as the probe's
// lookup, which matched the game's own player-1 checks in game (16.1).
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

// --- The current level --------------------------------------------------------------

std::atomic<int32_t> g_logged_level{-1};

size_t LevelIndex(int32_t game_level) {
  if (game_level <= 1) return 0;
  if (game_level >= 4) return 3;
  return static_cast<size_t>(game_level - 1);
}

const Multipliers& Current() {
  const uint32_t comp = FindComponent(Player1Entity(), 33);
  const int32_t game_level = Ptr(comp) ? static_cast<int32_t>(U32(comp + 80)) : 1;
  const Multipliers& m = kLevels[LevelIndex(game_level)];
  if (Ptr(comp) && g_logged_level.exchange(game_level) != game_level) {
    REXSYS_INFO(
        "[difficulty] level {} (game level {}): gold x{} prices x{} enemy damage x{} XP x{}",
        m.name, game_level, m.gold, m.prices, m.enemy_damage, m.xp);
  }
  return m;
}

// --- Scaling helpers ----------------------------------------------------------------

// Rounds to the nearest coin, as the game's own prices do (16.3), and never turns a
// positive amount into 0.
int32_t Scale(int32_t amount, double factor) {
  if (amount <= 0 || factor == 1.0) return amount;
  const long v = std::lround(static_cast<double>(amount) * factor);
  return v < 1 ? 1 : static_cast<int32_t>(v);
}

// The int result in r3, the way the game's own `lwz r3` leaves it.
void SetResult(PPCContext& ctx, int32_t v) { ctx.r3.u64 = static_cast<uint32_t>(v); }

// Set while Lua Experience.Modify runs on this thread: that XP is not scaled.
thread_local bool t_scripted_xp = false;

// Return addresses of MoneyComp_Modify_8239E698 calls whose gains are not scaled
// (found in the decrypted image; technical.md section 2 and 17).
constexpr std::array<uint32_t, 8> kUnscaledGoldCallers = {
    0x827AFCA4u,  // Shopkeeper_SellItem_827AFB58: already / prices
    0x828B0438u,  // Ownership_SellProperty_828B01C8: already / prices
    0x8239EA5Cu,  // MoneyComp_Modify_8239E698: the co-op share recursion
    0x8239EC84u,  // MoneyComp_Modify_8239E698: the co-op share recursion
    0x8241394Cu,  // Coop_MoveGoldToPlayer1_82413620: transfer
    0x823106ACu,  // Network_DropClient_8230FEB0: transfer
    0x8238D5F8u,  // OrbGift_Apply_8238D4A8: a gift from another player, or a refund
    0x82380C34u,  // Network_HandleMessage_8237DE18: gold copied from the other machine
};

bool IsUnscaledGoldCaller(uint32_t lr) {
  for (const uint32_t a : kUnscaledGoldCallers) {
    if (a == lr) return true;
  }
  return false;
}

}  // namespace

// --- The level changes ----------------------------------------------------------------

// r3 = the type-33 component, r4 = the new level. Combat_GetDamageTakenFactor_8249F298
// caches its result at +2164 of the entity's type-57 component and only recomputes it
// while that is 0.0 (the game clears it on load). Clearing it here makes the next hit
// use the new level's DamageReceived value (Verified in the generated code; seen in game
// 2026-11-22: one gate read, then cached hits).
extern "C" void LuaPlayer_SetDifficultyLevel_823C9458(PPCContext& __restrict ctx,
                                                     uint8_t* base) {
  const uint32_t comp = ctx.r3.u32;
  const int32_t old_level = Ptr(comp) ? static_cast<int32_t>(U32(comp + 80)) : -1;
  const int32_t new_level = ctx.r4.s32;
  __imp__LuaPlayer_SetDifficultyLevel_823C9458(ctx, base);
  const uint32_t ent = Ptr(comp) ? U32(comp + 4) : 0;
  const uint32_t combat = FindComponent(ent, 57);
  if (Ptr(combat)) W32(combat + 2164, 0);  // 0.0f: recompute the damage-taken factor
  REXSYS_INFO("[difficulty] SetDifficultyLevel {} -> {} on {:08X} (player 1: {})", old_level,
              new_level, ent, ent != 0 && ent == Player1Entity() ? "yes" : "no");
}

// --- Enemy damage and XP: the game's own gates --------------------------------------

// r3 = the index (0 DamageCaused, 1 DamageReceived, 2 ExperienceReceived,
// 3 SpellManaCost, 4 StockItemPrice); the float result goes in f1. The shipped table
// is all 1.0 (16.2), so nothing of the original is lost by not calling it.
extern "C" void Difficulty_GetMultiplier_823C92F0(PPCContext& __restrict ctx, uint8_t* base) {
  (void)base;
  const Multipliers& m = Current();
  double v = 1.0;
  switch (ctx.r3.u32) {
    case 1:
      v = m.enemy_damage;
      break;
    case 2:
      v = t_scripted_xp ? 1.0 : m.xp;
      break;
    default:
      break;  // the hero's own damage, mana cost and the (unused) price stay as they are
  }
  ctx.f1.f64 = static_cast<double>(static_cast<float>(v));
}

extern "C" void LuaExperience_Modify_8251C8E0(PPCContext& __restrict ctx, uint8_t* base) {
  const bool previous = t_scripted_xp;
  t_scripted_xp = true;
  __imp__LuaExperience_Modify_8251C8E0(ctx, base);
  t_scripted_xp = previous;
}

// --- Prices ---------------------------------------------------------------------------

// Each pair calls its partner internally: Item_GetSellPrice_828E8320 clamps against
// Item_GetBuyPrice_828E7F78, and Property_GetBuyPrice_8274DD78 floors at
// Property_GetSellPrice_8274DFD0. Those inner calls reach these overrides too, so while
// one price is being worked out the inner one returns the game's own value, and the
// multiplier is applied once, to the final result. Without this, an owned shop's sell
// price (owner bonus x1.4) clamped against the scaled buy price and came out unchanged
// on Hardcore (seen in game 2026-11-22: diamond sell 3,938 on Hardcore).
thread_local int t_price_depth = 0;

class PriceScope {
 public:
  PriceScope() : outer_(t_price_depth++ == 0) {}
  ~PriceScope() { --t_price_depth; }
  bool outer() const { return outer_; }

 private:
  bool outer_;
};

extern "C" void Item_GetBuyPrice_828E7F78(PPCContext& __restrict ctx, uint8_t* base) {
  const PriceScope scope;
  __imp__Item_GetBuyPrice_828E7F78(ctx, base);
  if (scope.outer()) SetResult(ctx, Scale(ctx.r3.s32, Current().prices));
}

extern "C" void Item_GetSellPrice_828E8320(PPCContext& __restrict ctx, uint8_t* base) {
  const PriceScope scope;
  __imp__Item_GetSellPrice_828E8320(ctx, base);
  if (scope.outer()) SetResult(ctx, Scale(ctx.r3.s32, 1.0 / Current().prices));
}

extern "C" void Property_GetBuyPrice_8274DD78(PPCContext& __restrict ctx, uint8_t* base) {
  const PriceScope scope;
  __imp__Property_GetBuyPrice_8274DD78(ctx, base);
  if (scope.outer()) SetResult(ctx, Scale(ctx.r3.s32, Current().prices));
}

extern "C" void Property_GetSellPrice_8274DFD0(PPCContext& __restrict ctx, uint8_t* base) {
  const PriceScope scope;
  __imp__Property_GetSellPrice_8274DFD0(ctx, base);
  if (scope.outer()) SetResult(ctx, Scale(ctx.r3.s32, 1.0 / Current().prices));
}

// --- Gold gained ----------------------------------------------------------------------

// r3 = the money component, r4 = delta (section 2). Only positive changes to player 1
// are scaled; network replays (byte +107 of the owner's type-91 component) only add
// and clamp, and were already scaled on the machine that sent them.
extern "C" void MoneyComp_Modify_8239E698(PPCContext& __restrict ctx, uint8_t* base) {
  const int32_t delta = ctx.r4.s32;
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  if (delta > 0 && !IsUnscaledGoldCaller(lr)) {
    const uint32_t comp = ctx.r3.u32;
    const uint32_t owner = Ptr(comp) ? U32(comp + 4) : 0;
    const uint32_t net = FindComponent(owner, 91);
    const bool replay = Ptr(net) && U8(net + 107) != 0;
    if (owner != 0 && owner == Player1Entity() && !replay) {
      const double gold = Current().gold;
      if (gold != 1.0) {
        const int32_t scaled = Scale(delta, gold);
        REXSYS_INFO("[difficulty] gold {:+} -> {:+} from {:08X}", delta, scaled, lr);
        ctx.r4.s64 = scaled;
      }
    }
  }
  __imp__MoneyComp_Modify_8239E698(ctx, base);
}
