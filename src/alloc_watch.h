// alloc_watch - lightweight cross-module state for the table-zeroing monitor.
//
// The size-class allocator table (guest 0x40001880, ptr at 0x83496BD4) is
// observed all-zero at the intermittent menu-load crash, while in clean runs
// it is populated (head+12 = 0x400018B0). Two hypotheses:
//   (A) populated at init, then zeroed later  (zeroing bug)
//   (B) consumer runs before init builds it   (init-ordering race)
// A background app thread (see fable_2_app.h) samples the table head
// continuously from t=0 and logs the first valid observation + the first
// null, which distinguishes (A) from (B) WITHOUT perturbing the recompiled
// hot path (the monitor is a separate OS thread; addresses are stable).
#pragma once

#include <atomic>
#include <cstdint>

namespace fable2::allocwatch {

// Guest arena host base. Fixed virtual mapping (not ASLR): 0x100000000.
// Confirmed stable across runs (init probe + VEH fault host 0x10000001C).
inline constexpr uintptr_t kGuestBase = 0x100000000ull;
// Table pointer global (guest) and the table it pointed to in prior crashes.
inline constexpr uint32_t kTablePtrGuest = 0x83496BD4;
inline constexpr uint32_t kFlagGuestAddr = 0x83496BCB;
inline constexpr uint32_t kTableGuestDefault = 0x40001880;

// If a one-shot recompiled probe is present it records the live base/table
// here; the monitor prefers these over the hardcoded defaults. Zero = unset.
inline std::atomic<uintptr_t> g_base{0};
inline std::atomic<uint32_t> g_table{0};

// Convert a guest address (< 0x100000000 arena) to a host pointer.
inline const volatile uint32_t* GuestPtr(uintptr_t base, uint32_t ga) {
  const uintptr_t off = (ga >= 0xE0000000u) ? 0x100000000ull : 0ull;
  return reinterpret_cast<const volatile uint32_t*>(base + ga + off);
}
inline const volatile uint8_t* GuestByte(uintptr_t base, uint32_t ga) {
  const uintptr_t off = (ga >= 0xE0000000u) ? 0x100000000ull : 0ull;
  return reinterpret_cast<const volatile uint8_t*>(base + ga + off);
}

}  // namespace fable2::allocwatch
