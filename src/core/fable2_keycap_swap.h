// Live swap of button prompts between controller icons and keyboard keycaps,
// following guest user 0's most recent input device.
//
// The prompt textures (src/gui/prompt_textures.h) come in two versions. A
// bank read hook serves the version for the current mode whenever the game
// loads one, and a worker thread patches textures already in memory when the
// mode changes, dropping the GPU's copy so it re-uploads.
//
// Threading: the textures are fixed once Init runs. The read hook (a game
// thread) only touches each slot's atomic flags; the worker owns the address
// cache. The mode is atomic, and the worker keeps patching until the textures
// match the latest mode.
#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/system/xfile.h>
#include <rex/system/xmemory.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "../gui/prompt_texture.h"
#include "keycap_texture_scan.h"

namespace fable2::keycap {

// fable2_keycap_swap.log next to the exe, started fresh each launch.
inline void Log(const std::string& msg) {
  static std::FILE* f = [] {
#ifdef _WIN32
    wchar_t exe_path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) return static_cast<std::FILE*>(nullptr);
    const auto path = std::filesystem::path(exe_path).parent_path() / "fable2_keycap_swap.log";
    return std::fopen(path.string().c_str(), "wb");
#else
    return std::fopen("fable2_keycap_swap.log", "wb");
#endif
  }();
  static std::mutex lock;
  if (!f) return;
  std::lock_guard guard(lock);
  std::fprintf(f, "%s\n", msg.c_str());
  std::fflush(f);
}

class KeycapSwap {
 public:
  // Takes the prompt textures; call before Init.
  void SetTextures(std::vector<gui::PromptTexture> textures) {
    slots_.clear();
    for (auto& t : textures) slots_.push_back(Slot{std::move(t)});
    flags_ = std::make_unique<SlotFlags[]>(slots_.size());
  }

  // Installs the bank read hook. Does nothing without textures, so a launch
  // without keyboard prompts is unaffected.
  void Init(rex::memory::Memory* memory) {
    memory_ = memory;
    enabled_ = memory_ != nullptr && !slots_.empty();
    Log("Init: " + std::to_string(slots_.size()) + " prompt textures" +
        (enabled_ ? "" : ", swap off"));
    if (enabled_) rex::system::RexSetXFilePostReadHook(&KeycapSwap::OnFileReadHook);
  }

  // Call once per frame. Switches to keycaps when the latest input came from a
  // synthetic (keyboard/mouse) device, to controller icons otherwise.
  void UpdateFromActiveDevice() {
    if (!enabled_) return;
    const int target = ActiveDeviceIsKeyboard() ? 1 : 0;
    if (target == mode_.load()) return;
    {
      std::lock_guard lock(worker_mutex_);
      mode_.store(target);
      if (!worker_.joinable()) worker_ = std::thread([this] { WorkerLoop(); });
    }
    worker_cv_.notify_one();
  }

  bool keyboard_mode() const { return mode_.load() != 0; }

  ~KeycapSwap() {
    {
      std::lock_guard lock(worker_mutex_);
      stop_ = true;
    }
    worker_cv_.notify_one();
    if (worker_.joinable()) worker_.join();
  }

 private:
  // Guest physical memory is reached through its 0xA0000000 view, which the
  // GPU texture cache tracks.
  static constexpr uint32_t kPhysicalView = 0xA0000000u;
  static constexpr uint64_t kPhysicalSize = 0x20000000ull;

  struct Slot {
    gui::PromptTexture texture;
    // Where the texture was last found in memory, and its byte order there
    // (1: red and blue swapped, seen for some full-RGBA textures).
    uint32_t addrs[4] = {0, 0, 0, 0};
    int count = 0;
    int order = 0;
  };

  struct SlotFlags {
    std::atomic<bool> off_screen{false};  // not found; skipped until the bank is read again
    std::atomic<int> served_mode{-1};     // mode the last bank read served
  };

  static bool ActiveDeviceIsKeyboard() {
    auto* rt = rex::Runtime::instance();
    auto* input = rt ? static_cast<rex::input::InputSystem*>(rt->input_system()) : nullptr;
    return input && input->ActiveDeviceIsSynthetic(0);
  }

  static bool IsTextureBank(const char* path) {
    if (!path) return false;
    std::string p(path);
    for (char& c : p) c = c == '\\' ? '/' : char(std::tolower(static_cast<unsigned char>(c)));
    return p.find(gui::kTextureBank) != std::string::npos;
  }

  static void OnFileReadHook(const char* path, uint8_t* buffer, size_t bytes, uint64_t offset,
                             uint32_t /*guest_address*/);

  // Serves the current mode's version of any prompt texture in this read.
  void OnFileRead(const char* path, uint8_t* buffer, size_t bytes, uint64_t offset) {
    if (!IsTextureBank(path)) return;
    const int mode = mode_.load();
    for (size_t i = 0; i < slots_.size(); ++i) {
      const auto& payload = slots_[i].texture.payload[mode];
      const uint64_t start = slots_[i].texture.offset;
      const uint64_t end = start + payload.size();
      if (end <= offset || start >= offset + bytes) continue;
      const uint64_t from = std::max(start, offset);
      const uint64_t to = std::min(end, offset + bytes);
      std::memcpy(buffer + (from - offset), payload.data() + (from - start), to - from);
      flags_[i].served_mode.store(mode);
      flags_[i].off_screen.store(false);
    }
  }

  void WorkerLoop() {
    std::unique_lock lock(worker_mutex_);
    for (;;) {
      worker_cv_.wait(lock, [&] { return stop_ || mode_.load() != applied_mode_; });
      if (stop_) return;
      const int target = mode_.load();
      const int from = applied_mode_;
      lock.unlock();
      ApplyModeChange(from, target);
      lock.lock();
      applied_mode_ = target;
    }
  }

  static std::vector<uint8_t> InOrder(const std::vector<uint8_t>& px, int order) {
    if (order == 0) return px;
    std::vector<uint8_t> out = px;
    for (size_t i = 0; i + 4 <= out.size(); i += 4) std::swap(out[i], out[i + 2]);
    return out;
  }

  uint8_t* Physical(uint32_t addr) const { return memory_->physical_membase() + (addr - kPhysicalView); }

  void Patch(uint32_t addr, const std::vector<uint8_t>& bytes) {
    std::memcpy(Physical(addr), bytes.data(), bytes.size());
    memory_->InvalidateGpuCopies(addr, static_cast<uint32_t>(bytes.size()));
  }

  // Patches the cached copies if they all still hold the old version.
  bool PatchCached(Slot& s, int from, int to) {
    if (s.count == 0) return false;
    const auto old_bytes = InOrder(s.texture.live[from], s.order);
    for (int j = 0; j < s.count; ++j)
      if (std::memcmp(Physical(s.addrs[j]), old_bytes.data(), old_bytes.size()) != 0) return false;
    const auto new_bytes = InOrder(s.texture.live[to], s.order);
    for (int j = 0; j < s.count; ++j) Patch(s.addrs[j], new_bytes);
    return true;
  }

  void ApplyModeChange(int from, int to) {
    const auto started = std::chrono::steady_clock::now();
    int patched = 0, cache_hits = 0, off_screen = 0;
    std::vector<size_t> scanned;
    std::vector<ScanTarget> targets;
    for (size_t i = 0; i < slots_.size(); ++i) {
      Slot& s = slots_[i];
      if (flags_[i].off_screen.load() && s.count == 0) continue;
      if (PatchCached(s, from, to)) {
        patched += s.count;
        ++cache_hits;
        continue;
      }
      scanned.push_back(i);
      for (int order = 0; order < (s.texture.raw_bc3 ? 1 : 2); ++order)
        targets.push_back({InOrder(s.texture.live[from], order)});
    }

    FindAll(memory_->physical_membase(), kPhysicalSize, targets);
    size_t next = 0;
    for (size_t i : scanned) {
      Slot& s = slots_[i];
      const int orders = s.texture.raw_bc3 ? 1 : 2;
      const ScanTarget* hit = nullptr;
      int hit_order = 0;
      for (int order = 0; order < orders; ++order, ++next)
        if (!hit && targets[next].found > 0) hit = &targets[next], hit_order = order;
      s.count = hit ? hit->found : 0;
      if (!hit) {
        // A bank read during this switch may already have served the new
        // version, so only a texture served in the old mode is off screen.
        if (flags_[i].served_mode.load() != to) flags_[i].off_screen.store(true);
        ++off_screen;
        continue;
      }
      s.order = hit_order;
      const auto new_bytes = InOrder(s.texture.live[to], s.order);
      for (int j = 0; j < s.count; ++j) {
        s.addrs[j] = kPhysicalView + static_cast<uint32_t>(hit->offsets[j]);
        Patch(s.addrs[j], new_bytes);
        ++patched;
      }
      flags_[i].off_screen.store(false);
      Log("  found " + s.texture.name);
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count();
    Log(std::string(to ? "keyboard" : "controller") + ": patched " + std::to_string(patched) +
        " copies (" + std::to_string(cache_hits) + " cached, " + std::to_string(off_screen) +
        " textures not on screen) in " + std::to_string(ms) + " ms");
  }

  std::vector<Slot> slots_;
  std::unique_ptr<SlotFlags[]> flags_;
  rex::memory::Memory* memory_ = nullptr;
  bool enabled_ = false;
  std::atomic<int> mode_{0};  // 0: controller, 1: keyboard and mouse

  std::thread worker_;
  std::mutex worker_mutex_;
  std::condition_variable worker_cv_;
  int applied_mode_ = 0;  // the mode the textures in memory show (worker_mutex_)
  bool stop_ = false;     // (worker_mutex_)
};

inline KeycapSwap& Instance() {
  static KeycapSwap instance;
  return instance;
}

inline void KeycapSwap::OnFileReadHook(const char* path, uint8_t* buffer, size_t bytes,
                                       uint64_t offset, uint32_t) {
  Instance().OnFileRead(path, buffer, bytes, offset);
}

}  // namespace fable2::keycap
