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
    // Release build: don't create the stall-dump log file. sline/dump_threads
    // are all null-guarded, so the detector stays live but writes nothing.
    return nullptr;
    // char dir[MAX_PATH] = {0};
    // if (!GetModuleFileNameA(nullptr, dir, MAX_PATH)) return nullptr;
    // char* cut = nullptr;
    // for (char* p = dir + std::strlen(dir) - 1; p >= dir; --p) {
    //   if (*p == '\\' || *p == '/') {
    //     cut = p;
    //     break;
    //   }
    // }
    // if (cut) *(cut + 1) = 0;
    // char path[MAX_PATH + 32];
    // snprintf(path, sizeof(path), "%sfable2_stall_dump.log", dir);
    // FILE* out = nullptr;
    // if (fopen_s(&out, path, "w") != 0) return nullptr;
    // setvbuf(out, nullptr, _IONBF, 0);  // tiny writes; must survive TerminateProcess
    // return out;
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

inline void dump_threads(uint32_t render_tid) {
  const int64_t unix_ms = (int64_t)GetTickCount64();
  sline("# stall dump unix_ms=%lld render_tid=0x%08X", (long long)unix_ms,
        render_tid);
  // Nothing to write to: don't suspend threads for a dump nobody will read.
  if (!slog()) return;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return;
  const uint32_t pid = GetCurrentProcessId();
  const uint32_t self_tid = GetCurrentThreadId();
  THREADENTRY32 te{};
  te.dwSize = sizeof(te);
  if (Thread32First(snap, &te)) {
    do {
      if (te.th32OwnerProcessID != pid) continue;
      // Suspending this thread would stop the dump with no one to resume it.
      if (te.th32ThreadID == self_tid) continue;
      uintptr_t rip = 0, rsp = 0;
      uint8_t st[64] = {};
      HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME |
                                 THREAD_QUERY_INFORMATION,
                             FALSE, te.th32ThreadID);
      if (th) {
        SuspendThread(th);
        CONTEXT c{};
        c.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(th, &c)) {
          rip = c.Rip;
          rsp = c.Rsp;
          safe_read((const void*)rsp, st, sizeof(st));
        }
        ResumeThread(th);
        CloseHandle(th);
      }
      // One line per thread; stack words as 64-bit hex for offline mapping.
      FILE* f = slog();
      if (!f) break;
      fprintf(f, "tid=0x%08lX%s rsp=0x%012llX", (unsigned long)te.th32ThreadID,
              te.th32ThreadID == render_tid ? " (RENDER)" : "",
              (unsigned long long)rsp);
      fmt_ip(f, "rip", rip);
      fprintf(f, " stk:");
      for (int i = 0; i < (int)sizeof(st); i += 8) {
        uint64_t w;
        std::memcpy(&w, st + i, 8);
        fprintf(f, " %016llX", (unsigned long long)w);
      }
      std::fputc('\n', f);
      std::fflush(f);
    } while (Thread32Next(snap, &te));
  }
  CloseHandle(snap);
}

inline void loop() {
  // The log is turned off in this build, so there is nothing to watch for.
  if (!slog()) return;
  Sleep(3000);
  sline("== stall dumper thread alive");
  int64_t last_dump_us = 0;
  int dumps = 0;
  while (dumps < 40) {
    Sleep(1000);
    const int64_t last =
        fable2::uip::last_hook_us().load(std::memory_order_relaxed);
    if (last == 0) continue;
    const int64_t now = fable2::uip::now_us();
    const int64_t silence_ms = (now - last) / 1000;
    if (silence_ms < 4000) continue;
    if (silence_ms > 300000) continue;  // only interesting early in the run
    if (now - last_dump_us < 5000000) continue;
    last_dump_us = now;
    ++dumps;
    const uint32_t tid =
        fable2::uip::last_hook_tid().load(std::memory_order_relaxed);
    sline("== STALL: %lld ms since last hook call", (long long)silence_ms);
    dump_threads(tid);
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
