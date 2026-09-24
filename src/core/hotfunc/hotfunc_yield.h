// hotfunc_yield.h - batched NtYieldExecution for hot-function overrides.
//
// Guest work loops (e.g. the ProcessGameFrame_82276C30 loop) call NtYieldExecution once
// per iteration. The SDK implements it as rex::thread::MaybeYield() =
// SwitchToThread() + MemoryBarrier(). SwitchToThread is a real scheduler
// transition: when another guest thread (vblank worker, audio, ...) is ready
// it runs to completion/block, and the whole duration is charged to the
// caller. In the profiler this was the single biggest cost inside the 475
// chain (~13.7% of total CPU).
//
// On a multi-core host the yield is only a politeness hint - the other guest
// threads run on their own cores regardless. So the override batches it:
// every `hotfunc_yield_every`-th call does the real SwitchToThread, the
// others take only a compiler barrier. That is a signal_fence (NOT a full
// atomic_thread_fence): the whole point is to stop the *compiler* hoisting a
// guest load past the yield point, and on x86 the hardware already provides
// strong ordering (TSO) so a seq_cst fence would just emit an ~30-50 cycle
// mfence on 15 of every 16 calls for no correctness benefit. The real-yield
// path still runs MaybeYield's full MemoryBarrier (it's a genuine thread
// switch, so it keeps the strongest ordering).
// Tunable at runtime via [perf] hotfunc_yield_every in fable2_config.toml
// (1 = original every-call yield; 0 = never yield).

#pragma once

#include <atomic>
#include <cstdint>

#include "fable2_config.h"

// Forward declaration resolved against the rexruntime.dll export
// (.?MaybeYield@thread@rex@@YAXXZ = rex::thread::MaybeYield(), void() -
// verified in rexruntime.def).
namespace rex {
namespace thread {
void MaybeYield();
}  // namespace thread
}  // namespace rex

namespace fable2::hotfunc {

inline void BatchedYield() {
  const int32_t every = fable2::config::Get().hotfunc_yield_every;
  if (every <= 0) {
    std::atomic_signal_fence(std::memory_order_seq_cst);  // compiler barrier only, no mfence
    return;
  }
  static thread_local uint32_t n = 0;
  if (++n >= static_cast<uint32_t>(every)) {
    n = 0;
    rex::thread::MaybeYield();  // SwitchToThread() + full MemoryBarrier()
  } else {
    // Compiler-only barrier: no mfence on x86. TSO covers hardware ordering;
    // this just stops the optimizer reordering guest accesses across the point.
    std::atomic_signal_fence(std::memory_order_seq_cst);
  }
}

}  // namespace fable2::hotfunc
