// fable2_tick_rate - dynamic tick rate (always on).
//
// The game runs two fixed-step ticks: HF (30 Hz originally) and LF (15 Hz),
// with HF at exactly twice LF. The dynamic tick rate locks the HF tick to the
// presented frames, with no frame cap:
//   - the render thread counts presented frames (OnRenderFrame) and keeps a
//     smoothed frame rate (the "plan");
//   - the game's frame loop (ProcessGameFrame_82276C30.cpp) runs one HF tick
//     per presented frame instead of on its own timer, and each tick lasts
//     the real time since the previous one (HF = 1 / that time, LF = HF / 2),
//     so every frame shows exactly one new tick that moved the game by the
//     time the frame really took;
//   - the render thread's "draw one LF period in the past" delay uses the
//     smoothed rate (fable2_hook_render_time_lf), so it does not jump frame
//     to frame.
// Above kMaxHfHz the loop ticks every Nth frame (N =
// ceil(fps / max)), which keeps an even cadence. Below 30 fps the game's own
// 30/15 Hz timer runs, as in the original, so a 30 fps frame limit plays
// exactly like the original game. See docs/patches.md,
// "Dynamic tick rate".

#pragma once

#include <cstdint>

namespace fable2::tickrate {

// Original HF rate, and the guest globals that hold the rates (doubles,
// big-endian).
inline constexpr double kMinHfHz = 30.0;
inline constexpr uint32_t kLfRateAddr = 0x83319510;  // LF tick rate (15.0)
inline constexpr uint32_t kHfRateAddr = 0x83319518;  // HF tick rate (30.0)
// Copy of the HF rate taken by a startup initializer (sub_83242668) and read
// by sub_8235ABA0. In the original game it always equals the HF rate, so it is
// kept in sync whenever the rate changes.
inline constexpr uint32_t kHfRateCopyAddr = 0x83497420;

// Highest HF rate; above it the loop ticks every Nth frame.
inline constexpr double kMaxHfHz = 144.0;

// Render thread, once per presented frame (MainRenderLoop_82B9CD68).
void OnRenderFrame();

// Number of frames presented so far.
uint64_t FramesPresented();

// What the game loop should do now:
//   hf_hz          - smoothed HF rate (the frame rate, or the frame rate / N);
//                    the loop sets each tick's own length, this is used for
//                    the render delay, the under-30 timer and the log;
//   frames_per_tick - 0 = not frame-locked (frame rate under 30: use the
//                     game's own timer at hf_hz), else tick once every this
//                     many presented frames.
struct Plan {
  double hf_hz;
  int frames_per_tick;
};
Plan CurrentPlan();

// Smoothed presented frame rate, for logging.
double MeasuredFps();

}  // namespace fable2::tickrate
