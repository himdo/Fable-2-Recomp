#pragma once
// fable2_crash_log.h - writes a host-side crash report to <exe dir>\fable2_crash.log.
//
// Catches what ends the process without a log line: an unhandled exception
// (access violation etc.), abort() (a failed assert, e.g. IM_ASSERT in an
// overlay), std::terminate, an invalid CRT parameter and a pure virtual call.
// Each report has the exception code and address and the call stack as
// module+offset; turn the offsets into function names with
//   llvm-symbolizer --obj=fable_2.exe 0x<offset>
// (the PDB next to the exe is found automatically). Guest code faults are
// logged separately by fable2_av_probe.h.
//
// Self-installing (static registrar) and installed again from the app once the
// SDK is up, in case the SDK sets its own unhandled-exception filter.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <psapi.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>

#pragma comment(lib, "psapi.lib")

namespace fable2::crashlog {

inline void Write(const char* what, DWORD code, const void* address, const CONTEXT* ctx) {
  static volatile LONG once = 0;
  if (InterlockedExchange(&once, 1) != 0) return;  // first report only
  wchar_t path[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  while (n > 0 && path[n - 1] != L'\\') --n;
  path[n] = 0;
  wcscat_s(path, L"fable2_crash.log");
  FILE* f = nullptr;
  if (_wfopen_s(&f, path, L"a") != 0 || !f) return;
  SYSTEMTIME t;
  GetLocalTime(&t);
  std::fprintf(f, "\n=== %04u-%02u-%02u %02u:%02u:%02u.%03u  %s  code=0x%08lX  address=%p  thread=%lu\n", t.wYear,
               t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, what, code, address,
               GetCurrentThreadId());
  auto describe = [&](const void* pc) {
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(pc), &mod) &&
        mod) {
      char full[MAX_PATH];
      if (GetModuleFileNameA(mod, full, MAX_PATH)) {
        const char* s = full;
        for (const char* p = full; *p; ++p)
          if (*p == '\\' || *p == '/') s = p + 1;
        strncpy_s(name, s, _TRUNCATE);
      }
      std::fprintf(f, "  %s+0x%llx\n", name,
                   static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pc) - reinterpret_cast<uintptr_t>(mod)));
    } else {
      std::fprintf(f, "  %p\n", pc);
    }
  };
  if (ctx) {
    std::fprintf(f, "  rip:");
    describe(reinterpret_cast<const void*>(ctx->Rip));
    // Walk the faulting thread's stack from the exception context.
    CONTEXT c = *ctx;
    for (int i = 0; i < 48 && c.Rip; ++i) {
      DWORD64 image_base = 0;
      PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &image_base, nullptr);
      if (!fn) {
        // Leaf function: the return address is on top of the stack.
        c.Rip = *reinterpret_cast<DWORD64*>(c.Rsp);
        c.Rsp += 8;
      } else {
        void* handler_data = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, c.Rip, fn, &c, &handler_data, &establisher, nullptr);
      }
      if (!c.Rip) break;
      describe(reinterpret_cast<const void*>(c.Rip));
    }
  } else {
    void* frames[48];
    const USHORT count = CaptureStackBackTrace(1, 48, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) describe(frames[i]);
  }
  std::fclose(f);
}

inline LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* ep) {
  Write("unhandled exception", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress,
        ep->ContextRecord);
  return EXCEPTION_CONTINUE_SEARCH;
}
inline void OnAbort(int) { Write("abort (failed assert?)", 0, nullptr, nullptr); }
inline void OnTerminate() {
  Write("std::terminate", 0, nullptr, nullptr);
  std::abort();
}
inline void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
  Write("invalid CRT parameter", 0, nullptr, nullptr);
  std::abort();
}
inline void OnPureCall() {
  Write("pure virtual call", 0, nullptr, nullptr);
  std::abort();
}

inline void Install() {
  SetUnhandledExceptionFilter(OnUnhandled);
  std::signal(SIGABRT, OnAbort);
  std::set_terminate(OnTerminate);
  _set_invalid_parameter_handler(OnInvalidParameter);
  _set_purecall_handler(OnPureCall);
}

struct Registrar {
  Registrar() { Install(); }
};
inline Registrar g_registrar;

}  // namespace fable2::crashlog
#endif  // _WIN32
