#pragma once
// fable2_stall_dump.h — render-thread stall detector.
//
// When the thread that last called fable2::uip::scan() (the render thread)
// goes silent for >4 s, this dumps the RIP + first stack words of EVERY
// thread in the process to <exe dir>\fable2_stall_dump.log. A stall with no
// access violations (confirmed by fable2_av_probe.log) means either an
// infinite loop or a blocked wait; the RIP + on-stack return addresses show
// exactly where each thread is (recompiled guest function vs host code vs
// an ntdll wait routine).
//
// Cheap: one steady_clock read per second; the toolhelp snapshot + per-thread
// Suspend/GetThreadContext only runs on a detected stall, at most once per 5 s,
// capped at 40 dumps.

#ifndef FABLE2_STALL_DUMP_H
#define FABLE2_STALL_DUMP_H

#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "fable2_ui_input_probe.h"  // fable2::uip::{last_hook_us,last_hook_tid,now_us}

namespace fable2_stall_dump {

inline FILE* slog() {
  static FILE* f = []() -> FILE* {
    char dir[MAX_PATH] = {0};
    if (!GetModuleFileNameA(nullptr, dir, MAX_PATH)) return nullptr;
    char* cut = nullptr;
    for (char* p = dir + std::strlen(dir) - 1; p >= dir; --p) {
      if (*p == '\\' || *p == '/') {
        cut = p;
        break;
      }
    }
    if (cut) *(cut + 1) = 0;
    char path[MAX_PATH + 32];
    snprintf(path, sizeof(path), "%sfable2_stall_dump.log", dir);
    FILE* out = nullptr;
    if (fopen_s(&out, path, "w") != 0) return nullptr;
    setvbuf(out, nullptr, _IONBF, 0);  // tiny writes; must survive TerminateProcess
    return out;
  }();
  return f;
}

inline void sline(const char* fmt, ...) {
  FILE* f = slog();
  if (!f) return;
  char buf[600];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  std::fputs(buf, f);
  std::fputc('\n', f);
  std::fflush(f);
}

struct ModuleRange {
  std::string name;
  uintptr_t base;
  uintptr_t end;
};

inline const std::vector<ModuleRange>& modules() {
  static const std::vector<ModuleRange> v = [] {
    std::vector<ModuleRange> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
      do {
        char nameA[MAX_PATH] = {};
        WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, nameA,
                            (int)sizeof(nameA), nullptr, nullptr);
        out.push_back({nameA, (uintptr_t)me.modBaseAddr,
                       (uintptr_t)me.modBaseAddr + me.modBaseSize});
      } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return out;
  }();
  return v;
}

inline void fmt_ip(FILE* f, const char* tag, uintptr_t ip) {
  for (const ModuleRange& m : modules()) {
    if (ip >= m.base && ip < m.end) {
      fprintf(f, " %s=+0x%llX(%s)", tag, (unsigned long long)(ip - m.base),
              m.name.c_str());
      return;
    }
  }
  fprintf(f, " %s=0x%llX(none)", tag, (unsigned long long)ip);
}

inline bool safe_read(const void* p, void* dst, size_t n) {
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(const_cast<void*>(p), &mbi, sizeof(mbi)) == 0) return false;
  if (mbi.State != MEM_COMMIT) return false;
  const uintptr_t start = (uintptr_t)mbi.BaseAddress;
  if ((uintptr_t)p + n > start + (uintptr_t)mbi.RegionSize) return false;
  std::memcpy(dst, p, n);
  return true;
}

// Capture a thread's RIP/RSP + a stack sample. Must stay POD-only (no C++
// objects requiring unwinding) so the SEH __try is legal. Reading the live
// context without suspending: GetThreadContext works on user-mode running
// threads (a slightly stale RIP is fine here), and suspending every thread
// risks leaving one stuck if we fault midway.
inline bool capture_thread(uint32_t tid, uintptr_t* rip, uintptr_t* rsp,
                           void* st, size_t st_n,
                           uintptr_t* rax = nullptr, uintptr_t* rcx = nullptr,
                           uintptr_t* rdx = nullptr, uintptr_t* r8 = nullptr,
                           uintptr_t* r9 = nullptr, uintptr_t* r10 = nullptr,
                           uintptr_t* r11 = nullptr) {
  *rip = 0;
  *rsp = 0;
  __try {
    HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                           FALSE, tid);
    if (!th) return false;
    CONTEXT c{};
    c.ContextFlags = CONTEXT_FULL;
    bool ok = false;
    if (GetThreadContext(th, &c)) {
      *rip = c.Rip;
      *rsp = c.Rsp;
      if (rax) *rax = c.Rax;
      if (rcx) *rcx = c.Rcx;
      if (rdx) *rdx = c.Rdx;
      if (r8) *r8 = c.R8;
      if (r9) *r9 = c.R9;
      if (r10) *r10 = c.R10;
      if (r11) *r11 = c.R11;
      safe_read((const void*)c.Rsp, st, st_n);
      ok = true;
    }
    CloseHandle(th);
    return ok;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

inline void dump_threads(uint32_t render_tid) {
  const int64_t unix_ms = (int64_t)GetTickCount64();
  sline("# stall dump unix_ms=%lld render_tid=0x%08X", (long long)unix_ms,
        render_tid);
  // Log actual module bases so raw stack words resolve offline.
  for (const ModuleRange& m : modules()) {
    sline("# module %s base=0x%llX end=0x%llX", m.name.c_str(),
          (unsigned long long)m.base, (unsigned long long)m.end);
  }
  // Retry the snapshot a few times: early in the run the process is still
  // churning threads and a racy snapshot can enumerate only a few.
  std::vector<THREADENTRY32> entries;
  for (int attempt = 0; attempt < 4 && entries.size() < 4; ++attempt) {
    entries.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    const uint32_t pid = GetCurrentProcessId();
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    bool first = true;
    while (first ? Thread32First(snap, &te) : Thread32Next(snap, &te)) {
      first = false;
      if (te.th32OwnerProcessID != pid) continue;
      entries.push_back(te);
    }
    CloseHandle(snap);
  }
  int shown = 0;
  for (const THREADENTRY32& te : entries) {
    uintptr_t rip = 0, rsp = 0;
    uintptr_t rax = 0, rcx = 0, rdx = 0, r8 = 0, r9 = 0, r10 = 0, r11 = 0;
    const bool is_render = (te.th32ThreadID == render_tid);
    uint8_t st[256] = {};
    // Capture the full general-register set for EVERY thread: while blocked in
    // NtDelayExecution, r8 holds the host sleep duration in 100ns units
    // (negative = relative). The render thread (guest code in its stack) is
    // identified post-hoc among the NtDelayExecution waiters.
    capture_thread(te.th32ThreadID, &rip, &rsp, st, sizeof(st), &rax, &rcx,
                   &rdx, &r8, &r9, &r10, &r11);
    // One line per thread; stack words as 64-bit hex for offline mapping.
    FILE* f = slog();
    if (!f) break;
    fprintf(f, "tid=0x%08lX%s rsp=0x%012llX", (unsigned long)te.th32ThreadID,
            is_render ? " (RENDER)" : "",
            (unsigned long long)rsp);
    fmt_ip(f, "rip", rip);
    fprintf(f,
            " rax=0x%016llX rcx=0x%016llX rdx=0x%016llX r8=0x%016llX "
            "r9=0x%016llX r10=0x%016llX r11=0x%016llX",
            (unsigned long long)rax, (unsigned long long)rcx,
            (unsigned long long)rdx, (unsigned long long)r8,
            (unsigned long long)r9, (unsigned long long)r10,
            (unsigned long long)r11);
    fprintf(f, " stk:");
    for (int i = 0; i < (int)sizeof(st); i += 8) {
      uint64_t w;
      std::memcpy(&w, st + i, 8);
      // Resolve to module+offset when it lands in a known image.
      bool hit = false;
      for (const ModuleRange& m : modules()) {
        if (w >= m.base && w < m.end) {
          fprintf(f, " +%08llX(%s)", (unsigned long long)(w - m.base),
                  m.name.c_str());
          hit = true;
          break;
        }
      }
      if (!hit) fprintf(f, " %016llX", (unsigned long long)w);
    }
    std::fputc('\n', f);
    std::fflush(f);
    ++shown;
  }
  sline("# dump done threads=%d", shown);
}

inline void loop() {
  Sleep(3000);
  sline("== stall dumper thread alive");
  // Use GetTickCount64 (not the UI probe clock) for the wall-clock samples so
  // the sampler fires even when the UI input probe is not enabled.
  const int64_t start_ms = (int64_t)GetTickCount64();
  int64_t last_dump_ms = 0;
  int dumps = 0;
  while (dumps < 40) {
    Sleep(1000);
    const int64_t last =
        fable2::uip::last_hook_us().load(std::memory_order_relaxed);
    const int64_t now_ms = (int64_t)GetTickCount64();
    const int64_t up_ms = now_ms - start_ms;
    bool want = false;
    const char* why = nullptr;
    if (last != 0) {
      const int64_t last_hook_ms = last / 1000;
      const int64_t silence_ms = now_ms - last_hook_ms;
      if (silence_ms >= 4000 && silence_ms <= 300000) {
        want = true;
        why = "hook silence";
      }
    }
    // Always wall-clock-sample during the early window too: the hooked
    // pipeline may keep firing while the render path is still frozen.
    if (!want && up_ms >= 6000 && up_ms <= 150000) {
      want = true;
      why = "wall-clock sample";
    }
    if (!want) continue;
    if (now_ms - last_dump_ms < 5000) continue;
    last_dump_ms = now_ms;
    ++dumps;
    sline("== STALL[%s] up=%lld ms", why, (long long)up_ms);
    dump_threads(fable2::uip::last_hook_tid().load(std::memory_order_relaxed));
  }
}

struct Registrar {
  std::thread t;
  Registrar() {
    t = std::thread(&loop);
    t.detach();
  }
};

static Registrar g_stall_dump_registrar;

}  // namespace fable2_stall_dump

#endif  // FABLE2_STALL_DUMP_H
