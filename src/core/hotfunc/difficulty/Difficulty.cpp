// Difficulty settings (docs/feature/difficulty_settings/, decision 6): applies the
// per-level multipliers to gold gained, prices, the damage the hero takes, enemy health
// and the XP the hero collects.
//
// The level is the game's own per-hero difficulty level: the int at +80 of player 1's
// type-33 (player) component, which LuaPlayer_SetDifficultyLevel_823C9458 writes and
// the save keeps. Game level 1 or lower is Easy (the original game, every multiplier
// 1.0), 2 Medium, 3 Hard, 4 or higher Hardcore. It is chosen on the New Game cards
// (after boy or girl) or changed in game with F8 (src/core/difficulty_dialog.h); the
// console still works: lua Player.SetDifficultyLevel(QuestManager.HeroEntity, 2)
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
//   - Enemy health: HealthComp_Modify_8268BD90, the hit handler's health change on a
//     target that is an enemy of player 1 takes health / enemy health (decision 7,
//     technical.md 21).
//
// Every line it logs starts with "[difficulty]".
#include "fable_2_pch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>  // VirtualQuery: the slot label checks guest addresses first
#endif

#include <rex/logging/macros.h>
#include <rex/ppc/context.h>

#include "difficulty.h"
#include "fable2_patches.h"   // fable2::patches::GuestBase
#include "fable2_ui_events.h" // FINISHED_TRANSITIONING_TO_GAME

extern "C" void __imp__FrontMenu_HandleCardEvent_826C9188(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__CopyAssign_RefCounted_821F0108(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Gui_FindElement_823F8C30(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Gui_GoToSlide_823F9140(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Speech_PlayByKey_82306098(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Handle_Release_822ADE08(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__GuiElement_SetText_823F8268(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Allocate_OrDbgBreak_8221F388(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__WideString_Assign_82B39DD8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Release_RefCounted_821C67D8(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Difficulty_GetMultiplier_823C92F0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__LuaExperience_Modify_8251C8E0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__LuaPlayer_SetDifficultyLevel_823C9458(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Item_GetBuyPrice_828E7F78(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Item_GetSellPrice_828E8320(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Property_GetBuyPrice_8274DD78(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Property_GetSellPrice_8274DFD0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__MoneyComp_Modify_8239E698(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__HealthComp_Modify_8268BD90(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__Faction_GetRelationship_822040D0(PPCContext& ctx, uint8_t* base);

namespace {

// --- The multipliers (overview decision 6) -----------------------------------------

using Multipliers = fable2::difficulty::LevelInfo;

// Subtitles: delta4o's choice of 2026-11-20 (overview, "The idea").
constexpr std::array<Multipliers, fable2::difficulty::kLevelCount> kLevels = {{
    {"Easy", "Little Sparrow", 1.0, 1.0, 1.0, 1.0, 1.0},
    {"Medium", "Hero of Albion", 0.85, 1.15, 1.5, 1.25, 0.85},
    {"Hard", "GuildMaster", 0.7, 1.3, 2.0, 1.5, 0.7},
    {"Hardcore", "Archon's Heir", 0.65, 1.4, 2.5, 2.0, 0.65},
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
        "[difficulty] level {} (game level {}): gold x{} prices x{} enemy damage x{} enemy "
        "health x{} XP x{}",
        m.name, game_level, m.gold, m.prices, m.enemy_damage, m.enemy_health, m.xp);
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

// --- Enemy health (decision 7, design A; technical.md 21) ------------------------------
//
// Combat_ApplyHit_8268AB30 takes a hit's health through HealthComp_Modify_8268BD90(r3 = the
// target's health component, f1 = -damage, r5 = the attacker); that call returns to
// 0x8268AF98. Scripted damage and kills, regeneration and damage over time come from other
// callers and stay as they are. A hit is divided by the level's enemy health when the target
// isn't a player (no type-33 component: player 1 and co-op player 2) and its faction regards
// player 1 as an enemy: Faction_GetRelationship_822040D0(target's type-133 component,
// player 1) == 4, the test Lua Faction.IsMyEnemy makes. So every attacker's hits count (the
// hero, the dog, henchmen, allies), and villagers, friendly guards and allies are left alone.
// The life-steal (0x8268B968) is worked out from the hit's damage before this, so it doesn't
// change. Health is a float, so nothing is rounded.

namespace {

constexpr uint32_t kHitHealthChange = 0x8268AF98u;  // in Combat_ApplyHit_8268AB30
constexpr int32_t kEnemy = 4;                       // Faction_GetRelationship_822040D0

// How the target's faction regards `other`: 1 neutral, 2 ally, 4 enemy; 0 without a
// faction component. Called at the start of HealthComp_Modify_8268BD90, before the callee
// has a frame, so the callee's frame goes below the caller's r1 as on a normal call.
int32_t Relationship(const PPCContext& ctx, uint8_t* base, uint32_t target, uint32_t other) {
  const uint32_t faction = FindComponent(target, 133);
  if (!Ptr(faction) || !Ptr(other)) return 0;
  PPCContext c = ctx;
  c.r3.u64 = faction;
  c.r4.u64 = other;
  __imp__Faction_GetRelationship_822040D0(c, base);
  return c.r3.s32;
}

}  // namespace

extern "C" void HealthComp_Modify_8268BD90(PPCContext& __restrict ctx, uint8_t* base) {
  if (static_cast<uint32_t>(ctx.lr) == kHitHealthChange && ctx.f1.f64 < 0.0) {
    const double health = Current().enemy_health;
    const uint32_t comp = ctx.r3.u32;
    const uint32_t target = Ptr(comp) ? U32(comp + 4) : 0;
    const uint32_t hero = Player1Entity();
    if (health != 1.0 && Ptr(target) && Ptr(hero) && !Ptr(FindComponent(target, 33))) {
      const int32_t rel = Relationship(ctx, base, target, hero);
      const double before = ctx.f1.f64;
      if (rel == kEnemy) {
        ctx.f1.f64 = static_cast<double>(static_cast<float>(before / health));
      }
      REXSYS_INFO("[difficulty] hit {:.1f} -> {:.1f} on {:08X} by {:08X} ({})", before, ctx.f1.f64,
                  target, ctx.r5.u32,
                  rel == kEnemy ? "enemy" : rel == 2 ? "ally, kept" : rel == 1 ? "neutral, kept" : "no faction, kept");
    }
  }
  __imp__HealthComp_Modify_8268BD90(ctx, base);
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

// --- The difficulty cards: New Game and in game ----------------------------------------
//
// New Game: the cards come between the gender choice and the start (delta4o's design).
// FrontMenu_HandleCardEvent_826C9188 starts the new game on BOY_SELECTED / GIRL_SELECTED:
// it builds the creature name ("CreatureHeroChild" / "CreatureHeroFemaleChild") and calls
// FrontMenu_StartNewGame_8237A0C8 with r3 = *(*(*(0x83496928)+8)+80). That path reads
// only the event's hash at +4, never r3 (Verified in the generated code). So the override
// holds the event back and opens the cards; once a card is chosen, the pad read replays
// the same handler with an 8-byte event of the same hash, built on the guest stack.
// The level is written to the new hero after FINISHED_TRANSITIONING_TO_GAME.
//
// Theresa's destiny line ("his/her destiny unfolds") plays when the difficulty card is
// chosen, not on the boy or girl choice: Speech_PlayByKey_82306098 skips the game's own
// calls for it, and the pad read plays it just before the replay, then waits as long as
// the game waited between the line and the SELECTED event. B on the cards puts the boy and
// girl cards back the way New Game shows them (technical.md 19.2).

namespace {

// Event hash and speech key globals at 0x834A0000 + offset (technical.md section 7).
constexpr int32_t kBoySelected = -20448;
constexpr int32_t kGirlSelected = -20444;
constexpr int32_t kChooseCard = -20424;       // the choosecard element name
constexpr int32_t kWhoIsTheHero = -20340;    // TEXT_QUEST_QC010_FRONT_END_20
constexpr int32_t kDestinyLineBoy = -20336;   // TEXT_QUEST_QC010_FRONT_END_30_HM
constexpr int32_t kDestinyLineGirl = -20332;  // TEXT_QUEST_QC010_FRONT_END_30_HF
uint32_t EventHash(int32_t offset) {
  return U32(0x834A0000u + static_cast<uint32_t>(offset));
}

// Return addresses of the game's Speech_PlayByKey_82306098 calls for the destiny line
// (Verified in the generated code): the card input on the boy / girl confirm, and the
// UNFREEZE replays for card states 3 / 4 after a dialog.
constexpr std::array<uint32_t, 4> kDestinyLineCalls = {
    0x826C7A50u,  // FrontMenu_HandleCardInput_826C72A8, boy (key -20336)
    0x826C7B98u,  // FrontMenu_HandleCardInput_826C72A8, girl (key -20332)
    0x826C9638u,  // FrontMenu_HandleUnfreeze_826C9460, state 3 (boy)
    0x826C96B0u,  // FrontMenu_HandleUnfreeze_826C9460, state 4 (girl)
};

bool IsDestinyLineCall(uint32_t lr) {
  for (const uint32_t a : kDestinyLineCalls) {
    if (a == lr) return true;
  }
  return false;
}

// The longest wait between our destiny line and the start of the game.
constexpr int64_t kMaxLineLeadMs = 4000;

size_t ThreadKey() { return std::hash<std::thread::id>{}(std::this_thread::get_id()); }

std::atomic<uint32_t> g_held_event{0};  // the held-back SELECTED hash, 0 = none
std::atomic<uint32_t> g_held_self{0};   // r3 of the held call
// The front-end object, as last seen by the card events (for the gender cards' prompts);
// 0 once a game is started or a hero exists, because the object may not outlive the menu.
std::atomic<uint32_t> g_front_end{0};
std::atomic<size_t> g_card_thread{0};   // the thread that sent it
std::atomic<int64_t> g_choice_ms{0};    // when the card was chosen
thread_local bool t_replaying = false;

std::atomic<int> g_newgame_level{-1};   // card index to write to the new hero
std::atomic<int64_t> g_newgame_ms{0};   // when the new game was started
std::atomic<uint32_t> g_hero_at_start{0};

// The destiny line (see the top of this section).
std::atomic<int64_t> g_line_held_ms{0};    // when the game's own line was skipped
std::atomic<int64_t> g_line_gap_ms{0};     // from then to the SELECTED event
std::atomic<int64_t> g_line_played_ms{0};  // when ours was played, 0 = not yet

// B on the New Game cards.
std::atomic<bool> g_back_requested{false};
std::atomic<int64_t> g_back_ms{0};

// Pad edges (pad-read thread only).
uint16_t g_prev_buttons = 0;
bool g_prev_left = false;
bool g_prev_right = false;

using GuestFn = void (*)(PPCContext&, uint8_t*);

// Calls fn(self, &event) with an 8-byte event {0, hash} on a fresh guest stack frame below
// the caller's: back chain at sp, the event at sp + 0x100. fn's own frame goes below sp.
void CallWithEvent(PPCContext& ctx, uint8_t* base, GuestFn fn, uint32_t self, uint32_t hash) {
  const PPCContext saved = ctx;
  const uint32_t sp = (ctx.r1.u32 - 0x200u) & ~0xFu;
  W32(sp, ctx.r1.u32);
  const uint32_t ev = sp + 0x100u;
  W32(ev, 0);
  W32(ev + 4, hash);
  ctx.r1.u64 = sp;
  ctx.r3.u64 = self;
  ctx.r4.u64 = ev;
  fn(ctx, base);
  ctx = saved;
}

void ReplayCardEvent(PPCContext& ctx, uint8_t* base, uint32_t self, uint32_t hash) {
  t_replaying = true;
  CallWithEvent(ctx, base, __imp__FrontMenu_HandleCardEvent_826C9188, self, hash);
  t_replaying = false;
}

// Plays one of Theresa's front-end lines the way the card input does (fable_2_recomp.0.cpp
// at 0x826C7A50): Speech_PlayByKey_82306098(r3 = an 8-byte handle on the stack, r4 =
// *(*(*(0x83496928)+8)+80), r5 = &key, r6 = 0), then Handle_Release_822ADE08(handle).
// The line goes on playing after the release.
bool PlayFrontEndLine(PPCContext& ctx, uint8_t* base, int32_t key_offset) {
  uint32_t speaker = U32(0x83496928u);
  for (const uint32_t off : {8u, 80u}) {
    if (!Ptr(speaker)) return false;
    speaker = U32(speaker + off);
  }
  const PPCContext saved = ctx;
  const uint32_t sp = (ctx.r1.u32 - 0x200u) & ~0xFu;
  W32(sp, ctx.r1.u32);
  const uint32_t handle = sp + 0x100u;
  W32(handle, 0);
  W32(handle + 4, 0);
  ctx.r1.u64 = sp;
  ctx.r3.u64 = handle;
  ctx.r4.u64 = speaker;
  ctx.r5.u64 = 0x834A0000u + static_cast<uint32_t>(key_offset);
  ctx.r6.u64 = 0;
  __imp__Speech_PlayByKey_82306098(ctx, base);
  ctx.r3.u64 = handle;
  __imp__Handle_Release_822ADE08(ctx, base);
  ctx = saved;
  return true;
}

// The front-end fields the card screen uses (technical.md 19.2), for the log.
std::string CardFields(uint32_t self) {
  if (!Ptr(self)) return "?";
  return fmt::format("+24={} +100={} +101={} +104={} +108={} +112={} +192={} +212={}",
                     static_cast<int32_t>(U32(self + 24)), U8(self + 100), U8(self + 101),
                     static_cast<int32_t>(U32(self + 104)), static_cast<int32_t>(U32(self + 108)),
                     static_cast<int32_t>(U32(self + 112)), U8(self + 192), U8(self + 212));
}

// B: put the boy and girl cards back, the way New Game shows them (FrontMenu_HandleEvent at
// 0x826C863C, fable_2_recomp.167.cpp; Verified in the generated code):
//   name = CopyAssign_RefCounted_821F0108(&tmp, &global choosecard name)
//   el = Gui_FindElement_823F8C30(name, gui = *(self+20))   (it destroys the name handle)
//   Gui_GoToSlide_823F9140(el, gui, 1)
//   self+24 = 2, then FE_20 ("who is the hero") through Speech_PlayByKey_82306098.
// The game's other way back, UNFREEZE through FrontMenu_HandleUnfreeze_826C9460, only works
// after a dialog: on the card screen +101 is 0, and UNFREEZE then left +24 = 0 without the
// slide or the line (seen in game 2026-11-26, fable_2_077.log).
void BackToGenderCardsNow(PPCContext& ctx, uint8_t* base, uint32_t self) {
  REXSYS_INFO("[difficulty] New Game: back to boy or girl ({})", CardFields(self));
  if (!Ptr(self)) return;
  const uint32_t gui = U32(self + 20);
  if (!Ptr(gui)) return;
  const PPCContext saved = ctx;
  const uint32_t sp = (ctx.r1.u32 - 0x200u) & ~0xFu;
  W32(sp, ctx.r1.u32);
  const uint32_t name = sp + 0x100u;
  W32(name, 0);  // CopyAssign releases what the slot held first
  ctx.r1.u64 = sp;
  ctx.r3.u64 = name;
  ctx.r4.u64 = 0x834A0000u + static_cast<uint32_t>(kChooseCard);
  __imp__CopyAssign_RefCounted_821F0108(ctx, base);
  ctx.r4.u64 = gui;  // r3 = the name handle, as CopyAssign returns it
  __imp__Gui_FindElement_823F8C30(ctx, base);
  const uint32_t element = ctx.r3.u32;
  if (Ptr(element)) {
    ctx.r3.u64 = element;
    ctx.r4.u64 = gui;
    ctx.r5.u64 = 1;
    __imp__Gui_GoToSlide_823F9140(ctx, base);
  }
  ctx = saved;
  if (!Ptr(element)) {
    REXSYS_INFO("[difficulty] New Game: choosecard element not found");
    return;
  }
  W32(self + 24, 2);
  const bool line = PlayFrontEndLine(ctx, base, kWhoIsTheHero);
  REXSYS_INFO("[difficulty] New Game: gender cards again ({}), FE_20 {}", CardFields(self),
              line ? "played" : "NOT played");
}

}  // namespace

extern "C" void FrontMenu_HandleCardEvent_826C9188(PPCContext& __restrict ctx, uint8_t* base) {
  if (!t_replaying) g_front_end.store(ctx.r3.u32);
  const uint32_t ev = ctx.r4.u32;
  const uint32_t hash = Ptr(ev) ? U32(ev + 4) : 0;
  const bool boy = hash != 0 && hash == EventHash(kBoySelected);
  const bool girl = hash != 0 && hash == EventHash(kGirlSelected);
  if ((boy || girl) && !t_replaying) {
    namespace d = fable2::difficulty;
    if (g_held_event.load() == 0) {
      const int64_t now = fable2::uievents::NowMs();
      const int64_t line = g_line_held_ms.exchange(0);
      g_line_gap_ms.store(line > 0 ? std::clamp<int64_t>(now - line, 0, kMaxLineLeadMs) : 0);
      g_line_played_ms.store(0);
      g_back_requested.store(false);
      g_held_self.store(ctx.r3.u32);
      g_card_thread.store(ThreadKey());
      d::Dialog().chosen.store(-1);
      d::Dialog().nav_x.store(0);
      d::Dialog().activate.store(0);
      d::Dialog().back.store(0);
      g_held_event.store(hash);
      d::Dialog().mode.store(int(d::DialogMode::kNewGame));
      REXSYS_INFO(
          "[difficulty] New Game: {} chosen, showing the difficulty cards (thread {:X}, destiny line "
          "{} ms before)",
          boy ? "boy" : "girl", ThreadKey(), line > 0 ? now - line : -1);
    }
    return;  // the game starts once a card is chosen (OnPadRead)
  }
  __imp__FrontMenu_HandleCardEvent_826C9188(ctx, base);
}

// r3 = an 8-byte sound handle to fill, r4 = the speaker, r5 = &key, r6 = 0. Every caller
// releases the handle right after (Handle_Release_822ADE08). The destiny line's calls
// are skipped: the handle stays empty, so the release does nothing, and the line plays
// when a difficulty card is chosen.
extern "C" void Speech_PlayByKey_82306098(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  if (IsDestinyLineCall(lr)) {
    const uint32_t handle = ctx.r3.u32;
    if (Ptr(handle)) {
      W32(handle, 0);
      W32(handle + 4, 0);
    }
    g_line_held_ms.store(fable2::uievents::NowMs());
    REXSYS_INFO("[difficulty] New Game: destiny line held back until a card is chosen (call {:08X})", lr);
    return;
  }
  __imp__Speech_PlayByKey_82306098(ctx, base);
}

namespace fable2::difficulty {

const LevelInfo& Level(int index) {
  return kLevels[static_cast<size_t>(std::clamp(index, 0, kLevelCount - 1))];
}

// The card input (FrontMenu_HandleCardInput_826C72A8) switches on +24 of the front-end
// object: 0 the main menu, 1 a card flipping, 2 the boy and girl cards arriving (New Game),
// 3 / 4 boy / girl highlighted (BOY_ACTIVE / GIRL_ACTIVE), 5 the load cards, 6 a dialog
// (Verified in the generated code). In 2-4 left / right, A and B act on the gender cards.
bool GenderCardsWaitingForInput() {
  const uint32_t fe = g_front_end.load();
  if (!Ptr(fe) || DialogOpen() || g_held_event.load() != 0) return false;
  const int32_t state = static_cast<int32_t>(U32(fe + 24));
  return state >= 2 && state <= 4 && CurrentIndex() < 0;
}

int CurrentIndex() {
  const uint32_t comp = FindComponent(Player1Entity(), 33);
  if (!Ptr(comp)) return -1;
  return static_cast<int>(LevelIndex(static_cast<int32_t>(U32(comp + 80))));
}

DialogState& Dialog() {
  static DialogState d;
  return d;
}

void RequestInGameDialog() {
  DialogState& d = Dialog();
  if (DialogOpen()) return;
  if (CurrentIndex() < 0) {
    REXSYS_INFO("[difficulty] F8: no hero yet, the cards only open in game");
    return;
  }
  d.chosen.store(-1);
  d.nav_x.store(0);
  d.activate.store(0);
  d.back.store(0);
  d.mode.store(int(DialogMode::kInGame));
  REXSYS_INFO("[difficulty] in-game cards opened (level {})", Level(CurrentIndex()).name);
}

void Choose(int index) {
  DialogState& d = Dialog();
  d.chosen_mode.store(d.mode.load());
  d.chosen.store(std::clamp(index, 0, kLevelCount - 1));
  g_choice_ms.store(fable2::uievents::NowMs());
  d.suppress_pad.store(true);  // the A that chose it must not reach the game
  d.mode.store(int(DialogMode::kClosed));
}

void CloseDialog() {
  DialogState& d = Dialog();
  if (d.mode.load() != int(DialogMode::kInGame)) return;  // New Game needs a choice
  d.suppress_pad.store(true);
  d.mode.store(int(DialogMode::kClosed));
  REXSYS_INFO("[difficulty] in-game cards closed, level unchanged");
}

void BackToGenderCards() {
  DialogState& d = Dialog();
  if (d.mode.load() != int(DialogMode::kNewGame)) return;
  g_back_ms.store(fable2::uievents::NowMs());
  g_back_requested.store(true);
  d.suppress_pad.store(true);  // the B must not reach the game (B there = back to the menu)
  d.mode.store(int(DialogMode::kClosed));
  REXSYS_INFO("[difficulty] New Game cards: B, back to boy or girl");
}

bool OnPadRead(PPCContext& ctx, uint8_t* base, uint16_t buttons, int16_t lx) {
  DialogState& d = Dialog();

  // New Game: act on the thread that sent the card event (after 2 s on any thread).
  const uint32_t held = g_held_event.load();
  if (held != 0 && !DialogOpen()) {
    const bool same_thread = ThreadKey() == g_card_thread.load();
    const int64_t now = fable2::uievents::NowMs();
    if (g_back_requested.load()) {
      // B: the boy and girl cards again.
      if (same_thread || now - g_back_ms.load() > 2000) {
        g_back_requested.store(false);
        g_line_played_ms.store(0);
        g_held_event.store(0);
        BackToGenderCardsNow(ctx, base, g_held_self.load());
      }
    } else if (d.chosen.load() >= 0 && d.chosen_mode.load() == int(DialogMode::kNewGame)) {
      // A card was chosen: the destiny line first, then, as long after it as the game
      // waited between the line and the SELECTED event, the start of the game.
      const int64_t gap = g_line_gap_ms.load();
      const bool late = now - g_choice_ms.load() > 2000 + gap;
      if (same_thread || late) {
        const int64_t played = g_line_played_ms.load();
        if (played == 0) {
          const bool boy = held == EventHash(kBoySelected);
          const bool ok = PlayFrontEndLine(ctx, base, boy ? kDestinyLineBoy : kDestinyLineGirl);
          g_line_played_ms.store(now);
          REXSYS_INFO("[difficulty] New Game: destiny line ({}) {}, the game starts in {} ms",
                      boy ? "boy" : "girl", ok ? "played" : "NOT played (no speaker)", gap);
        }
        if (played != 0 || gap == 0) {
          if (now - g_line_played_ms.load() >= gap) {
            const int level = d.chosen.exchange(-1);
            g_held_event.store(0);
            g_line_played_ms.store(0);
            g_hero_at_start.store(Player1Entity());
            g_newgame_level.store(level);
            g_newgame_ms.store(now);
            REXSYS_INFO("[difficulty] New Game on {}: starting the game ({} thread {:X}, card thread {:X})",
                        Level(level).name, same_thread ? "same" : "OTHER", ThreadKey(), g_card_thread.load());
            g_front_end.store(0);
            ReplayCardEvent(ctx, base, g_held_self.load(), held);
          }
        }
      }
    }
  }

  const bool left = (buttons & 0x0004) != 0 || lx < -16000;
  const bool right = (buttons & 0x0008) != 0 || lx > 16000;
  const uint16_t pressed = buttons & static_cast<uint16_t>(~g_prev_buttons);
  const bool left_edge = left && !g_prev_left;
  const bool right_edge = right && !g_prev_right;
  g_prev_buttons = buttons;
  g_prev_left = left;
  g_prev_right = right;

  if (DialogOpen()) {
    if (pressed & 0x1000) d.activate.fetch_add(1);                  // A
    if (pressed & (0x2000 | 0x0020)) d.back.fetch_add(1);           // B, Back
    if (left_edge) d.nav_x.fetch_sub(1);
    if (right_edge) d.nav_x.fetch_add(1);
    return true;
  }
  if (d.suppress_pad.load()) {
    if (buttons == 0 && lx > -7849 && lx < 7849) d.suppress_pad.store(false);
    return true;
  }
  return false;
}

// The hero's level as last seen in game, for the slot label's level file (written on
// the UI thread after a save).
std::atomic<int> g_last_level{-1};

void OnWorldUpdate(PPCContext& ctx, uint8_t* base) {
  if (CurrentIndex() >= 0) g_front_end.store(0);  // in game: the menu's object may be gone
  if (const int now = CurrentIndex(); now >= 0) g_last_level.store(now, std::memory_order_relaxed);

  // New Game: write the chosen level to the new hero once the game is on screen.
  if (const int level = g_newgame_level.load(); level >= 0) {
    const int64_t started = g_newgame_ms.load();
    const bool in_game = fable2::uievents::LastFired("FINISHED_TRANSITIONING_TO_GAME") > started;
    const uint32_t hero = Player1Entity();
    // Without the event, only trust a hero when none existed at the start (a fresh
    // boot); otherwise it may still be the hero of the game left before.
    const bool fresh = g_hero_at_start.load() == 0 && hero != 0;
    const uint32_t comp = FindComponent(hero, 33);
    if ((in_game || fresh) && Ptr(comp)) {
      g_newgame_level.store(-1);
      // The game saves straight after the start, before the next world update would see
      // the new level (seen 2026-11-27, fable_2_079.log: a Hard hero's first save got Easy).
      g_last_level.store(level, std::memory_order_relaxed);
      const int32_t before = static_cast<int32_t>(U32(comp + 80));
      const uint8_t changed = U8(comp + 38);
      if (level > 0) {
        // Directly, without the "changed" flag at +38 that Player.SetDifficultyLevel sets:
        // the Masochist question (open question 5) stays open.
        W32(comp + 80, static_cast<uint32_t>(level + 1));
        const uint32_t combat = FindComponent(hero, 57);
        if (Ptr(combat)) W32(combat + 2164, 0);  // recompute the damage-taken factor
      }
      REXSYS_INFO(
          "[difficulty] New Game: hero {:08X} had game level {} (changed flag {}), now {} ({}), "
          "{} ms after the start{}",
          hero, before, changed, level > 0 ? level + 1 : before, Level(level).name,
          fable2::uievents::NowMs() - started, in_game ? "" : " (no transition event seen)");
    }
  }

  // In game: a card chosen with F8.
  DialogState& d = Dialog();
  if (d.chosen_mode.load() == int(DialogMode::kInGame) && d.chosen.load() >= 0) {
    const int level = d.chosen.exchange(-1);
    const uint32_t comp = FindComponent(Player1Entity(), 33);
    const int now = CurrentIndex();
    if (level >= 0 && Ptr(comp) && now != 3 && level != 3 && level != now) {
      const PPCContext saved = ctx;
      ctx.r3.u64 = comp;
      ctx.r4.s64 = level + 1;
      LuaPlayer_SetDifficultyLevel_823C9458(ctx, base);  // the override: also clears the cache
      ctx = saved;
      g_last_level.store(level, std::memory_order_relaxed);
      REXSYS_INFO("[difficulty] in game: {} -> {}", Level(now).name, Level(level).name);
    } else if (level >= 0) {
      REXSYS_INFO("[difficulty] in game: {} kept", now >= 0 ? Level(now).name : "?");
    }
  }
}

// --- The slot label ----------------------------------------------------------------------

namespace {

constexpr const char* kLevelFile = "recomp_difficulty.txt";
std::mutex g_slots_mu;
std::filesystem::path g_save_root;
std::vector<std::pair<std::string, int>> g_slot_levels;  // slot name ("Hero 1") -> card index
const auto g_process_start = std::filesystem::file_time_type::clock::now();

int ParseLevel(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  for (int i = 0; i < kLevelCount; ++i) {
    if (s == Level(i).name) return i;
  }
  return 0;  // no file or unknown: Easy, the original game (decision 6)
}

// The slot name in a HeroNNN.header: UTF-16 big-endian from +8; the folder name in
// ASCII from +0x108 (read on disk 2026-11-24: "Hero 1" / "Hero000", "Hero 6" / "Hero005").
bool ReadHeader(const std::filesystem::path& file, std::string& name, std::string& folder) {
  std::ifstream in(file, std::ios::binary);
  std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (b.size() < 0x128) return false;
  name.clear();
  for (size_t i = 8; i + 1 < 0x108 && name.size() < 64; i += 2) {
    const unsigned c = (unsigned(b[i]) << 8) | b[i + 1];
    if (c == 0) break;
    if (c < 0x80) {
      name += char(c);
    } else if (c < 0x800) {
      name += char(0xC0 | (c >> 6));
      name += char(0x80 | (c & 0x3F));
    } else {
      name += char(0xE0 | (c >> 12));
      name += char(0x80 | ((c >> 6) & 0x3F));
      name += char(0x80 | (c & 0x3F));
    }
  }
  folder.clear();
  for (size_t i = 0x108; i < 0x128 && b[i] != 0; ++i) folder += char(b[i]);
  return !name.empty() && !folder.empty();
}

}  // namespace

void SetSaveRoot(const std::filesystem::path& root) {
  std::lock_guard<std::mutex> lock(g_slots_mu);
  g_save_root = root;
}

void PollSaveSlots() {
  static int64_t last = 0;
  const int64_t now_ms = fable2::uievents::NowMs();
  if (now_ms - last < 2000) return;
  last = now_ms;
  std::filesystem::path root;
  {
    std::lock_guard<std::mutex> lock(g_slots_mu);
    root = g_save_root;
  }
  if (root.empty()) return;

  namespace fs = std::filesystem;
  std::error_code ec;
  std::vector<std::pair<std::string, int>> table;
  const auto fnow = fs::file_time_type::clock::now();
  // A New Game level not written yet belongs to the hero being started.
  const int pending = g_newgame_level.load();
  const int level = pending >= 0 ? pending : g_last_level.load(std::memory_order_relaxed);
  for (const auto& profile : fs::directory_iterator(root, ec)) {
    const fs::path title = profile.path() / "4D5307F1";
    const fs::path data = title / "00000001";
    const fs::path headers = title / "Headers" / "00000001";
    std::error_code e2;
    if (!fs::is_directory(data, e2)) continue;

    // 1. A save written in this run, in the last 30 s, gets the hero's level next to it.
    if (level >= 0) {
      for (const auto& slot : fs::directory_iterator(data, e2)) {
        std::error_code e3;
        const auto saved = fs::last_write_time(slot.path() / "mainsave.bin", e3);
        if (e3 || saved < g_process_start || fnow - saved > std::chrono::seconds(30)) continue;
        const fs::path file = slot.path() / kLevelFile;
        const auto stamped = fs::last_write_time(file, e3);
        if (!e3 && stamped >= saved) continue;
        std::ofstream(file, std::ios::trunc) << Level(level).name << "\n";
        REXSYS_INFO("[difficulty] slot {}: saved on {}", slot.path().filename().string(), Level(level).name);
      }
    }

    // 2. The slot names and their levels, for the label.
    for (const auto& h : fs::directory_iterator(headers, e2)) {
      if (h.path().extension() != ".header") continue;
      std::string name, folder;
      if (!ReadHeader(h.path(), name, folder)) continue;
      std::ifstream in(data / folder / kLevelFile);
      std::string text;
      std::getline(in, text);
      table.emplace_back(name, ParseLevel(text));
    }
  }
  std::lock_guard<std::mutex> lock(g_slots_mu);
  g_slot_levels = std::move(table);
}

}  // namespace fable2::difficulty

// --- The slot label: "Hero 2 (Medium) / Bowerstone Old Town" ----------------------------
//
// The Continue screen's shared loadgame.SaveGameDescription text is set through
// GuiElement_SetText_823F8268(element r3, r4, &handle r5): the text object is
// *(*(element+4)), and when WideString_NotEqual_822D6998(&text+140, handle) says the text
// differs, RefCounted_Assign_822B85C8 makes the text object hold the handle's string (with
// its own reference) and marks it changed. FrontMenu_SelectLoadCard_826C9008 passes
// &names[card] with names = *(fe+120); opening the screen sets card 0 through another path
// that also ends here (seen in game 2026-11-24). A handle points to a ref-counted wide string
// {+0 UTF-16BE chars, +4 length, +8 (buffer size << 1) | flag, +12 refcount}, freed by
// Release_RefCounted_821C67D8 when the count reaches 0 (Verified in the generated code). The
// text is the whole card description: "Hero 2 / Bowerstone Old Town\nLiving as an orphan ...".
//
// Setting the GUI's textstring attribute has no visible effect. So, for a description of a
// known slot ("<slot name> / ...", the name from the headers), a NEW string with the level
// after the name is built the way the game's wide sprintf (WideString_Format_82B39260) builds
// one: a 16-byte record from Allocate_OrDbgBreak_8221F388, WideString_Assign_82B39DD8(record,
// chars, length), refcount 1. It is set through a handle of our own, which is released
// again, so the text object holds the only reference and the game frees the string when the
// text changes. The game's names[] list is never touched, so nothing is cut or shifted.
// Every read is checked first (a stale header's card pointed into text: "QC01" read as a
// pointer crashed an earlier try).

namespace {

#ifdef _WIN32
// The end of the committed, readable region holding a, or 0.
const uint8_t* HostRegionEnd(uint32_t a) {
  if (fable2::patches::GuestBase() == nullptr || a < 0x10000u) return nullptr;
  const uint8_t* p = Mem(a);
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT) return nullptr;
  const DWORD prot = mbi.Protect & 0xFF;
  if ((mbi.Protect & PAGE_GUARD) != 0 || prot == PAGE_NOACCESS) return nullptr;
  return static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
}
#else
const uint8_t* HostRegionEnd(uint32_t) { return nullptr; }
#endif

bool HostRange(uint32_t a, size_t n) {
  const uint8_t* end = HostRegionEnd(a);
  return end != nullptr && Mem(a) + n <= end;
}

uint16_t U16(uint32_t a) { return static_cast<uint16_t>((U8(a) << 8) | U8(a + 1)); }
void W16(uint32_t a, uint16_t v) {
  *Mem(a) = static_cast<uint8_t>(v >> 8);
  *Mem(a + 1) = static_cast<uint8_t>(v & 0xFF);
}

constexpr uint32_t kMaxChars = 1024;

// `handle` = the address of a string handle about to be set as some element's text. Returns
// the labelled text when it is a known slot's card description, else an empty vector.
std::vector<uint16_t> LabelledSlotText(uint32_t handle) {
  namespace d = fable2::difficulty;
  if ((handle & 3u) != 0 || !HostRange(handle, 4)) return {};
  const uint32_t rec = U32(handle);
  if ((rec & 3u) != 0 || !HostRange(rec, 16)) return {};
  const uint32_t chars = U32(rec);
  if ((chars & 1u) != 0) return {};
  const uint8_t* region_end = HostRegionEnd(chars);
  if (region_end == nullptr) return {};
  const uint32_t room = static_cast<uint32_t>(
      std::min<std::ptrdiff_t>(region_end - Mem(chars), std::ptrdiff_t{2} * kMaxChars) / 2);
  if (room < 2) return {};

  // Cheap filter first: the first character must start one of the slot names.
  std::vector<std::pair<std::string, int>> slots;
  {
    std::lock_guard<std::mutex> lock(d::g_slots_mu);
    slots = d::g_slot_levels;
  }
  const uint16_t first = U16(chars);
  bool maybe = false;
  for (const auto& [slot, lv] : slots) maybe = maybe || (!slot.empty() && static_cast<uint8_t>(slot[0]) == first);
  if (!maybe) return {};

  std::vector<uint16_t> wide;
  std::string text;  // ASCII view, for matching only
  for (uint32_t i = 0; i < room; ++i) {
    const uint16_t c = U16(chars + 2 * i);
    if (c == 0) break;
    wide.push_back(c);
    text += c < 0x80 ? static_cast<char>(c) : '?';
  }
  if (wide.empty() || wide.size() >= room) return {};
  // A card description: "<name> / <place>\n<text>".
  const size_t line_end = text.find('\n');
  const size_t name_end = text.find(" / ");
  if (line_end == std::string::npos || name_end == std::string::npos || name_end > line_end) return {};
  const std::string name = text.substr(0, name_end);
  int level = -1;
  for (const auto& [slot, lv] : slots) {
    if (slot == name) level = lv;
  }
  if (level < 0) return {};
  const std::string suffix = std::string(" (") + d::Level(level).name + ")";
  wide.insert(wide.begin() + static_cast<std::ptrdiff_t>(name_end), suffix.begin(), suffix.end());
  REXSYS_INFO("[difficulty] slot label: '{}{}' ({} characters)", name, suffix, wide.size());
  return wide;
}

}  // namespace

extern "C" void GuiElement_SetText_823F8268(PPCContext& __restrict ctx, uint8_t* base) {
  const std::vector<uint16_t> text = LabelledSlotText(ctx.r5.u32);
  if (text.empty()) {
    __imp__GuiElement_SetText_823F8268(ctx, base);
    return;
  }
  const PPCContext saved = ctx;
  const uint32_t len = static_cast<uint32_t>(text.size());
  // A fresh frame below the caller's: back chain at sp, our handle at sp + 0x100, the
  // characters from sp + 0x110. The callees' frames go below sp.
  const uint32_t frame = (0x110u + 2u * (len + 1u) + 0xFu) & ~0xFu;
  const uint32_t sp = (ctx.r1.u32 - frame - 0x100u) & ~0xFu;
  W32(sp, ctx.r1.u32);
  const uint32_t handle = sp + 0x100u;
  const uint32_t chars = sp + 0x110u;
  for (uint32_t i = 0; i < len; ++i) W16(chars + 2 * i, text[i]);
  W16(chars + 2 * len, 0);
  ctx.r1.u64 = sp;

  ctx.r3.u64 = 16;
  __imp__Allocate_OrDbgBreak_8221F388(ctx, base);
  const uint32_t rec = ctx.r3.u32;
  if (!Ptr(rec)) {
    ctx = saved;
    __imp__GuiElement_SetText_823F8268(ctx, base);
    return;
  }
  W32(rec, 0);
  W32(rec + 4, 0);
  W32(rec + 8, 0);
  ctx.r3.u64 = rec;
  ctx.r4.u64 = chars;
  ctx.r5.u64 = len;
  __imp__WideString_Assign_82B39DD8(ctx, base);  // buffer, +4 = len, +8, the terminator
  W32(rec + 12, 1);                              // our reference
  W32(handle, rec);

  ctx.r3.u64 = saved.r3.u64;
  ctx.r4.u64 = saved.r4.u64;
  ctx.r5.u64 = handle;
  __imp__GuiElement_SetText_823F8268(ctx, base);  // the text object takes its own reference
  ctx.r3.u64 = handle;
  __imp__Release_RefCounted_821C67D8(ctx, base);  // ours goes; the string lives on in the text
  ctx = saved;
}
