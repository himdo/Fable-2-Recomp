// fable2_hooks.cpp - mid-asm hook functions for the [[entrypoint.midasm_hook]]
// entries in fable_2_manifest.toml (see docs/patches.md).
//
// Codegen emits an extern prototype for each hook into the generated code
// (e.g. `extern void fable2_hook_website_g1(PPCRegister& r9);`) and calls it at
// the configured instruction address, passing the named registers BY
// REFERENCE, so a hook can read and/or rewrite them. Define each hook with
// plain C++ linkage (NOT extern "C") and exactly once, matching the emitted
// prototype.
//
// Every patch hook is gated on a toggle in fable2_config.toml ([patches]),
// consulted on each call, so a patch can be A/B'd without a rebuild.

#include <bit>
#include <cmath>
#include <cstring>

#include <rex/ppc/context.h>  // PPCRegister (union with u64/s64/u32/... views)
#include <rex/logging/macros.h>

#include <string>

#include <fmt/format.h>

#include "fable2_patches.h"
#include "fable2_tick_rate.h"
#include "fable2_config.h"
#include "mouse_look.h"  // fable2::mouselook (shared host/guest mouse-delta state)

// ---------------------------------------------------------------------------
// Guild chest unlock (fable2.com website items + Collectors Edition content).
//
// An unregistered player who clicks the Guild chest sees "Go to www.fable2.com
// for information on how to access the gold and items your heroic ancestors
// left behind." The chest contents are decided by two getter functions that
// run at save load:
//   - GuildChest_GetWebsiteItem_8256E368 (website items)
//   - GuildChest_GetCEContent_824B3528    (CE content)
//
// Each getter is gated by two "registered" flag bits on the chest object, and
// only when both pass does it find the item and call a per-object GRANT vtable
// method, returning THAT method's result. For an unregistered player the gates
// fail (and even where they pass, the grant method returns 0), so the getter
// reports "not available" and the chest stays locked. We force BOTH:
//   1. the two gate bit-extract results to 1 (g1 / g1b hooks below), and
//   2. the grant method's return to 1 (grantnew / grantavail hooks below).
//
// The getters run at save load, so this populates the chest inventory on load;
// the chest then opens normally and shows the website + CE items.
//
// Toggle: [patches] unlock_website / unlock_ce in fable2_config.toml
// (both default true).
// ---------------------------------------------------------------------------

// Unlock Website Items. GATE 1 (bit6 of *(r4+0x90)): the `rlwinm r9, r10, 0,
// 0x19, 0x19` at 0x8256E384 extracts bit 6 into r9 (0x40 or 0). Force r9 = 1
// so the gate always passes.
void fable2_hook_website_g1(PPCRegister& r9) {
  if (fable2::config::Get().unlock_website) {
    r9.u64 = 1;
  }
}

// Unlock Website Items. GATE 1b (bit0 of *(r4+0x40)): the `clrlwi r8, r9,
// 0x1f` at 0x8256E3AC extracts bit 0 into r8 (1 or 0). Force r8 = 1 so the
// gate always passes.
void fable2_hook_website_g1b(PPCRegister& r8) {
  if (fable2::config::Get().unlock_website) {
    r8.u64 = 1;
  }
}

// Unlock Website Items. Grant method sub_8256D940 (the website-chest
// vtable[1] "is this item new?" check): its final instruction is `xori r3,
// r9, 1` at 0x8256D9E4. It returns 0 when the item hash is already in the
// grant list; force r3 = 1 so the getter reports the item as granted.
void fable2_hook_website_grantnew(PPCRegister& r3) {
  if (fable2::config::Get().unlock_website) {
    r3.u32 = 1;
  }
}

// Unlock Collectors Edition Content. GATE 1 (bit6 of *(r4+0x90)): the
// `rlwinm r10, r11, 0, 0x19, 0x19` at 0x824B3540 extracts bit 6 into r10
// (0x40 or 0). Force r10 = 1 so the gate always passes.
void fable2_hook_ce_g1(PPCRegister& r10) {
  if (fable2::config::Get().unlock_ce) {
    r10.u64 = 1;
  }
}

// Unlock Collectors Edition Content. GATE 1b (a bit of *(r4+0x28)): the
// `rlwinm r9, r10, 7, 0x1f, 0x1f` at 0x824B3568 extracts the bit into r9
// (1 or 0). Force r9 = 1 so the gate always passes.
void fable2_hook_ce_g1b(PPCRegister& r9) {
  if (fable2::config::Get().unlock_ce) {
    r9.u64 = 1;
  }
}

// Unlock Collectors Edition Content. Grant method sub_824ACAE0 (the CE-chest
// grant vtable slot called from GetCEContent): its entire body is `lbz r3,
// 993(r3)` (returns the byte at item+0x3E1, the "CE content available" flag).
// Force r3 = 1 so the getter reports the CE content as available.
void fable2_hook_ce_grantavail(PPCRegister& r3) {
  if (fable2::config::Get().unlock_ce) {
    r3.u32 = 1;
  }
}

// Disable Motion Blur. Runs right after `lfs f12, 0xd4(r31)` at 0x822A49E8 in
// the camera update, where the camera's current full-screen motion blur amount
// (+0xD4) is loaded to be copied into the renderer's view settings (+0x7C).
// Forcing f12 to 0 means the renderer never applies the blur, while the camera
// object and any script reading Camera.GetBlur still see the game's value.
// The first non-zero request is logged once, so the log shows whether the game
// actually asked for motion blur during play.
void fable2_hook_disable_motion_blur(PPCRegister& f12) {
  static bool logged = false;
  const bool disable = fable2::config::Get().disable_motion_blur;
  if (!logged && f12.f64 != 0.0) {
    logged = true;
    REXSYS_INFO("[motion-blur] game requested full-screen motion blur {:.3f} ({})", f12.f64,
                disable ? "forced to 0" : "left on");
  }
  if (disable) {
    f12.f64 = 0.0;
  }
}

// Skip Intro Videos (just-harry's "Skip intro videos" patch, as a hook).
// sub_822F4958 builds the boot video queue (microsoft_logo.bik,
// lionhead_logo.bik, terminator) and loops queueing slots until
// CompareString_8229AD78 reports the terminator. This runs right after the
// first CompareString (bl at 0x822F49B8): r3 = 0 means "slot 0 is the
// terminator", so the loop is skipped and no intro video is queued. Harry's
// original NOPs the slot-0 construction instead; the hook leaves all three
// strings constructed and released normally. Runs once per boot.
void fable2_hook_skip_intro_videos(PPCRegister& r3) {
  if (fable2::config::Get().skip_intro_videos) {
    r3.u64 = 0;
  }
}

// Tick-rate stores. Game init (sub_8233AE50) writes the HF and LF tick-rate
// doubles (0x83319518 at 0x8233AE98, 0x83319510 at 0x8233AEB4). The dynamic
// tick rate owns both (src/core/fable2_tick_rate.h), so the stores are
// skipped: returning true jumps past each one.
bool fable2_hook_skip_tick_rate_store() {
  static const bool logged = [] {
    REXSYS_INFO("[tick-rate] dynamic: one HF tick per frame at the frame "
                "rate, up to {:.0f} Hz", fable2::tickrate::kMaxHfHz);
    return true;
  }();
  return logged;
}

// Direct mouse-to-camera look (separates the mouse from the keyboard->gamepad
// mapping; see src/input/mouse_look.h). Runs at 0x822A47F8 in
// CECCameraUpdateFrame_822A47C0, right after the virtual call writes the
// camera's "current" transform to the stack (r1+96), and before the view lerp.
// r31 is the active CECCamera.
//
// The goal is to orbit the camera around the hero EXACTLY like the game's own
// right-stick look. A live [ml-dump] capture (hero stationary, stick pushed
// around) shows the game drives three coupled things on an orbit: the rotation-
// state orientation quaternions (cam+160 target / cam+176 current) AND the
// world-space eye position (cam+144), which sweeps a ~3.1-radius circle around
// the hero. Rotating only the quaternions makes the camera turn in place (no
// orbit); rotating only the eye detaches it from the hero. So the hook does
// both, with the same world-space orbit rotation R:
//   * orbit the eye:        eye' = pivot + R * (eye - pivot)
//   * rotate the quaternions: q' = R * q   (target + current)
// where pivot is the hero (derived as eye + forward * camera_distance, low-passed
// to resist jitter) and R = yaw around world-up then pitch around the resulting
// right axis. The hero-follow recomputes cam+144/160/176 from scratch each frame,
// so the hook re-applies the persistent look offset (running yaw/pitch radians)
// every frame and the look sticks where it is aimed without springing back or
// running away. Offsets + sensitivity + orbit radius are in fable2_config.toml
// [input]; mouse_look_camera_diag / mouse_look_camera_dump log the fields so the
// layout can be confirmed with no rebuild.
void fable2_hook_direct_mouse_look(PPCRegister& r31) {
  const auto& cfg = fable2::config::Get();
  rex::memory::Memory* mem =
      fable2::mouselook::memory_ref().load(std::memory_order_acquire);
  if (!mem) return;
  const uint32_t cam = r31.u32;
  if (cam == 0) return;

  // Diagnostic: dump EVERY 4-byte field of the active camera object (offset
  // 0..260) once per frame (capped), to the [ml-dump] log, so we can see
  // empirically which fields the game's own right-stick look changes. Only the
  // active hero camera (the one with a non-zero look-at center at +144) is
  // dumped.
  if (cfg.mouse_look_camera_dump) {
    const float cx = fable2::mouselook::read_guest_float(mem, cam + 144);
    if (cx != 0.0f) {
      static int dumped = 0;
      if (dumped < 2400) {
        std::string s = fmt::format("[ml-dump] cam=0x{:08X} ", cam);
        for (uint32_t off = 0; off <= 260; off += 4) {
          const float v = fable2::mouselook::read_guest_float(mem, cam + off);
          s += fmt::format("a{}={:.3f} ", off, v);
        }
        REXSYS_INFO("{}", s.c_str());
        ++dumped;
      }
    }
    // Also dump the camera CONTROLLER (owner object at cam+4) — the native
    // right-stick orbit's accumulated state (angle + clamp) lives here, not on
    // the CECCamera. Only the active hero camera (non-zero eye at +144) is
    // dumped. This lets a full-range stick test reveal the orbit-pitch field
    // and its min/max.
    if (cx != 0.0f) {
      uint32_t owner = 0;
      if (fable2::mouselook::try_read_guest_u32(mem, cam + 4, &owner) &&
          fable2::mouselook::is_plausible_guest_ptr(owner)) {
        static int ctldumped = 0;
        if (ctldumped < 2400) {
          std::string s = fmt::format("[ml-ctl] ctl=0x{:08X} ", owner);
          for (uint32_t off = 0; off <= 128; off += 4) {
            const float v = fable2::mouselook::read_guest_float(mem, owner + off);
            s += fmt::format("c{}={:.3f} ", off, v);
          }
          // Also dump the look sub-object the virtual call reads (owner+124).
          uint32_t sub = 0;
          if (fable2::mouselook::try_read_guest_u32(mem, owner + 124, &sub) &&
              fable2::mouselook::is_plausible_guest_ptr(sub)) {
            s += fmt::format(" | sub=0x{:08X} ", sub);
            for (uint32_t off = 0; off <= 64; off += 4) {
              const float v = fable2::mouselook::read_guest_float(mem, sub + off);
              s += fmt::format("s{}={:.3f} ", off, v);
            }
          }
          REXSYS_INFO("{}", s.c_str());
          ++ctldumped;
        }
      }
    }
    // Also dump the GLOBAL camera-state object that sub_822CBDC8 integrates the
    // eye offset into (reached via the pointer chain
    // 0x83496940 -> +12 -> +88 -> +4). The look orbit (eye position around the
    // hero) is integrated here, so it may live in this object rather than the
    // CECCamera. Dump it under a distinct [ml-dump] tag.
    uint32_t p0 = 0, p1 = 0, p2 = 0, gs = 0;
    bool ok0 = fable2::mouselook::try_read_guest_u32(mem, 0x83496940u, &p0);
    bool ok1 = ok0 && fable2::mouselook::try_read_guest_u32(mem, p0 + 12, &p1);
    bool ok2 = ok1 && fable2::mouselook::try_read_guest_u32(mem, p1 + 88, &p2);
    bool ok3 = ok2 && fable2::mouselook::try_read_guest_u32(mem, p2 + 4, &gs);
    // Log the pointer chain a few times so we can see where it breaks.
    static int ptrdumped = 0;
    if (ptrdumped < 3) {
      ++ptrdumped;
      REXSYS_INFO("[ml-ptr] base=0x83496940 p0=0x{:08X} p1=0x{:08X} p2=0x{:08X} gs=0x{:08X} ok={}{}{}{}",
                  p0, p1, p2, gs, ok0 ? '0' : 'X', ok1 ? '1' : 'X',
                  ok2 ? '2' : 'X', ok3 ? '3' : 'X');
    }
    if (ok3 && fable2::mouselook::is_plausible_guest_ptr(gs)) {
      static int gsdumped = 0;
      if (gsdumped < 2400) {
        std::string s = fmt::format("[ml-gs] gs=0x{:08X} ", gs);
        for (uint32_t off = 0; off <= 260; off += 4) {
          const float v = fable2::mouselook::read_guest_float(mem, gs + off);
          s += fmt::format("g{}={:.3f} ", off, v);
        }
        REXSYS_INFO("{}", s.c_str());
        ++gsdumped;
      }
    }
  }

  // Diagnostic: log the camera's orbit angle scalars + look-at every frame
  // (capped), independent of direct_mouse_look, so we can watch which fields
  // change when the game's own right-stick look runs. The angle scalars are the
  // camera's accumulated orbit angles: current at +44/48/52, target at +68/72/76.
  if (cfg.mouse_look_camera_diag) {
    static int logged = 0;
    if (logged < 400) {
      auto f = [&](uint32_t addr) {
        return fable2::mouselook::read_guest_float(mem, addr);
      };
      REXSYS_INFO(
          "[ml-d] cam=0x{:08X} ang44={:.3f} ang48={:.3f} ang52={:.3f}  "
          "tgt68={:.3f} tgt72={:.3f} tgt76={:.3f}  center144={:.2f},{:.2f},{:.2f}",
          cam,
          f(cam + 44), f(cam + 48), f(cam + 52),
          f(cam + 68), f(cam + 72), f(cam + 76),
          f(cam + 144), f(cam + 148), f(cam + 152));
      ++logged;
    }
  }

  if (!cfg.direct_mouse_look) return;

  // Accumulate the persistent look offset (radians). It persists across frames
  // so the look sticks instead of springing back when the cursor stops.
  int32_t dx = 0;
  int32_t dy = 0;
  fable2::mouselook::take_delta(dx, dy);
  // Map the shared sensitivity slider (mouse_look_scale, default 256) onto the
  // direct-look sensitivity too, so one control scales both the legacy right-
  // stick mapping and this direct camera orbit. At 256 the factor is 1.0, so
  // the configured mouse_look_*_sens values are used verbatim (no behavior
  // change at the default); a larger slider value = faster look. Each sens
  // keeps its own sign (axis direction), only the magnitude is scaled.
  const float sens_scale = float(cfg.mouse_look_scale) / 256.0f;
  fable2::mouselook::cum_yaw() += float(dx) * (cfg.mouse_look_yaw_sens * sens_scale);
  fable2::mouselook::cum_pitch() += float(dy) * (cfg.mouse_look_pitch_sens * sens_scale);
  // Native-aware pitch clamp. The game's own right-stick look integrates the
  // orbit into the target angle at cam+68 and clamps it to [floor, ceil]. We
  // read that native angle and clamp the mouse's cumulative pitch so the TOTAL
  // (native orbit + mouse) stays within [floor, ceil]. This makes the mouse and
  // the stick share one orbit and one set of bounds: the mouse's headroom shrinks
  // as the stick approaches a limit, so the two can't compound past the native
  // limits. Yaw is left unbounded (a full 360 orbit).
  const float native_orbit = fable2::mouselook::read_guest_float(mem, cam + 68);
  float& cum_pitch = fable2::mouselook::cum_pitch();
  const float pmin = cfg.mouse_look_pitch_floor - native_orbit;
  const float pmax = cfg.mouse_look_pitch_ceil - native_orbit;
  if (cum_pitch > pmax) cum_pitch = pmax;
  if (cum_pitch < pmin) cum_pitch = pmin;
  const float yaw = fable2::mouselook::cum_yaw();
  const float pitch = fable2::mouselook::cum_pitch();
  if (yaw == 0.0f && pitch == 0.0f) return;

  // The active hero camera has a real world-space eye position at
  // camera+camera_eye_off; the secondary camera's is all-zero. Skip the zero one
  // so we never write a bogus eye/rotation into it.
  const fable2::mouselook::Vec3 eye = fable2::mouselook::read_guest_vec3(
      mem, cam + uint32_t(cfg.camera_eye_off));
  if (fable2::mouselook::vec3_len(eye) < 1.0f) return;

  using Quat = fable2::mouselook::Quat;
  using Vec3 = fable2::mouselook::Vec3;
  using fable2::mouselook::axis_quat_vec;
  using fable2::mouselook::axis_vec;
  using fable2::mouselook::quat_from_basis;
  using fable2::mouselook::quat_mul;
  using fable2::mouselook::quat_rotate_vec;
  using fable2::mouselook::vec3_cross;
  using fable2::mouselook::vec3_normalize;

  // Hero pivot: the eye looks along the camera's local +Y (forward) toward the
  // hero, so the hero sits `camera_distance` in front of the eye. Low-pass the
  // pivot so per-frame jitter in the forward/eye estimate cannot shake the orbit.
  const Quat q_tgt = fable2::mouselook::read_guest_quat(
      mem, cam + uint32_t(cfg.camera_rot_tgt_off));
  const Vec3 fwd = quat_rotate_vec(q_tgt, Vec3{0.0f, 1.0f, 0.0f});
  const float dist = cfg.camera_distance;
  const Vec3 pivot_new{eye.x + fwd.x * dist, eye.y + fwd.y * dist,
                       eye.z + fwd.z * dist};
  if (!fable2::mouselook::orbit_pivot_init()) {
    fable2::mouselook::orbit_pivot() = pivot_new;
    fable2::mouselook::orbit_pivot_init() = true;
  } else {
    constexpr float kAlpha = 0.15f;  // low-pass toward the fresh estimate
    Vec3& p = fable2::mouselook::orbit_pivot();
    p.x += kAlpha * (pivot_new.x - p.x);
    p.y += kAlpha * (pivot_new.y - p.y);
    p.z += kAlpha * (pivot_new.z - p.z);
  }
  const Vec3 pivot = fable2::mouselook::orbit_pivot();

  // World-space orbit rotation: yaw around the world up axis, then pitch around
  // the resulting right axis (so pitch stays forward/back after a large yaw).
  const Quat r_yaw = axis_quat_vec(axis_vec(cfg.mouse_look_yaw_axis), yaw);
  const Vec3 orbit_right = quat_rotate_vec(r_yaw, axis_vec(cfg.mouse_look_pitch_axis));
  const Quat r_pitch = axis_quat_vec(orbit_right, pitch);
  const Quat R = quat_mul(r_pitch, r_yaw);

  // Orbit the eye around the hero: eye' = pivot + R * (eye - pivot).
  const Vec3 o{eye.x - pivot.x, eye.y - pivot.y, eye.z - pivot.z};
  const Vec3 o_rot = quat_rotate_vec(R, o);
  const Vec3 eye_new{pivot.x + o_rot.x, pivot.y + o_rot.y, pivot.z + o_rot.z};
  fable2::mouselook::write_guest_vec3(mem, cam + uint32_t(cfg.camera_eye_off), eye_new);

  // Orientation: a clean look-at from the new eye toward the hero, with world-up
  // as up. Building it from scratch (instead of rotating the follow logic's own
  // orientation, which carries a slight bank/tilt) removes the in-plane twist
  // that shows up on up/down. right/forward/up form the camera's local X/Y/Z.
  Vec3 forward = vec3_normalize(
      Vec3{pivot.x - eye_new.x, pivot.y - eye_new.y, pivot.z - eye_new.z});
  Vec3 right = vec3_cross(forward, Vec3{0.0f, 0.0f, 1.0f});
  if (fable2::mouselook::vec3_len(right) < 1e-4f)
    right = vec3_cross(forward, Vec3{1.0f, 0.0f, 0.0f});  // forward ~ vertical
  right = vec3_normalize(right);
  const Vec3 up = vec3_cross(right, forward);
  const Quat q_new = quat_from_basis(right, forward, up);
  fable2::mouselook::write_guest_quat(
      mem, cam + uint32_t(cfg.camera_rot_tgt_off), q_new);
  fable2::mouselook::write_guest_quat(
      mem, cam + uint32_t(cfg.camera_rot_cur_off), q_new);
}

// Realtime Texture Morphing (hero/dog black textures without CPU readback;
// plans/hero-dog-realtime-texture-morphing.md). sub_82A76018 turns each
// texture morph request into a morph job and copies the request's
// RealTimeTextureMorphing byte with `lbz r9, 0x20(r30)` at 0x82A7607C. With
// it set, the morph renderer (sub_82A69728) draws straight into the final
// uncompressed texture and builds its mips on the GPU, instead of resolving to
// a scratch texture that the CPU reads back and DXT-compresses (that CPU read
// is what returns black on a split-memory host).
void fable2_hook_realtime_texture_morphing(PPCRegister& r9) {
  if (!fable2::config::Get().realtime_texture_morphing) {
    return;
  }
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXSYS_INFO("[texture-morph] building hero/dog textures in realtime (GPU) mode");
  }
  r9.u64 = 1;
}

// ---------------------------------------------------------------------------
// Interpolation ([patches] interpolation). Two places where the game shows
// LF-tick state without blending it to the render time.
// ---------------------------------------------------------------------------
namespace {

bool InterpolationEnabled() {
  static const bool enabled = [] {
    const bool on = fable2::config::Get().interpolation;
    if (on) {
      REXSYS_INFO("[interpolation] on: GUI updates every HF tick, cloth "
                  "colliders blended between poses");
    }
    return on;
  }();
  return enabled;
}

uint8_t* GuestMem(uint32_t addr) {
  return fable2::patches::GuestBase() + addr + (addr >= 0xE0000000u ? 0x1000u : 0u);
}

uint32_t LoadU32(uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, GuestMem(addr), 4);
  return __builtin_bswap32(v);
}

float LoadF32(uint32_t addr) { return std::bit_cast<float>(LoadU32(addr)); }

}  // namespace

// GUI every HF tick. sub_82278C90 (run on every HF tick from ProcessGameFrame)
// calls the front-end object's vtable[2] (sub_82286B40: GUI manager update,
// subtitles, HUD) only when an LF boundary was crossed (r29) or a query says
// so; the check starts with `clrlwi r10,r29,24` at 0x82278D1C. That update
// measures its own elapsed time from the wall clock (sub_822B6D70), so running
// it every HF tick does not speed anything up; it only updates text twice as
// often (every frame with the dynamic tick rate). Returning true jumps to the
// call at 0x82278D40.
bool fable2_hook_gui_every_tick() { return InterpolationEnabled(); }

// Cloth colliders blended to the render time. sub_82A895C0 builds the cloth's
// collision shapes from the driving character's skeleton. Its four loops read
// a bone straight from the CURRENT pose (*(inst+512)+16, 48-byte bones: quat,
// translation, scale) right after `add r11,r11,r9`, while the skin it collides
// with is drawn blended between the previous pose (*(inst+516)) and the
// current one with the alpha at *(inst+176)+16 (sub_8222E300, using
// sub_8222E5D0). So the colliders run up to one LF tick ahead of the drawn
// body and jump on every LF tick. Here r11 is pointed at the same bone blended
// the way sub_8222E5D0 + sub_8222E300 blend it (quaternion sign-corrected
// lerp, then normalized; translation and scale lerped), written below the
// stack pointer: the four loads from r11 that follow run before the next call,
// so nothing overwrites it in between.
void fable2_hook_cloth_collider_bone(PPCRegister& r1, PPCRegister& r11,
                                     PPCRegister& r30) {
  if (!InterpolationEnabled()) return;
  if (fable2::patches::GuestBase() == nullptr) return;
  const uint32_t inst = r30.u32;
  const uint32_t cur_pose = LoadU32(inst + 512);
  const uint32_t prev_pose = LoadU32(inst + 516);
  const uint32_t interp = LoadU32(inst + 176);
  if (cur_pose == 0 || prev_pose == 0 || interp == 0 || prev_pose == cur_pose) {
    return;
  }
  const uint32_t cur_bones = LoadU32(cur_pose + 16);
  const uint32_t prev_bones = LoadU32(prev_pose + 16);
  if (cur_bones == 0 || prev_bones == 0) return;
  const float alpha = LoadF32(interp + 16);
  if (!(alpha >= 0.0f && alpha < 1.0f)) return;  // 1 = current pose (or NaN)

  // A bone is 12 contiguous big-endian floats; guest memory is mapped
  // linearly, so each bone is read and written through one host pointer.
  const uint32_t cur = r11.u32;
  const uint8_t* prev_p = GuestMem(prev_bones + (cur - cur_bones));
  const uint8_t* cur_p = GuestMem(cur);
  uint32_t raw_a[12], raw_b[12];
  std::memcpy(raw_a, prev_p, sizeof(raw_a));
  std::memcpy(raw_b, cur_p, sizeof(raw_b));
  float a[12], b[12];
  for (int i = 0; i < 12; ++i) {
    a[i] = std::bit_cast<float>(__builtin_bswap32(raw_a[i]));
    b[i] = std::bit_cast<float>(__builtin_bswap32(raw_b[i]));
  }
  const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  const float sign = dot < 0.0f ? -1.0f : 1.0f;
  float out[12];
  for (int i = 0; i < 4; ++i) out[i] = a[i] + (sign * b[i] - a[i]) * alpha;
  for (int i = 4; i < 12; ++i) out[i] = a[i] + (b[i] - a[i]) * alpha;
  const float len_sq =
      out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3];
  if (len_sq > 1e-12f) {
    const float inv_len = 1.0f / std::sqrt(len_sq);
    for (int i = 0; i < 4; ++i) out[i] *= inv_len;
  }
  const uint32_t scratch = (r1.u32 - 256) & ~15u;
  uint32_t raw_out[12];
  for (int i = 0; i < 12; ++i) {
    raw_out[i] = __builtin_bswap32(std::bit_cast<uint32_t>(out[i]));
  }
  std::memcpy(GuestMem(scratch), raw_out, sizeof(raw_out));
  r11.u64 = scratch;
}

// Steady render delay under the dynamic tick rate. The render thread draws the
// scene about one LF period in the past: sub_8236C520 loads the LF rate
// (`lfd f0,-27376(r10)` at 0x8236C5A4) and subtracts 1/LF from now. With
// the dynamic tick rate every tick lasts as long as its frame, so the LF global
// changes every frame, and that delay would jump with it, shaking everything
// drawn. Here the render side gets the smoothed rate (the measured frame rate)
// instead, so the delay only drifts slowly.
void fable2_hook_render_time_lf(PPCRegister& f0) {
  const fable2::tickrate::Plan plan = fable2::tickrate::CurrentPlan();
  if (plan.frames_per_tick == 0) return;
  f0.f64 = plan.hf_hz / 2.0;
}
