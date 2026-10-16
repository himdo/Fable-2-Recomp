// fable2_patches - see fable2_patches.h for the design notes.
//
// SCOPE: this table is for GUEST-IMAGE (data) patches only. Code-region
// Xenia patches must be implemented as mid-asm hooks instead
// ([[entrypoint.midasm_hook]] in fable_2_manifest.toml + src/core/fable2_hooks.cpp).
//
// There is no separate patch file: each patch is switched on by a
// [patches] key in fable2_config.toml (src/core/fable2_config.h), and the
// table below is built from those switches.

#include "fable2_patches.h"

#include <atomic>
#include <format>

#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "fable2_config.h"

namespace fable2::patches {

namespace {

// No data patches at the moment. The Xenia "High Tick Rate" / "Higher HF Tick
// Rate" patches lived here; the dynamic tick rate (src/core/fable2_tick_rate.h)
// replaced them. ApplyAll still runs: it records the guest base for the hooks.
std::vector<Patch> BuildPatches() { return {}; }

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

const std::vector<Patch>& Patches() {
  // Built on first use; the config is loaded (OnPostInitLogging) before the
  // patches are applied (OnPostLoadXexImage).
  static const std::vector<Patch> patches = BuildPatches();
  return patches;
}

namespace {
std::atomic<uint8_t*> g_guest_base{nullptr};
}  // namespace

uint8_t* GuestBase() { return g_guest_base.load(std::memory_order_acquire); }

size_t ApplyAll(rex::memory::Memory* memory, const rex::PPCImageInfo& image) {
  if (!memory) {
    REXSYS_ERROR("[patches] no guest memory available; not applying patches");
    return 0;
  }
  g_guest_base.store(memory->virtual_membase(), std::memory_order_release);

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
