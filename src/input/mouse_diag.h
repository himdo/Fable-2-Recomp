// fable_2 - mouse-look diagnostics: fable2_mouse.log, live stats, and the
// synthetic mouse movement behind `f2 mouse inject`.
//
// Why: mouse look feels slow and stuttery. The camera script turns the camera
// once per camera update, from the last stick value it was given, while the
// input driver turns each *poll* into a stick value. These records show how
// polls, mouse movement and camera updates line up, so a fix can be checked
// with numbers rather than by feel. tools/mouse_stats.py summarises the file.
//
// Off by default: [input] mouse_look_diag in fable2_config.toml, the
// mouse_look_diag cvar, or `f2 mouse diag on`. When on, the file
// fable2_mouse.log is created next to the exe (truncated once per run) with:
//   P  one line per input poll of the keyboard/mouse pad (input driver)
//   U  one line per camera update, from the Lua camera trace
//      (scripts/recomp/mouse_diag.lua, started with `f2 mouse trace on`)
//   S  a summary once per second
// Every line starts with "<kind> t=<us>", microseconds on the steady clock.
//
// Logging must not cause the stutter it measures, so threads that record only
// append to an in-memory buffer under a mutex. A background thread writes the
// buffer every 250 ms. Nothing here does file I/O on a guest thread.
//
// Synthetic movement: `f2 mouse inject <dx> <dy> [ms]` spreads dx, dy pixels
// evenly over ms milliseconds from a 1 kHz host thread, like a hand moving at
// a constant speed. The input driver adds it to the real movement on each poll.
// It ignores window focus, so tests can run from console\in.txt.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <rex/cvar.h>

#ifdef _WIN32
#include <share.h>
#endif

// Defined in main.cpp, seeded from fable2_config.toml [input] mouse_look_diag.
REXCVAR_DECLARE(bool, mouse_look_diag);

namespace fable2::mousediag {

using Clock = std::chrono::steady_clock;

inline Clock::time_point Epoch() {
  static const Clock::time_point t = Clock::now();
  return t;
}
inline int64_t NowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - Epoch()).count();
}

inline bool Enabled() { return REXCVAR_GET(mouse_look_diag); }

// What one poll of the keyboard/mouse pad saw and reported.
struct PollRecord {
  int32_t dx = 0, dy = 0;          // cursor displacement this poll (screen px, y down)
  int32_t inj_dx = 0, inj_dy = 0;  // synthetic movement added this poll
  int32_t rx = 0, ry = 0;          // right stick reported to the game
  bool clamped = false;            // an axis hit full deflection: movement lost
  bool looking = false;            // focus + mouse_look + debug menu closed
};

// Running sums for one reporting window (one second for S lines, or the time
// since the last `f2 mouse` for the status command).
struct Window {
  int64_t start_us = 0;
  uint64_t polls = 0;
  uint64_t updates = 0;  // camera updates seen through the Lua trace
  uint64_t moving_polls = 0;
  uint64_t clamped = 0;
  int64_t px_x = 0, px_y = 0;  // |dx| + |inj_dx| summed, and the same for y
  int64_t gap_max_us = 0;
  double gap_sum = 0, gap_sumsq = 0;
  uint64_t gaps = 0;
  uint64_t threads[8] = {};
  int thread_count = 0;
  uint64_t dropped = 0;  // lines not buffered because the buffer was full

  void AddThread(uint64_t id) {
    for (int i = 0; i < thread_count; ++i)
      if (threads[i] == id) return;
    if (thread_count < 8) threads[thread_count++] = id;
  }
};

inline std::string FormatWindow(const char* kind, int64_t now_us, const Window& w) {
  const double secs = std::max(1e-6, (now_us - w.start_us) / 1e6);
  const double mean = w.gaps ? w.gap_sum / w.gaps : 0.0;
  const double var = w.gaps ? std::max(0.0, w.gap_sumsq / w.gaps - mean * mean) : 0.0;
  char buf[384];
  std::snprintf(buf, sizeof buf,
                "%s t=%lld secs=%.2f polls=%llu polls_s=%.1f upd=%llu upd_s=%.1f moving=%llu "
                "clamp=%llu px=%lld,%lld threads=%d gapmean=%.0f gapsd=%.0f gapmax=%lld dropped=%llu",
                kind, static_cast<long long>(now_us), secs,
                static_cast<unsigned long long>(w.polls), w.polls / secs,
                static_cast<unsigned long long>(w.updates), w.updates / secs,
                static_cast<unsigned long long>(w.moving_polls),
                static_cast<unsigned long long>(w.clamped), static_cast<long long>(w.px_x),
                static_cast<long long>(w.px_y), w.thread_count, mean, std::sqrt(var),
                static_cast<long long>(w.gap_max_us), static_cast<unsigned long long>(w.dropped));
  return buf;
}

class Diag {
 public:
  static Diag& Get() {
    static Diag d;
    return d;
  }

  // Input driver: one call per poll of the keyboard/mouse pad.
  void OnPoll(const PollRecord& r) {
    const int64_t t = NowUs();
    const uint64_t tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    const bool log = Enabled();
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t gap = last_poll_us_ ? t - last_poll_us_ : 0;
    last_poll_us_ = t;
    last_ = r;
    for (Window* w : {&second_, &status_}) {
      ++w->polls;
      if (gap > 0) {
        ++w->gaps;
        w->gap_sum += static_cast<double>(gap);
        w->gap_sumsq += static_cast<double>(gap) * static_cast<double>(gap);
        w->gap_max_us = std::max(w->gap_max_us, gap);
      }
      const int32_t mx = std::abs(r.dx) + std::abs(r.inj_dx);
      const int32_t my = std::abs(r.dy) + std::abs(r.inj_dy);
      if (mx || my) ++w->moving_polls;
      w->px_x += mx;
      w->px_y += my;
      if (r.clamped) ++w->clamped;
      w->AddThread(tid);
    }
    if (!log) return;
    EnsureWriterLocked();
    char buf[200];
    std::snprintf(buf, sizeof buf,
                  "P t=%lld tid=%llx gap=%lld dx=%d dy=%d inj=%d,%d rx=%d ry=%d clamp=%d look=%d\n",
                  static_cast<long long>(t), static_cast<unsigned long long>(tid & 0xffffff),
                  static_cast<long long>(gap), r.dx, r.dy, r.inj_dx, r.inj_dy, r.rx, r.ry,
                  r.clamped ? 1 : 0, r.looking ? 1 : 0);
    AppendLocked(buf);
  }

  // cprint override: one line from scripts/recomp/mouse_diag.lua (the text
  // after the "[f2diag] " tag). Lines starting with "U " count as camera updates.
  void OnLuaLine(std::string_view text) {
    const int64_t t = NowUs();
    const bool log = Enabled();
    std::lock_guard<std::mutex> lock(mu_);
    const bool update = text.size() >= 2 && text[0] == 'U' && text[1] == ' ';
    if (update) {
      ++second_.updates;
      ++status_.updates;
    }
    if (!log) return;
    EnsureWriterLocked();
    std::string line;
    line.reserve(text.size() + 32);
    if (update) {
      // "U <rest>" -> "U t=<us> <rest>"
      line += "U t=" + std::to_string(t) + " ";
      line.append(text.substr(2));
    } else {
      line += "L t=" + std::to_string(t) + " ";
      line.append(text);
    }
    line += '\n';
    AppendLocked(line);
  }

  // Free-form note (commands, mode changes), so a log explains itself.
  void Note(const std::string& text) {
    if (!Enabled()) return;
    std::lock_guard<std::mutex> lock(mu_);
    EnsureWriterLocked();
    AppendLocked("N t=" + std::to_string(NowUs()) + " " + text + "\n");
  }

  // `f2 mouse`: rates since the previous call, plus the last poll.
  std::string StatusAndReset() {
    const int64_t t = NowUs();
    std::lock_guard<std::mutex> lock(mu_);
    std::string s = FormatWindow("since-last", t, status_);
    char buf[160];
    std::snprintf(buf, sizeof buf, " | last poll dx=%d dy=%d inj=%d,%d rx=%d ry=%d clamp=%d look=%d",
                  last_.dx, last_.dy, last_.inj_dx, last_.inj_dy, last_.rx, last_.ry,
                  last_.clamped ? 1 : 0, last_.looking ? 1 : 0);
    s += buf;
    status_ = Window{};
    status_.start_us = t;
    return s;
  }

  // Starts the writer even before the first record (`f2 mouse diag on`).
  void Start() {
    std::lock_guard<std::mutex> lock(mu_);
    EnsureWriterLocked();
  }

 private:
  Diag() {
    second_.start_us = status_.start_us = NowUs();
  }

  static constexpr size_t kMaxBuffer = 8u << 20;  // 8 MB between writes

  void AppendLocked(const std::string& line) {
    if (buf_.size() + line.size() > kMaxBuffer) {
      ++second_.dropped;
      return;
    }
    buf_ += line;
  }

  void EnsureWriterLocked() {
    if (writer_started_) return;
    writer_started_ = true;
    std::thread([this] { WriterLoop(); }).detach();
  }

  std::FILE* OpenLog() {
#if defined(_WIN32)
    // Shared read so the log can be watched while the game runs.
    return ::_fsopen("fable2_mouse.log", "w", _SH_DENYNO);
#else
    return std::fopen("fable2_mouse.log", "w");
#endif
  }

  void WriterLoop() {
    std::FILE* f = OpenLog();
    if (f) {
      std::fprintf(f, "# fable2_mouse.log v1: P = input poll, U = camera update (Lua trace), "
                      "S = per-second summary, N = note, L = other trace text. t in microseconds.\n");
      std::fflush(f);
    }
    int64_t next_summary = NowUs() + 1000000;
    std::string out;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      const int64_t t = NowUs();
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (t >= next_summary) {
          if (Enabled()) buf_ += FormatWindow("S", t, second_) + "\n";
          second_ = Window{};
          second_.start_us = t;
          next_summary = t + 1000000;
        }
        out.swap(buf_);
      }
      if (f && !out.empty()) {
        std::fwrite(out.data(), 1, out.size(), f);
        std::fflush(f);
      }
      out.clear();
    }
  }

  std::mutex mu_;
  std::string buf_;
  bool writer_started_ = false;
  int64_t last_poll_us_ = 0;
  PollRecord last_{};
  Window second_{};
  Window status_{};
};

// --- Synthetic movement (`f2 mouse inject`) -------------------------------

// Pixels delivered by the injector but not yet taken by a poll.
inline std::atomic<int32_t>& PendingX() {
  static std::atomic<int32_t> v{0};
  return v;
}
inline std::atomic<int32_t>& PendingY() {
  static std::atomic<int32_t> v{0};
  return v;
}

// Input driver: take the synthetic movement that arrived since the last poll.
inline void TakeInjected(int32_t& dx, int32_t& dy) {
  dx = PendingX().exchange(0, std::memory_order_relaxed);
  dy = PendingY().exchange(0, std::memory_order_relaxed);
}

// Spreads a total movement evenly over a duration, like a hand moving at a
// constant speed. Pure, so it can be unit tested: given the elapsed time it
// returns the pixels that should have been delivered by then.
struct InjectPlan {
  int32_t total_x = 0, total_y = 0;
  int64_t duration_us = 0;

  void DeliveredBy(int64_t elapsed_us, int32_t& x, int32_t& y) const {
    if (duration_us <= 0 || elapsed_us >= duration_us) {
      x = total_x;
      y = total_y;
      return;
    }
    if (elapsed_us <= 0) {
      x = y = 0;
      return;
    }
    x = static_cast<int32_t>(static_cast<int64_t>(total_x) * elapsed_us / duration_us);
    y = static_cast<int32_t>(static_cast<int64_t>(total_y) * elapsed_us / duration_us);
  }
};

class Injector {
 public:
  static Injector& Get() {
    static Injector i;
    return i;
  }

  // Replaces any movement still in progress.
  void Start(int32_t dx, int32_t dy, int64_t duration_ms) {
    std::lock_guard<std::mutex> lock(mu_);
    plan_ = InjectPlan{dx, dy, std::max<int64_t>(duration_ms, 0) * 1000};
    start_us_ = NowUs();
    sent_x_ = sent_y_ = 0;
    active_ = true;
    if (!thread_started_) {
      thread_started_ = true;
      std::thread([this] { Loop(); }).detach();
    }
  }

  void Stop() {
    std::lock_guard<std::mutex> lock(mu_);
    active_ = false;
  }

  bool Active() {
    std::lock_guard<std::mutex> lock(mu_);
    return active_;
  }

 private:
  void Loop() {
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      std::lock_guard<std::mutex> lock(mu_);
      if (!active_) continue;
      int32_t x = 0, y = 0;
      const int64_t elapsed = NowUs() - start_us_;
      plan_.DeliveredBy(elapsed, x, y);
      PendingX().fetch_add(x - sent_x_, std::memory_order_relaxed);
      PendingY().fetch_add(y - sent_y_, std::memory_order_relaxed);
      sent_x_ = x;
      sent_y_ = y;
      if (elapsed >= plan_.duration_us) active_ = false;
    }
  }

  std::mutex mu_;
  InjectPlan plan_{};
  int64_t start_us_ = 0;
  int32_t sent_x_ = 0, sent_y_ = 0;
  bool active_ = false;
  bool thread_started_ = false;
};

}  // namespace fable2::mousediag
