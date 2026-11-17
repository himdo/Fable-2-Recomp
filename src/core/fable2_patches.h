// fable2_patches - runtime guest-image patch system (Xenia patch.toml format).
//
// Patches are applied to the decrypted XEX image in the guest arena from
// Fable2App::OnPostLoadXexImage() - after the SDK has loaded default.xex
// into guest memory, before the guest module launches (the SDK documents
// this hook as the place for data patches).
//
// SCOPE: DATA PATCHES ONLY. This build executes *native* recompiled code,
// so guest .text bytes are never executed - a code-region op here only
// rewrites dead bytes (ApplyAll flags these in the log). Code-region Xenia
// patches must be implemented as mid-asm hooks instead: see
// src/core/fable2_hooks.cpp + [[entrypoint.midasm_hook]] in fable_2_manifest.toml.
// Data ops (BSS/.data/.rodata, i.e. addresses outside the code region)
// DO take effect, because the recompiled code reads/writes guest memory.
//
// There is no separate patch file: each patch is switched on by a [patches]
// key in fable2_config.toml (high_tick_rate, higher_hf_tick_rate), read via
// fable2::config::Get(). Patches() builds the table from those switches.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <rex/image_info.h>

namespace rex::memory {
class Memory;
}

// Defined by the codegen output (generated/default/fable_2_init.cpp).
extern const rex::PPCImageInfo PPCImageConfig;

namespace fable2::patches {

// One write to the guest image. Mirrors the Xenia patch file format:
//   [[patch.be8]] / [[patch.be16]] / [[patch.be32]] / [[patch.be64]]
//       address = 0x8233aeb4
//       value   = 0x60000000
struct Op {
  enum class Width { kBe8, kBe16, kBe32, kBe64 };
  Width width;
  uint32_t address;  // guest address
  uint64_t value;
};

struct Patch {
  std::string name;
  std::string desc;
  std::string author;
  bool is_enabled;
  std::vector<Op> ops;
};

// The patch table, with each patch's is_enabled taken from the [patches]
// section of fable2_config.toml. The config must be loaded first.
const std::vector<Patch>& Patches();

// Applies all enabled patches to the loaded guest image.
//   memory - the runtime guest memory manager (ReXApp::runtime()->memory()).
//   image  - image layout for bounds checks and code/data classification.
// Logs every write (old -> new value) plus a summary. Returns the number of
// ops applied (out-of-range ops are skipped with an error log).
size_t ApplyAll(rex::memory::Memory* memory, const rex::PPCImageInfo& image);

// High Tick Rate / Higher HF Tick Rate while the game runs (the in-game
// Advanced Options page): rewrites the tick-rate doubles the two patches
// above change (LF 15 or 30 Hz at 0x83319510, HF 30 or 60 Hz at 0x83319518).
// The skip-store hooks in fable2_hooks.cpp follow the config live, so the
// game does not write them back. Needs ApplyAll to have run (it keeps the
// memory). Higher HF only counts with High Tick Rate, as at startup.
bool SetTickRates(bool high_tick_rate, bool higher_hf_tick_rate);

// View distance while the game runs: rewrites the four per-class draw
// distance multipliers the "View Distance" patch sets (clamped to 1..10).
// Objects pick them up as they load, so the change shows from the next area
// or save load. Needs ApplyAll to have run.
bool SetDrawDistance(float multiplier);

}  // namespace fable2::patches
