// fable_2 - Live minimap, game side: the region's map record, the hero and the
// dog, read after each world update for the overlay (src/core/minimap_dialog.h),
// and the Back (View) button that shows and hides it.
//
// Included once, from main.cpp, after src/core/fable2_photo_mode.h (whose
// world-update and pad-read overrides call in here).
//
// How the pause menu's map works (it is the same data):
//   The pause screen (vtable 0x820B06E8) fills Foreground.Pages.Contents.MiniMap
//   in slot 2, sub_8240DD98. sub_82410740(this, &tex, &north, &iconRot,
//   &iconXYZ (16-byte vector), &iconScale, &hasProperMinimap) reads the current
//   level's MinimapInfo record (regionmap_<level>_<scenario> in globals.gdb:
//   MinimapBackgroundImage, MinimapDegreesRotationToNorth, IconTransformX/Y/Z,
//   IconTransformScale, IconTransformRotation, HasProperMinimap). It does not
//   use `this`, so it is called here with a scratch frame. The texture comes
//   back as a resource id (FNV-1 of the lower-case path, sub_822CA580), which
//   loads nothing. The map lies in the canvas x / y plane (GameFace position.x
//   / position.y, y up); the icon vector's lanes are (x, depth, y), so
//   IconTransformZ is the canvas y offset. The image (element Bowerstone,
//   scale 3.5 = 350 units across) is turned by `north` degrees. sub_8240FFB8
//   places an icon (matched against the game with minimap_debug in Bowerlake):
//     v   = (world.xy / extent - 0.5) * (iconScale * 240)   (extent = larger
//           side of the level rectangle at *(level + 4) + 152)
//     rel = RotCCW((north + iconRot) deg) * v + (IconTransformX, IconTransformZ)
//   rel is in canvas units from the image's centre. The hero icon's rotation.z
//   is (north + iconRot) deg - heading, heading = sub_8219EC50(fwd.x, fwd.y) =
//   atan2(x, y), clockwise from world +y (checked by walking: the hero moves
//   along it); a positive rotation.z turns counter-clockwise on screen.
//
// Hero: player = sub_821C7D38() (reads the hub itself), hero = **(player + 16),
// alive while *(u8*)(hero + 144) & 0x40; position = vtable+64 of *(hero + 124)
// (r3 = result vector), facing = sub_821E70C8(r3 = result, r4 = hero).
// Dog: sub_8235F3D8(*(*(*(*(*0x83496920 + 12) + 88) + 4))), same fields.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging/macros.h>

#include "deck_settings_state.h"
#include "fable2_config.h"
#include "minimap_state.h"
#include "photo_mode_state.h"

REXCVAR_DEFINE_BOOL(minimap_debug, false, "Minimap",
                    "Log the minimap's region record, hero position and HUD offset, and check "
                    "the icon transform against the pause menu's own");
REXCVAR_DEFINE_DOUBLE(minimap_image_units, 350.0, "Minimap",
                      "Size of the pause menu's map image in its canvas units (icon positions "
                      "are in those units)");
REXCVAR_DEFINE_BOOL(minimap_flip_y, false, "Minimap",
                    "Draw canvas +y downwards instead of upwards (orientation check)");
REXCVAR_DEFINE_DOUBLE(minimap_rot_sign, 1.0, "Minimap",
                      "Direction of the canvas rotations: 1 or -1 (orientation check)");

namespace fable2::minimap {

namespace detail {

constexpr uint32_t kHub = 0x83496920;
constexpr uint32_t kDrawGuiFlag = 0x8331930E;
constexpr uint32_t kRegionInfo = 0x82410740;      // sub_82410740 (MinimapInfo -> outputs)
constexpr uint32_t kGetPlayer = 0x821C7D38;       // -> player
constexpr uint32_t kGetFacing = 0x821E70C8;       // (r3 = out vector, r4 = entity)
constexpr uint32_t kAtan2 = 0x8219EC50;           // (f1, f2) -> f1
constexpr uint32_t kGetDog = 0x8235F3D8;          // (r3 = family slot) -> dog entity

inline bool ptr_ok(uint32_t a) {
  return a >= 0x30000000u && a < 0xA0000000u && (a & 3) == 0;
}
inline uint32_t ld32(uint8_t* base, uint32_t a) { return REX_LOAD_U32(a); }
inline uint8_t ld8(uint8_t* base, uint32_t a) { return REX_LOAD_U8(a); }
inline float ldf(uint8_t* base, uint32_t a) {
  const uint32_t bits = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}
inline void st32(uint8_t* base, uint32_t a, uint32_t v) { REX_STORE_U32(a, v); }
inline void stf(uint8_t* base, uint32_t a, float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  REX_STORE_U32(a, bits);
}
inline void call_guest(PPCContext& ctx, uint8_t* base, uint32_t addr) {
  REX_CALL_INDIRECT_FUNC(addr);
}

struct State {
  uint32_t level = 0;        // level object of the record we hold
  uint32_t level_inner = 0;  // *(level + 4), changes with the scenario too
  int64_t last_query_ms = 0;
  bool have_region = false;
  uint32_t texture_id = 0;
  float north = 0, icon_rot = 0, icon_x = 0, icon_y = 0, icon_scale = 1, extent = 0;
  bool proper = false;
  int64_t last_debug_ms = 0;
  // Back button (per user): held, and whether another button joined it.
  uint16_t prev_buttons[4] = {0, 0, 0, 0};
  bool back_combo[4] = {false, false, false, false};
};
inline State& S() {
  static State s;
  return s;
}

// The current level object (the one sub_82410740 reads), or 0.
inline uint32_t level_object(uint8_t* base, uint32_t* inner) {
  const uint32_t hub = ld32(base, kHub);
  if (!ptr_ok(hub)) return 0;
  const uint32_t a = ld32(base, hub + 12);
  if (!ptr_ok(a)) return 0;
  const uint32_t b = ld32(base, a + 88);
  if (!ptr_ok(b)) return 0;
  const uint32_t c = ld32(base, b + 4);
  if (!ptr_ok(c)) return 0;
  const uint32_t d = ld32(base, c + 8);
  if (!ptr_ok(d)) return 0;
  const uint32_t e = ld32(base, d + 4);
  if (!ptr_ok(e)) return 0;
  const uint32_t level = ld32(base, e + 36);
  if (!ptr_ok(level)) return 0;
  const uint32_t in = ld32(base, level + 4);
  if (!ptr_ok(in)) return 0;
  if (inner) *inner = in;
  return level;
}

// Reads the region's record into S() (game thread, world on top).
inline void query_region(PPCContext& ctx, uint8_t* base, uint32_t level, uint32_t inner) {
  State& g = S();
  const PPCContext saved = ctx;
  const uint32_t sp = (uint32_t(ctx.r1.u32) - 1024u) & ~15u;
  ctx.r1.u64 = sp;
  const uint32_t out = sp + 256;  // 16-byte aligned
  // +0 texture, +4 north, +8 icon rotation, +16 icon vector, +32 scale, +36 proper
  st32(base, out + 0, 0);
  stf(base, out + 4, 0.0f);
  stf(base, out + 8, 0.0f);
  for (uint32_t i = 0; i < 4; ++i) stf(base, out + 16 + 4 * i, 0.0f);
  stf(base, out + 32, 1.0f);
  st32(base, out + 36, 0);
  ctx.r3.u64 = 0;
  ctx.r4.u64 = out + 0;
  ctx.r5.u64 = out + 4;
  ctx.r6.u64 = out + 8;
  ctx.r7.u64 = out + 16;
  ctx.r8.u64 = out + 32;
  ctx.r9.u64 = out + 36;
  call_guest(ctx, base, kRegionInfo);
  g.texture_id = ld32(base, out + 0);
  g.north = ldf(base, out + 4);
  g.icon_rot = ldf(base, out + 8);
  g.icon_x = ldf(base, out + 16);
  g.icon_y = ldf(base, out + 24);  // lane 2 = IconTransformZ = canvas y
  g.icon_scale = ldf(base, out + 32);
  g.proper = ld8(base, out + 36) != 0;
  // The level rectangle (ints x0, y0, x1, y1).
  const int32_t x0 = int32_t(ld32(base, inner + 152)), y0 = int32_t(ld32(base, inner + 156));
  const int32_t x1 = int32_t(ld32(base, inner + 160)), y1 = int32_t(ld32(base, inner + 164));
  g.extent = float(std::max(x1 - x0, y1 - y0));
  g.level = level;
  g.level_inner = inner;
  g.have_region = true;
  ctx = saved;
  REXSYS_INFO("[minimap] region map id {:08X} (proper {}), north {} deg, icons x {} y {} scale {} "
              "rot {}, level rect {} {} {} {}",
              g.texture_id, g.proper, g.north, g.icon_x, g.icon_y, g.icon_scale, g.icon_rot, x0, y0,
              x1, y1);
}

// An entity's position (x, y) and whether it is alive. Game thread.
inline bool entity_position(PPCContext& ctx, uint8_t* base, uint32_t entity, uint32_t scratch,
                            float& x, float& y) {
  if (!ptr_ok(entity) || !(ld8(base, entity + 144) & 0x40)) return false;
  const uint32_t comp = ld32(base, entity + 124);
  if (!ptr_ok(comp)) return false;
  const uint32_t vt = ld32(base, comp);
  if (!ptr_ok(vt) && !(vt >= 0x82000000u && vt < 0x84000000u)) return false;
  const uint32_t fn = ld32(base, vt + 64);
  if (fn < 0x82000000u || fn >= 0x84000000u) return false;
  ctx.r3.u64 = scratch;
  ctx.r4.u64 = comp;
  call_guest(ctx, base, fn);
  x = ldf(base, scratch);
  y = ldf(base, scratch + 4);
  return std::isfinite(x) && std::isfinite(y);
}

inline uint32_t hero_entity(PPCContext& ctx, uint8_t* base) {
  const uint32_t hub = ld32(base, kHub);
  if (!ptr_ok(hub)) return 0;
  const uint32_t game = ld32(base, hub + 12);
  if (!ptr_ok(game) || !ptr_ok(ld32(base, game + 140)) || !ptr_ok(ld32(base, game + 156))) return 0;
  call_guest(ctx, base, kGetPlayer);
  const uint32_t player = ctx.r3.u32;
  if (!ptr_ok(player)) return 0;
  const uint32_t holder = ld32(base, player + 16);
  if (!ptr_ok(holder)) return 0;
  const uint32_t hero = ld32(base, holder);
  return ptr_ok(hero) ? hero : 0;
}

inline uint32_t dog_entity(PPCContext& ctx, uint8_t* base) {
  uint32_t p = ld32(base, kHub);
  for (uint32_t off : {12u, 88u, 4u}) {
    if (!ptr_ok(p)) return 0;
    p = ld32(base, p + off);
  }
  if (!ptr_ok(p)) return 0;
  const uint32_t slot = ld32(base, p);
  if (!ptr_ok(slot)) return 0;
  ctx.r3.u64 = slot;
  call_guest(ctx, base, kGetDog);
  return ptr_ok(ctx.r3.u32) ? ctx.r3.u32 : 0;
}

// minimap_debug: the pause menu's own icon placement next to ours (canvas
// units from the map image's centre).
inline void check_transform(uint8_t* base, uint32_t out, uint32_t self, uint32_t pos, double arg) {
  static int64_t last = 0;
  const int64_t t = now_ms();
  if (t - last < 500) return;
  last = t;
  const float extent = ldf(base, self + 52), north = ldf(base, self + 84);
  const float irot = ldf(base, self + 216), ix = ldf(base, self + 224), iy = ldf(base, self + 232);
  const float iscale = ldf(base, self + 240), ex = ldf(base, self + 64), ey = ldf(base, self + 72);
  const float px = ldf(base, pos), py = ldf(base, pos + 4);
  const float ox = ldf(base, out), oy = ldf(base, out + 8);
  float mx = 0, my = 0;
  if (extent > 0) {
    const float vx = (px / extent - 0.5f) * iscale * float(arg);
    const float vy = (py / extent - 0.5f) * iscale * float(arg);
    const float a = (north + irot) * 0.017453292f;
    mx = vx * std::cos(a) - vy * std::sin(a) + ix;
    my = vx * std::sin(a) + vy * std::cos(a) + iy;
  }
  REXSYS_INFO("[minimap] pause map icon: world ({:.1f}, {:.1f}) -> game ({:.2f}, {:.2f}), ours ({:.2f}, "
              "{:.2f}); extent {} north {} irot {} icon ({}, {}) scale {} arg {} image ({:.1f}, {:.1f})",
              px, py, ox - ex, oy - ey, mx, my, extent, north, irot, ix, iy, iscale, arg, ex, ey);
}

inline void save_shown(bool on) {
  std::thread([on] { fable2::config::SaveSetting("minimap", "shown", on ? "true" : "false"); })
      .detach();
}

}  // namespace detail

// After every world update (game thread). self = the world state object.
inline void on_world_update(PPCContext& ctx, uint8_t* base, uint32_t self) {
  using namespace detail;
  if (!feature_enabled().load(std::memory_order_relaxed) || !shown().load(std::memory_order_relaxed))
    return;
  State& g = S();
  const PPCContext saved = ctx;
  Snapshot s;
  s.time_ms = now_ms();
  s.world_on_top = fable2::photomode::detail::world_is_top(ctx, base, self);
  s.gui_drawn = ld8(base, kDrawGuiFlag) != 0;
  if (s.world_on_top) {
    uint32_t inner = 0;
    const uint32_t level = level_object(base, &inner);
    if (!level) {
      g.have_region = false;
    } else if (!g.have_region || level != g.level || inner != g.level_inner || g.texture_id == 0) {
      // A level that has just loaded may not have its record yet: retry
      // (once a second at first, then every 5 s while it has no image).
      const int64_t wait = g.have_region && level == g.level ? 5000 : 1000;
      if (s.time_ms - g.last_query_ms > wait || level != g.level) {
        g.last_query_ms = s.time_ms;
        query_region(ctx, base, level, inner);
      }
    }
    const uint32_t sp = (uint32_t(ctx.r1.u32) - 1024u) & ~15u;
    ctx.r1.u64 = sp;
    const uint32_t scratch = sp + 256, fwd = sp + 320;
    const uint32_t hero = hero_entity(ctx, base);
    if (hero && entity_position(ctx, base, hero, scratch, s.hero_x, s.hero_y)) {
      ctx.r3.u64 = fwd;
      ctx.r4.u64 = hero;
      call_guest(ctx, base, kGetFacing);
      ctx.f1.f64 = ldf(base, fwd);
      ctx.f2.f64 = ldf(base, fwd + 4);
      call_guest(ctx, base, kAtan2);
      s.hero_heading = float(ctx.f1.f64);
      s.hero_ok = std::isfinite(s.hero_heading);
    }
    const uint32_t dog = dog_entity(ctx, base);
    if (dog) s.dog_ok = entity_position(ctx, base, dog, scratch, s.dog_x, s.dog_y);
    ctx = saved;
  }
  if (g.have_region) {
    s.level = g.level;
    s.region_ok = g.proper && g.texture_id != 0 && g.extent > 0.0f;
    s.texture_id = g.texture_id;
    s.north_deg = g.north;
    s.icon_rot_deg = g.icon_rot;
    s.icon_x = g.icon_x;
    s.icon_y = g.icon_y;
    s.icon_scale = g.icon_scale;
    s.extent = g.extent;
  }
  if (REXCVAR_GET(minimap_debug) && s.time_ms - g.last_debug_ms > 1000) {
    g.last_debug_ms = s.time_ms;
    REXSYS_INFO("[minimap] top {} gui {} region {} hero {} ({:.1f}, {:.1f}) heading {:.3f} dog {} "
                "({:.1f}, {:.1f}) hud y {:.2f} rest {:.2f} opacity {:.0f} age {} ms",
                s.world_on_top, s.gui_drawn, s.region_ok, s.hero_ok, s.hero_x, s.hero_y,
                s.hero_heading, s.dog_ok, s.dog_x, s.dog_y, hud().y.load(),
                hud().y_rest.load(), hud().opacity.load(), s.time_ms - hud().time_ms.load());
  }
  const bool visible_now = s.world_on_top && s.gui_drawn && s.region_ok && s.hero_ok &&
                           HudLive(s.time_ms) && !fable2::photomode::active() &&
                           !fable2::decksettings::active();
  {
    std::lock_guard<std::mutex> lock(snapshot_mutex());
    snapshot() = std::move(s);
  }
  if (visible_now) {
    if (auto hook = show_hook().load(std::memory_order_acquire)) hook();
  }
}

// After each guest pad read (game thread), once photo mode and the Deck Recomp
// page have blanked it if they own the pad. Back on its own (pressed and let go
// with no other button) shows or hides the minimap, during play only.
inline void on_pad_read(uint8_t* base, uint32_t user, uint32_t state_ptr, uint32_t result) {
  using namespace detail;
  if (user > 3) return;
  State& g = S();
  const uint16_t buttons = (result == 0 && state_ptr) ? REX_LOAD_U16(state_ptr + 4) : 0;
  const uint16_t before = g.prev_buttons[user];
  g.prev_buttons[user] = buttons;
  constexpr uint16_t kBack = 0x0020;
  if ((buttons & kBack) && !(before & kBack)) g.back_combo[user] = (buttons & ~kBack) != 0;
  if ((buttons & kBack) && (buttons & ~kBack)) g.back_combo[user] = true;
  if (!(before & kBack) || (buttons & kBack)) return;  // not a release
  if (g.back_combo[user] || !feature_enabled().load(std::memory_order_relaxed)) return;
  // During play: the HUD is up and nothing owns the pad.
  if (!HudLive(now_ms()) || fable2::photomode::active() || fable2::decksettings::active()) return;
  const bool on = !shown().load(std::memory_order_relaxed);
  shown().store(on, std::memory_order_relaxed);
  save_shown(on);
  REXSYS_INFO("[minimap] {} (Back)", on ? "shown" : "hidden");
}

}  // namespace fable2::minimap

// --- Strong overrides of the recompiled functions ----------------------------------------------
// The pause map's icon transform (checked against ours with minimap_debug).
extern "C" void sub_8240FFB8(PPCContext& ctx, uint8_t* base) {
  const uint32_t out = ctx.r3.u32, self = ctx.r4.u32, pos = ctx.r5.u32;
  const double arg = ctx.f1.f64;
  __imp__sub_8240FFB8(ctx, base);
  if (REXCVAR_GET(minimap_debug)) fable2::minimap::detail::check_transform(base, out, self, pos, arg);
}
