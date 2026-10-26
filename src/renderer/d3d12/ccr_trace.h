#pragma once
// Lightweight, unbuffered trace of every CreateCommittedResource call. Writes
// straight to ccr_trace.log (append + flush) so the exact resource params of the
// call that triggers the NVIDIA driver stack-overflow are captured even on a
// hard __fastfail crash (spdlog is buffered and would lose the line).
#include <cstdio>
#include <atomic>
#include <cstdint>

inline void CcrTrace(const char* kind, uint32_t w, uint32_t h, uint32_t mips,
                     uint32_t fmt, uint64_t size_bytes) {
  static std::atomic<uint64_t> s_seq{0};
  FILE* f = std::fopen("ccr_trace.log", "a");
  if (!f) {
    return;
  }
  std::fprintf(f, "CCR #%llu %s w=%u h=%u mips=%u fmt=0x%04X bytes=%llu\n",
               (unsigned long long)(++s_seq), kind, w, h, mips, fmt,
               (unsigned long long)size_bytes);
  std::fflush(f);
  std::fclose(f);
}
