// progress_report.cpp - see progress_report.h.

#include "progress_report.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cvars.h"
#include "progress_checklist.h"

#include <rex/cvar.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace customrenderer {
namespace progress_report {

namespace {

constexpr int kSnapshotSize = 2048;
constexpr int kTitleMax = 256;

// Written on the report thread, read by the app's overlay thread: a torn
// read is a cosmetic worst case (one garbled HUD frame), so no lock.
char g_snapshot[kSnapshotSize] = "Custom D3D12 renderer (not started)";

std::atomic<bool> g_running{false};
std::thread g_thread;
std::mutex g_log_mutex;
FILE* g_log = nullptr;

uint64_t CountOf(int index) {
  return progress::Find(index)
             ? progress::Features()[index].count.load(std::memory_order_relaxed)
             : 0;
}

#ifdef _WIN32
// The app's main window: the largest visible non-dialog top-level window of
// this process. Cached; re-resolved if SetWindowText fails.
HWND g_main_window = nullptr;
bool g_main_window_resolved = false;

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lparam) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != GetCurrentProcessId()) {
    return TRUE;
  }
  if (!IsWindowVisible(hwnd)) {
    return TRUE;
  }
  wchar_t class_name[64] = {};
  GetClassNameW(hwnd, class_name, 63);
  // Skip dialog/message-box classes and helper windows.
  if (wcscmp(class_name, L"#32770") == 0 || wcscmp(class_name, L"#32771") == 0 ||
      wcscmp(class_name, L"#32769") == 0 || wcscmp(class_name, L"IME") == 0) {
    return TRUE;
  }
  RECT rect{};
  if (!GetClientRect(hwnd, &rect)) {
    return TRUE;
  }
  const int area = rect.right * rect.bottom;
  auto* best = reinterpret_cast<std::pair<HWND, int>*>(lparam);
  if (area > best->second) {
    best->first = hwnd;
    best->second = area;
  }
  return TRUE;
}

HWND FindMainWindow() {
  std::pair<HWND, int> best{nullptr, -1};
  EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&best));
  return best.first;
}

HWND ResolveMainWindow() {
  if (!g_main_window_resolved) {
    g_main_window = FindMainWindow();
    g_main_window_resolved = true;
  }
  if (!g_main_window || !IsWindow(g_main_window)) {
    g_main_window = FindMainWindow();
    if (!g_main_window) {
      return nullptr;  // window not up yet (very early boot)
    }
  }
  return g_main_window;
}

// ---------------------------------------------------------------------------
// On-screen HUD: a click-through, layered, always-on-top window over the
// game window's top-left corner. The app's ImGui surface is not usable from
// the plugin (rexruntime's export table does not expose ImGuiDialog or the
// raw ImGui API), so the plugin owns its own overlay. Once the plugin draws
// its own D3D12 present pass (M1), this can be replaced by a GPU-drawn HUD;
// for M0 a layered GDI window is dependency-free and shows up immediately.

struct Hud {
  HWND hwnd = nullptr;
  HFONT font = nullptr;
  std::wstring last_text;
  int last_x = 0;
  int last_y = 0;
};

Hud g_hud;

LRESULT CALLBACK HudWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void HudInit() {
  if (g_hud.hwnd) {
    return;
  }
  g_hud.font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, FIXED_PITCH | FF_SWISS, L"Consolas");
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &HudWindowProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"RexGpuCustomRendererHud";
  RegisterClassExW(&wc);  // a benign failure (already registered) is fine
  g_hud.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
                                   WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                               L"RexGpuCustomRendererHud", L"",
                               WS_POPUP, 0, 0, 4, 4, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
}

void HudUpdate(const char* text, HWND main_window) {
  if (!REXCVAR_GET(custom_renderer_hud)) {
    if (g_hud.hwnd) {
      ShowWindow(g_hud.hwnd, SW_HIDE);
    }
    return;
  }
  if (!main_window) {
    return;
  }
  HudInit();
  if (!g_hud.hwnd) {
    return;
  }
  POINT origin{};
  ClientToScreen(main_window, &origin);
  const int kMargin = 8;
  const int x = origin.x + kMargin;
  const int y = origin.y + kMargin;

  const std::wstring wide(text, text + strnlen(text, kSnapshotSize));
  if (wide == g_hud.last_text && x == g_hud.last_x && y == g_hud.last_y) {
    return;  // nothing changed; skip the re-blit
  }
  g_hud.last_text = wide;
  g_hud.last_x = x;
  g_hud.last_y = y;

  // Split into lines and measure.
  std::vector<std::wstring> lines;
  std::wstring current;
  for (wchar_t c : wide) {
    if (c == L'\n') {
      lines.push_back(current);
      current.clear();
    } else if (c != L'\r') {
      current += c;
    }
  }
  lines.push_back(current);

  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen);
  HGDIOBJ old_font = SelectObject(dc, g_hud.font);
  const int line_height = 18;
  const int pad = 6;
  int max_chars = 0;
  for (const auto& line : lines) {
    max_chars = (int)std::max(max_chars, (int)line.size());
  }
  SIZE extent{};
  GetTextExtentPoint32W(dc, L"0", 1, &extent);
  const int width = std::min(720, (int)(max_chars * extent.cx + pad * 2));
  const int height = (int)lines.size() * line_height + pad * 2;
  SelectObject(dc, old_font);
  DeleteDC(dc);

  // 32bpp ARGB bitmap (GDI BGRX order) + opaque 1bpp mask for
  // UpdateLayeredWindow.
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = width;
  bi.bmiHeader.biHeight = -height;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bmp) {
    ReleaseDC(nullptr, screen);
    return;
  }
  HBITMAP mask = CreateBitmap(width, height, 1, 1, nullptr);
  HDC dc2 = CreateCompatibleDC(screen);
  HBITMAP old_bmp = (HBITMAP)SelectObject(dc2, bmp);
  HBITMAP old_mask = (HBITMAP)SelectObject(
      dc2, mask ? mask : GetStockObject(NULL_BRUSH));

  // Draw the dark background + colored text straight into the DIB, then set
  // the alpha byte of every pixel (BI_RGB leaves it zeroed, which
  // UpdateLayeredWindow would treat as fully transparent).
  RECT fill_rect{0, 0, width, height};
  HBRUSH bg_brush = CreateSolidBrush(RGB(8, 10, 16));
  FillRect(dc2, &fill_rect, bg_brush);
  DeleteObject(bg_brush);
  HGDIOBJ old_font2 = SelectObject(dc2, g_hud.font ? g_hud.font : GetStockObject(DEFAULT_GUI_FONT));
  SetBkMode(dc2, TRANSPARENT);
  int ty = pad;
  for (const auto& line : lines) {
    COLORREF fg = RGB(210, 210, 210);
    if (!line.empty() && line[0] == L'[') {
      fg = (line[1] == L'x') ? RGB(120, 235, 120) : RGB(150, 150, 150);
    } else if (&line == &lines.front()) {
      fg = RGB(90, 200, 255);
    }
    SetTextColor(dc2, fg);
    TextOutW(dc2, pad, ty, line.c_str(), (int)line.size());
    ty += line_height;
  }
  SelectObject(dc2, old_font2);

  static const uint8_t kPanelAlpha = 235;
  uint8_t* pixels = static_cast<uint8_t*>(bits);
  const size_t pixel_count = static_cast<size_t>(width) * height;
  for (size_t i = 3; i < pixel_count * 4; i += 4) {
    pixels[i] = kPanelAlpha;
  }

  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  POINT src_pt{};
  POINT dst_pt{x, y};
  SIZE size{width, height};
  UpdateLayeredWindow(g_hud.hwnd, screen, &dst_pt, &size, dc2, &src_pt, 0, &blend, 2);

  SelectObject(dc2, old_bmp);
  SelectObject(dc2, old_mask);
  DeleteDC(dc2);
  DeleteObject(bmp);
  if (mask) {
    DeleteObject(mask);
  }
  ReleaseDC(nullptr, screen);
}

void UpdateTitle() {
  if (!REXCVAR_GET(custom_renderer_title)) {
    return;
  }
  HWND main_window = ResolveMainWindow();
  if (!main_window) {
    return;
  }
  char reg_s[16], swap_s[16], draw_s[16];
  progress::FormatHumanCount(CountOf(2), reg_s, sizeof(reg_s));
  progress::FormatHumanCount(CountOf(3), swap_s, sizeof(swap_s));
  progress::FormatHumanCount(CountOf(9), draw_s, sizeof(draw_s));
  char title[kTitleMax];
  std::snprintf(title, sizeof(title),
                "Custom D3D12 (M0) %d%% | reg %s | swaps %s | draws %s",
                int(progress::PercentComplete()), reg_s, swap_s, draw_s);
  std::wstring wide(title, title + strnlen(title, sizeof(title)));
  if (!SetWindowTextW(main_window, wide.c_str())) {
    g_main_window = nullptr;  // force re-resolve next tick
  }
}
#else
void UpdateTitle() {}
#endif

void OpenLog(const std::filesystem::path& exe_dir) {
  std::error_code ec;
  std::filesystem::create_directories(exe_dir / "logs", ec);
  if (!g_log) {
    g_log = std::fopen((exe_dir / "logs" / "renderer-custom.log").string().c_str(), "a");
  }
}

void LogLine(const char* text) {
  if (!g_log) {
    return;
  }
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  std::lock_guard<std::mutex> lock(g_log_mutex);
  std::fprintf(g_log, "[custom-renderer] t=%lldms %s\n", (long long)now_ms, text);
  std::fflush(g_log);
}

void LogSnapshotOncePerLine() {
  char line[1024];
  progress::FormatSnapshot(line, sizeof(line));
  // Multi-line snapshot -> one log entry per line.
  char* cursor = line;
  while (*cursor) {
    char* end = std::strchr(cursor, '\n');
    if (end) {
      *end = '\0';
    }
    LogLine(cursor);
    if (!end) {
      break;
    }
    cursor = end + 1;
  }
}

void ReportLoop(const std::filesystem::path& exe_dir) {
  OpenLog(exe_dir);
  auto last_log = std::chrono::steady_clock::now();
  while (g_running.load(std::memory_order_relaxed)) {
    progress::FormatSnapshot(g_snapshot, kSnapshotSize);
    UpdateTitle();
    HudUpdate(g_snapshot, ResolveMainWindow());

    const int interval_s = REXCVAR_GET(custom_renderer_log_interval);
    const auto now = std::chrono::steady_clock::now();
    if (interval_s > 0 && now - last_log >= std::chrono::seconds(interval_s)) {
      last_log = now;
      LogSnapshotOncePerLine();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

}  // namespace

void Start(const std::filesystem::path& exe_dir) {
  bool expected = false;
  if (!g_running.compare_exchange_strong(expected, true)) {
    return;
  }
  std::atexit(Stop);
  g_thread = std::thread(ReportLoop, exe_dir);
}

void Stop() {
  if (!g_running.exchange(false)) {
    return;
  }
  if (g_thread.joinable()) {
    g_thread.join();
  }
#ifdef _WIN32
  if (g_hud.hwnd) {
    DestroyWindow(g_hud.hwnd);
    g_hud.hwnd = nullptr;
  }
  if (g_hud.font) {
    DeleteObject(g_hud.font);
    g_hud.font = nullptr;
  }
#endif
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (g_log) {
    std::fclose(g_log);
    g_log = nullptr;
  }
}

const char* GetSnapshotText() {
  return g_snapshot;
}

}  // namespace progress_report
}  // namespace customrenderer
