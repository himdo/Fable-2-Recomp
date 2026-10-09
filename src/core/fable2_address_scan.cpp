// fable2_address_scan.cpp - makes the game's aligned address-space scan skip
// ranges that are already taken, instead of trying (and failing) each one.
//
// The problem: sub_82B4DDD8 reserves an aligned block of guest address space
// by brute force. It calls NtAllocateVirtualMemory(MEM_RESERVE) at a fixed
// candidate address, and when that fails it adds the step (a power of two
// >= the size) and tries again, starting low in memory:
//
//   0x82B4DE50  li   r6, 1            <- loop head (hook runs here)
//   0x82B4DE54  mr   r5, r29          type = MEM_RESERVE
//   0x82B4DE58  mr   r4, r26          size
//   0x82B4DE5C  mr   r3, r31          candidate address
//   0x82B4DE60  bl   VirtualAllocate_82CC0CA0
//   ...         on failure: r31 += r28; loop while r31 < r27
//   0x82B4DEF8  li   r3, 0            "nothing found" exit
//
// Every taken candidate is a failed kernel call, and the SDK logs each one as
// "[error] BaseHeap::AllocFixed attempting to reserve an already reserved
// range" - about 3,850 of them at startup.
//
// The fix: before each attempt, look the candidate up in the guest heap's page
// table and move straight to the first aligned candidate whose whole range is
// free. Only candidates that would certainly fail are skipped, so the game
// ends up with exactly the address its own loop would have found - just
// without the failed calls. (Making the failing reserves *succeed* instead is
// not an option: the game relies on them failing, see
// thirdparty/sdk_allocfixed_patch.patch.)
//
// Whenever the hook cannot be sure (no guest heap there, a non-virtual heap,
// a query failure), it stops skipping and lets the game's own call decide.
//
// Needs rex::memory::BaseHeap::QueryRegionInfo exported from rexruntime.dll
// (SDK change: one line in src/system/rexruntime.def).

#include "fable2_address_scan.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>

#include <fmt/format.h>
#include <rex/logging/macros.h>
#include <rex/ppc/context.h>
#include <rex/system/xmemory.h>

namespace fable2::addressscan {
namespace {

std::atomic<rex::memory::Memory*> g_memory{nullptr};
std::atomic<uint32_t> g_logged{0};
constexpr uint32_t kMaxLogged = 8;

uint64_t RoundUp(uint64_t value, uint64_t step) { return (value + step - 1) / step * step; }

// Returns the end of the first taken page run inside [addr, addr + size), or 0
// when the whole range is free. Sets *unsure when it cannot tell.
uint64_t FirstConflictEnd(rex::memory::BaseHeap* heap, uint32_t addr, uint32_t size,
                          bool* unsure) {
  const uint32_t page = heap->page_size();
  const uint64_t end = uint64_t(addr) + size;
  uint64_t pos = addr;
  while (pos < end) {
    rex::memory::HeapAllocationInfo info{};
    if (!heap->QueryRegionInfo(static_cast<uint32_t>(pos), &info)) {
      *unsure = true;
      return 0;
    }
    const uint64_t run = std::max<uint64_t>(info.region_size, page);
    if (info.state != 0) {
      return pos + run;  // reserved/committed run starting at pos
    }
    pos += run;  // free run; continue after it
  }
  return 0;
}

}  // namespace

void SetMemory(rex::memory::Memory* memory) { g_memory.store(memory); }

}  // namespace fable2::addressscan

// Mid-asm hook at 0x82B4DE50 (loop head of sub_82B4DDD8), before the
// candidate is tried. r26 = size, r27 = end (exclusive), r28 = step,
// r31 = candidate. Moves r31 to the first candidate that can succeed.
// Returns true when no candidate below r27 is left: the hook then jumps to
// the function's own "nothing found" exit (0x82B4DEF8), which is where the
// game's loop ends up after failing every remaining candidate.
bool fable2_hook_address_scan_skip(PPCRegister& r26, PPCRegister& r27, PPCRegister& r28,
                                   PPCRegister& r31) {
  using namespace fable2::addressscan;
  rex::memory::Memory* memory = g_memory.load();
  const uint32_t size = r26.u32;
  const uint64_t limit = r27.u32;
  const uint64_t step = r28.u32;
  const uint64_t start = r31.u32;
  if (!memory || step == 0 || size == 0) {
    return false;
  }

  uint64_t addr = start;
  bool exhausted = false;
  for (;;) {
    if (addr >= limit || addr > 0xFFFFFFFFull) {
      exhausted = true;
      break;
    }
    rex::memory::BaseHeap* heap = memory->LookupHeap(static_cast<uint32_t>(addr));
    if (!heap || heap->heap_type() != rex::memory::HeapType::kGuestVirtual) {
      break;  // the kernel rejects these quietly; let the game's call decide
    }
    const uint64_t page = heap->page_size();
    const uint64_t span = RoundUp(size, page);
    const uint64_t heap_end = uint64_t(heap->heap_base()) + heap->heap_size();
    if (addr + span > heap_end) {
      // Does not fit before the end of this heap: every candidate up to the
      // heap end fails, so continue at the first one past it.
      addr = std::max(RoundUp(heap_end, step), addr + step);
      continue;
    }
    bool unsure = false;
    const uint64_t conflict_end =
        FirstConflictEnd(heap, static_cast<uint32_t>(addr), static_cast<uint32_t>(span), &unsure);
    if (unsure || conflict_end == 0) {
      break;  // free (or unknown): the game's own reserve goes ahead here
    }
    addr = std::max(RoundUp(conflict_end, step), addr + step);
  }

  if (addr != start && g_logged.load(std::memory_order_relaxed) < kMaxLogged) {
    const uint32_t n = g_logged.fetch_add(1) + 1;
    REXSYS_INFO("[address-scan] size 0x{:X}, step 0x{:X}: skipped {} taken candidate(s), "
                "0x{:08X} -> {}{}",
                size, step, (std::min<uint64_t>(addr, limit) - start) / step, start,
                exhausted ? std::string("none left") : fmt::format("0x{:08X}", addr),
                n == kMaxLogged ? " (further skips not logged)" : "");
  }

  if (exhausted) {
    return true;
  }
  r31.u64 = static_cast<uint32_t>(addr);
  return false;
}
