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

#include <rex/ppc/context.h>  // PPCRegister (union with u64/s64/u32/... views)
#include <rex/logging/macros.h>

#include "fable2_config.h"

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

// High Tick Rate (Xenia patch by Guy), code half. Runs BEFORE the store at
// 0x8233AEB4 that writes the game's LF tick-rate double (0x83319510);
// returning true jumps past it, which is exactly the Xenia patch's NOP.
// Without this the game overwrote the patched value (15 Hz -> 30 Hz, written
// at load by src/core/fable2_patches.cpp) and the patch had no effect.
// Toggle: [patches] high_tick_rate.
bool fable2_hook_high_tick_rate_skip_store() {
  static const bool enabled = [] {
    const bool on = fable2::config::Get().high_tick_rate;
    if (on) {
      REXSYS_INFO("[tick-rate] LF tick forced to 30 Hz (high_tick_rate)");
    }
    return on;
  }();
  return enabled;
}

// Higher HF Tick Rate (Xenia patch by Ultra), code half. Runs BEFORE the store
// at 0x8233AE98 that writes the HF tick double (0x83319518, normally 30 Hz =
// twice the 15 Hz LF tick). Skipping it keeps the patched 60 Hz, so with
// high_tick_rate on the HF:LF ratio stays 2:1 (60:30) instead of collapsing
// to 1:1 (30:30), which made cloth physics misbehave.
// Requires high_tick_rate: on its own (HF 60 Hz with LF 15 Hz, 4:1) it would
// break the 2:1 ratio the other way, so it is ignored unless both are on.
// Toggle: [patches] higher_hf_tick_rate (needs [patches] high_tick_rate).
bool fable2_hook_high_hf_tick_rate_skip_store() {
  static const bool enabled = [] {
    const auto& cfg = fable2::config::Get();
    if (cfg.higher_hf_tick_rate && !cfg.high_tick_rate) {
      REXSYS_WARN("[tick-rate] higher_hf_tick_rate ignored: it requires high_tick_rate");
      return false;
    }
    if (cfg.higher_hf_tick_rate) {
      REXSYS_INFO("[tick-rate] HF tick forced to 60 Hz (higher_hf_tick_rate)");
    }
    return cfg.higher_hf_tick_rate;
  }();
  return enabled;
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
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXSYS_INFO("[texture-morph] building hero/dog textures in realtime (GPU) mode");
  }
  r9.u64 = 1;
}
