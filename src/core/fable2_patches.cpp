// fable2_patches - see fable2_patches.h for the design notes.
//
// SCOPE: this table is for GUEST-IMAGE (data) patches only. Code-region
// Xenia patches must be implemented as mid-asm hooks instead
// ([[entrypoint.midasm_hook]] in fable_2_manifest.toml + src/core/fable2_hooks.cpp)
// (the 60 FPS patch used to live here; its guest-byte write was inert,
// so it became a mid-asm hook, which has since been removed entirely).
//
// The table is data-driven: Load() reads fable2_patches.toml from next to
// the exe (same format as Xenia's game-patches files), creating it with the
// built-in defaults if missing. The built-in defaults below are the single
// source of truth for both the fallback and the template written on first
// launch; config/fable2_patches.toml in the repo is the checked-in copy
// (staged next to the exe by the build) - keep the three in sync.

#include "fable2_patches.h"

#include <format>
#include <fstream>

#include <rex/logging.h>
#include <rex/system.h>
#include <rex/system/xmemory.h>

#include <toml++/toml.hpp>

namespace fable2::patches {

namespace {

// ---------------------------------------------------------------------------
// Built-in defaults (fallback + first-launch template). Ported from the Xenia
// game-patches file "4D5307F1 - Fable II (GOTY/Platinum Edition).patch.toml"
// (https://github.com/xenia-canary/game-patches); the Xenia file ships
// these with is_enabled = false. We default High Tick Rate to disabled.
// ---------------------------------------------------------------------------
const std::vector<Patch>& DefaultPatches() {
  static const std::vector<Patch> kPatches = {
      {
          "High Tick Rate",
          "Doubles tickrate to 30hz. Vastly improves in-game UI framerate. "
          "Improves input delay. Minor side effects.",
          "Guy",
          false,
          {
              // NOTE: code-region op. In this recomp the guest .text is never
              // executed (native recompiled code runs instead), so this NOP
              // write has no runtime effect. Kept so the patch stays a 1:1
              // copy of the Xenia entry.
              {Op::Width::kBe32, 0x8233AEB4, 0x60000000},
              // Data (BSS) op - takes effect: recompiled code sees the value.
              {Op::Width::kBe8, 0x83319511, 0x3E},
          },
      },
      // NOTE: 60 FPS (Margen67) was listed here as be8 0x82B9C8EB = 0x01
      // but was removed: the guest-byte write is inert in a recomp, and the
      // replacement mid-asm hook (fable2_hook_60fps) has been removed.
  };
  return kPatches;
}

// Must stay in sync with config/fable2_patches.toml in the repo; this copy
// is what a first launch writes out when the file is missing.
const char kTemplate[] =
R"TOML_EOF(# =============================================================================
# fable2_patches.toml - guest-image (data) patch table
# =============================================================================
#
# Loaded by fable_2.exe from the directory the exe lives in, and applied to
# the decrypted XEX image before the guest module launches. Format mirrors
# the Xenia game-patches files (https://github.com/xenia-canary/game-patches).
#
# - If the file is missing, the game recreates it at startup with the built-in
#   defaults (the embedded template in src/core/fable2_patches.cpp must stay in
#   sync with this file).
# - If the file fails to parse, the error is logged to logs/, a dialog is
#   shown, and the built-in defaults are used instead - the game still runs.
# - Each [[patch]] has:
#     name        (string, required)
#     description (string, optional)
#     author      (string, optional)
#     enabled     (bool,   default true)
#     ops         (array of {width, address, value}; required)
#       width   = "be8" | "be16" | "be32" | "be64"
#       address = guest address, e.g. 0x83319511
#       value   = bytes to write, big-endian
#
# SCOPE: DATA PATCHES ONLY. This build executes *native* recompiled code, so
# guest .text bytes are never executed: an op inside the code region only
# rewrites dead bytes (the log flags these as "no runtime effect"). Code
# patches must be implemented as mid-asm hooks instead:
#   [[entrypoint.midasm_hook]] in fable_2_manifest.toml + src/core/fable2_hooks.cpp
# Data ops (BSS/.data/.rodata) DO take effect,
# because the recompiled code reads/writes guest memory.
#
# NOTE: this is the patch table. fable2_config.toml (also next to the exe)
# is the recomp's general user config - a different file.
# =============================================================================

# Doubles the guest tick rate (Xenia patch by Guy). The be32 op is a
# code-region NOP and has no effect in this recomp (kept for 1:1 parity with
# the Xenia entry); the be8 data op takes effect.
# Field defaults (see the header above): name required, description/author
# optional (default ""), enabled default true, ops required.
[[patch]]
name = "High Tick Rate"             # Default: (required, no default)
description = "Doubles tickrate to 30hz. Vastly improves in-game UI framerate. Improves input delay. Minor side effects."  # Default: ""
author = "Guy"                       # Default: ""
# Enable/disable this patch: set true to turn it on (no rebuild needed).
# Default: false
enabled = false
ops = [
    { width = "be32", address = 0x8233AEB4, value = 0x60000000 },  # code-region NOP (inert in this recomp)
    { width = "be8",  address = 0x83319511, value = 0x3E },        # data op (takes effect)
]
)TOML_EOF";

std::vector<Patch> g_patches;  // empty until Load(); Patches() falls back.

Op::Width ParseWidth(std::string_view s) {
  if (s == "be8") return Op::Width::kBe8;
  if (s == "be16") return Op::Width::kBe16;
  if (s == "be32") return Op::Width::kBe32;
  if (s == "be64") return Op::Width::kBe64;
  return Op::Width::kBe8;  // sentinel; caller checks via WidthBytes
}

size_t WidthBytes(Op::Width w) {
  switch (w) {
    case Op::Width::kBe8:
      return 1;
    case Op::Width::kBe16:
      return 2;
    case Op::Width::kBe32:
      return 4;
    case Op::Width::kBe64:
      return 8;
  }
  return 0;
}

const char* WidthName(Op::Width w) {
  switch (w) {
    case Op::Width::kBe8:
      return "be8";
    case Op::Width::kBe16:
      return "be16";
    case Op::Width::kBe32:
      return "be32";
    case Op::Width::kBe64:
      return "be64";
  }
  return "?";
}

// Guest memory is big-endian on the host; format a value as big-endian hex.
std::string ToBeHex(uint64_t value, size_t bytes) {
  std::string s = "0x";
  for (size_t i = bytes; i-- > 0;) {
    s += std::format("{:02x}", uint8_t(value >> (8 * i)));
  }
  return s;
}

// Parse one [[patch]] table. Returns false (and logs) for a structurally
// broken entry; the whole file then falls back to the built-in defaults.
bool ParsePatch(const toml::table& t, Patch& out) {
  if (auto v = t["name"]; v && v.is_string()) {
    out.name = *v.value<std::string>();
  } else {
    REXSYS_ERROR("[patches] [[patch]] missing 'name' (string); file rejected");
    return false;
  }
  if (auto v = t["description"]; v && v.is_string())
    out.desc = *v.value<std::string>();
  if (auto v = t["author"]; v && v.is_string())
    out.author = *v.value<std::string>();
  if (auto v = t["enabled"]; v) {
    // toml++ 3.4: booleans are their own node type (is_boolean(), not
    // is_integer()); accept 0/1 too for convenience.
    if (v.is_boolean())
      out.is_enabled = *v.value<bool>();
    else if (v.is_integer())
      out.is_enabled = *v.value<int64_t>() != 0;
    else {
      REXSYS_ERROR("[patches] patch '{}': 'enabled' must be a boolean; "
                   "file rejected", out.name);
      return false;
    }
  }  // default: enabled

  const auto ops = t["ops"];
  if (!ops || !ops.is_array()) {
    REXSYS_ERROR("[patches] patch '{}': missing 'ops' array; file rejected",
                 out.name);
    return false;
  }
  for (const auto& op_node : *ops.as_array()) {
    if (!op_node.is_table()) {
      REXSYS_ERROR("[patches] patch '{}': op is not a table; file rejected",
                   out.name);
      return false;
    }
    const toml::table& op = *op_node.as_table();
    Op parsed{};
    if (auto v = op["width"]; v && v.is_string()) {
      parsed.width = ParseWidth(*v.value<std::string>());
    } else {
      REXSYS_ERROR("[patches] patch '{}': op missing 'width' (string); "
                   "file rejected", out.name);
      return false;
    }
    if (WidthBytes(parsed.width) == 0) {
      REXSYS_ERROR("[patches] patch '{}': unknown op width; file rejected",
                   out.name);
      return false;
    }
    if (auto v = op["address"]; v && v.is_integer()) {
      parsed.address = static_cast<uint32_t>(*v.value<int64_t>());
    } else {
      REXSYS_ERROR("[patches] patch '{}': op missing 'address' (integer); "
                   "file rejected", out.name);
      return false;
    }
    if (auto v = op["value"]; v && v.is_integer()) {
      parsed.value = static_cast<uint64_t>(*v.value<int64_t>());
    } else {
      REXSYS_ERROR("[patches] patch '{}': op missing 'value' (integer); "
                   "file rejected", out.name);
      return false;
    }
    out.ops.push_back(parsed);
  }
  return true;
}

// The SDK marks XEX code/rodata pages read-only after loading (see
// XexModule::LoadContinue's page-descriptor protection pass), so writing a
// patch to a read-only page faults. This guard flips the guest pages an op
// touches to read/write and restores their original protection afterwards.
// Safe at OnPostLoadXexImage time: the guest module has not launched yet.
class PageWriteGuard {
 public:
  PageWriteGuard(rex::memory::BaseHeap* heap, uint32_t address, size_t bytes)
      : heap_(heap), page_size_(heap->page_size()) {
    uint32_t page = address & ~(page_size_ - 1);
    const uint32_t end = address + bytes;  // exclusive
    while (page < end) {
      const uint32_t next = page + page_size_;
      const uint32_t hi = next < end ? next : end;
      access_ = heap_->QueryRangeAccess(page, hi - 1);
      if (access_ == rex::memory::PageAccess::kReadOnly) {
        // Protect covers every page the range touches (partial ranges are
        // rounded up to whole pages, like the XEX section pass relies on).
        const uint32_t range = hi - page;
        if (heap_->Protect(page, range, rex::memory::kMemoryProtectRead |
                                            rex::memory::kMemoryProtectWrite)) {
          flipped_.push_back({page, range});
        } else {
          REXSYS_ERROR("[patches]   failed to make guest page 0x{:08X} writable; "
                       "op will be skipped", page);
          return;
        }
      } else if (access_ != rex::memory::PageAccess::kReadWrite) {
        REXSYS_ERROR("[patches]   guest page 0x{:08X} is not readable/writable "
                     "(no-access); op will be skipped", page);
        return;
      }
      page = next;
    }
    ok_ = true;
  }

  ~PageWriteGuard() {
    // Restore in reverse so a multi-page op unwinds cleanly.
    for (auto it = flipped_.rbegin(); it != flipped_.rend(); ++it) {
      heap_->Protect(it->page, it->size, rex::memory::kMemoryProtectRead);
    }
  }

  bool ok() const { return ok_; }

 private:
  struct Flip {
    uint32_t page;
    uint32_t size;
  };
  rex::memory::BaseHeap* heap_;
  uint32_t page_size_;
  rex::memory::PageAccess access_;
  std::vector<Flip> flipped_;
  bool ok_ = false;
};

}  // namespace

bool Load(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
      REXSYS_ERROR("[patches] could not create {}", path.string());
      return false;
    }
    out << kTemplate;
    REXSYS_INFO("[patches] {} not found; created with built-in defaults",
                path.string());
  }

  // toml++ (v3, exceptions on) returns the table directly and throws a
  // toml::parse_error on a broken file.
  toml::table root;
  try {
    root = toml::parse_file(path.native());
  } catch (const toml::parse_error& err) {
    REXSYS_ERROR("[patches] {} failed to parse at line {}, column {}: {}",
                 path.string(), err.source().begin.line, err.source().begin.column,
                 err.description());
    const std::string msg = std::format(
        "fable2_patches.toml could not be parsed\n\n"
        "{}\n\n"
        "File: {}\n"
        "The game will continue with the built-in patch defaults. Fix the "
        "file (or delete it to recreate the defaults) and start again.",
        err.description(), path.string());
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    return false;
  }

  std::vector<Patch> loaded;
  bool parse_failed = false;
  const auto patches = root["patch"];
  if (patches && patches.is_array()) {
    for (const auto& node : *patches.as_array()) {
      if (!node.is_table()) {
        REXSYS_ERROR("[patches] [[patch]] entry is not a table; file "
                     "rejected");
        parse_failed = true;
        break;
      }
      Patch p;
      if (!ParsePatch(*node.as_table(), p)) {
        parse_failed = true;
        break;
      }
      loaded.push_back(std::move(p));
    }
  } else {
    // No [[patch]] array at all (e.g. a typo'd section header) - reject.
    parse_failed = true;
  }

  if (parse_failed) {
    REXSYS_WARN("[patches] no valid patches in {}; using built-in defaults",
                path.string());
    return false;
  }
  // An explicitly empty [[patch]] list is a valid user state (all patches
  // turned off) and is kept as such.
  g_patches = std::move(loaded);
  REXSYS_INFO("[patches] loaded {} patch(es) from {}", g_patches.size(),
              path.string());
  return true;
}

const std::vector<Patch>& Patches() {
  return g_patches.empty() ? DefaultPatches() : g_patches;
}

size_t ApplyAll(rex::memory::Memory* memory, const rex::PPCImageInfo& image) {
  if (!memory) {
    REXSYS_ERROR("[patches] no guest memory available; not applying patches");
    return 0;
  }

  size_t applied = 0;
  size_t skipped = 0;
  const uint64_t img_lo = image.image_base;
  const uint64_t img_hi = image.image_base + image.image_size;
  const uint64_t code_lo = image.code_base;
  const uint64_t code_hi = image.code_base + image.code_size;

  for (const Patch& p : Patches()) {
    if (!p.is_enabled) {
      REXSYS_INFO("[patches] '{}' disabled, skipping", p.name);
      continue;
    }
    REXSYS_INFO("[patches] applying '{}' by {} ({} op{}){}", p.name, p.author,
                p.ops.size(), p.ops.size() == 1 ? "" : "s",
                p.desc.empty() ? std::string{} : std::format(" - {}", p.desc));

    for (const Op& op : p.ops) {
      const size_t n = WidthBytes(op.width);
      if (n == 0) {
        REXSYS_ERROR("[patches]   {}: unknown op width; skipped", p.name);
        ++skipped;
        continue;
      }
      const uint64_t addr = op.address;
      if (addr < img_lo || addr + n > img_hi) {
        REXSYS_ERROR("[patches]   {} {} 0x{:08X} is outside the image "
                     "[0x{:08X}, 0x{:08X}); skipped",
                     p.name, WidthName(op.width), addr, img_lo, img_hi);
        ++skipped;
        continue;
      }

      auto* heap = memory->LookupHeap(op.address);
      if (!heap) {
        REXSYS_ERROR("[patches]   {} {} 0x{:08X} has no guest heap; skipped",
                     p.name, WidthName(op.width), addr);
        ++skipped;
        continue;
      }
      PageWriteGuard guard(heap, op.address, n);
      if (!guard.ok()) {
        ++skipped;
        continue;
      }

      uint8_t* host = memory->TranslateVirtual<uint8_t*>(op.address);
      uint64_t old_value = 0;
      for (size_t i = 0; i < n; ++i) {
        old_value = (old_value << 8) | host[i];  // big-endian in guest memory
      }

      for (size_t i = 0; i < n; ++i) {
        host[i] = uint8_t(op.value >> (8 * (n - 1 - i)));
      }

      const bool in_code =
          code_lo != 0 && addr >= code_lo && addr < code_hi;
      REXSYS_INFO("[patches]   {} {} 0x{:08X}: {} -> {} ({})", p.name,
                  WidthName(op.width), addr, ToBeHex(old_value, n),
                  ToBeHex(op.value, n),
                  in_code
                      ? "code region: guest .text is not executed in this "
                        "recomp (native code runs instead) - no runtime "
                        "effect"
                      : "data: takes effect at runtime");
      ++applied;
    }
  }

  REXSYS_INFO("[patches] done: {} op(s) applied, {} skipped", applied,
              skipped);
  return applied;
}

}  // namespace fable2::patches
