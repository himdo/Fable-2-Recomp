// progress_report.h - Custom renderer: makes progress visible.
//
// A single host thread (500 ms cadence) that:
//   - refreshes the text snapshot buffer served by the
//     `rex_custom_renderer_snapshot` plugin export (read by the app's
//     ImGui overlay),
//   - mirrors a one-line progress string into the game window's title
//     (Win32 SetWindowText on the app's main window),
//   - writes one line per `custom_renderer_log_interval` seconds to
//     logs/renderer-custom.log next to the exe.
//
// All data comes from the backend-agnostic progress checklist
// (progress_checklist.h); a future Vulkan renderer reuses this file as-is.

#pragma once

#include <cstdint>
#include <filesystem>

namespace customrenderer {
namespace progress_report {

// Start the reporting thread. `exe_dir` is where the exe (and thus logs/)
// lives. Safe to call once; no-op if the thread is already running.
void Start(const std::filesystem::path& exe_dir);

// Stop and join the thread (also registered with atexit).
void Stop();

// Stable pointer to the latest multi-line snapshot text (>= 1024 bytes).
// Always valid, even before Start().
const char* GetSnapshotText();

}  // namespace progress_report
}  // namespace customrenderer
