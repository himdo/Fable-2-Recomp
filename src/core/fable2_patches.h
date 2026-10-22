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
// key in fable2_config.toml, read via fable2::config::Get(). Patches()
// builds the table from those switches (none at the moment).

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

// Host base of the guest arena, as recompiled code addresses it (base +
// address, plus 0x1000 at 0xE0000000 and up). Recorded by ApplyAll, which runs
// before the game starts; null before that. For mid-asm hooks, which get
// registers but not the base.
uint8_t* GuestBase();

}  // namespace fable2::patches
