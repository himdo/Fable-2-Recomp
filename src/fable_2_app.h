// fable_2 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/filesystem.h>

#include <rex/perf/counter.h>
#include <rex/rex_app.h>
#include <rex/system/gpu_plugin.h>

#include <atomic>
#include <chrono>
#include <thread>

#include <windows.h>

#include "alloc_watch.h"
#include "fps_probe.h"
#include "keyboard_gamepad.h"

class Fable2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Fable2App>(new Fable2App(ctx, "fable_2",
        PPCImageConfig));
  }

  // Emulate the Xbox 360 Xenos GPU. Two plugins are staged next to the exe:
  //   rexgpu-xenos[d].dll        -> D3D12 (prebuilt SDK plugin; the default)
  //   rexgpu-xenos-vulkan[d].dll -> Vulkan (built from the SDK source via
  //                                   tools/build_sdk_vulkan.cmd)
  // Swap the renderer at launch with --gpu_plugin, no rebuild required:
  //   --gpu_plugin=xenos            D3D12 (default)
  //   --gpu_plugin=xenos-vulkan     Vulkan
  void OnPreSetup(rex::RuntimeConfig& config) override {
    std::string plugin = config.gpu_plugin.empty() ? "xenos" : config.gpu_plugin;
    config.gpu_plugin = plugin;
    if (plugin == "xenos-vulkan") {
      // Load the source-built plugin and force the Vulkan backend. It is
      // compiled with both D3D12 and Vulkan; the default "any" would pick
      // D3D12, so pass "vulkan" explicitly to get the VulkanGraphicsSystem.
      config.graphics = rex::system::LoadGpuPlugin("xenos-vulkan", "vulkan");
    }
    // Otherwise leave config.graphics null so ReXApp loads
    // LoadGpuPlugin("xenos") -> the prebuilt D3D12 plugin.

    // Build on top of the default input system (SDL gamepad + NOP) and add a
    // synthetic "keyboard gamepad" driver so host keys can drive the guest.
    // The mapping is the `keyboard_gamepad_map` cvar (default "E:A"). See
    // src/keyboard_gamepad.h.
    config.input_factory = [](bool tool_mode) ->
        std::unique_ptr<rex::system::IInputSystem> {
      auto system = rex::input::CreateDefaultInputSystem(tool_mode);
      system->AddDriver(
          std::make_unique<fable2::KeyboardGamepadDriver>(system->window(), 0));
      return system;  // C++14 unique_ptr<Derived> -> unique_ptr<Base>
    };
  }

  void OnPostSetup() override {
    // TEMP: 30fps-cap instrumentation is active via the strong symbol
    // overrides in fps_probe.h (writes fps_probe.log next to the exe).

    // Feed the F3 debug overlay with guest FPS (below). The GPU plugin
    // records per-guest-swap frame timing into the shared perf registry
    // (rex::perf, state lives in rexruntime.dll and is shared with the
    // plugin); read the last snapshot and expose it as FrameStats so the
    // overlay's "Guest: X FPS (Y ms)" line works on every runtime build.

    // Background monitor for the table-zeroing / init-ordering bug. Runs on a
    // SEPARATE OS thread with hardcoded stable addresses (no recompiled probe
    // dependency), so it does not perturb the recompiled hot path that the
    // race depends on. It samples the allocator table continuously from t=0
    // and distinguishes:
    //   (A) table valid then zeroed  -> "HEAD WENT NULL" after "FIRST VALID"
    //   (B) never valid (consumer first) -> only "null" ever, no "FIRST VALID"
    std::thread([]() {
      using namespace fable2::allocwatch;
      const uintptr_t base = kGuestBase;  // 0x100000000, stable
      auto t0 = std::chrono::steady_clock::now();
      auto ms = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(); };
      // The guest arena is commit-on-fault; a page may be uncommitted when we
      // sample early. Use VirtualQuery to only read committed pages, so an
      // early sample is skipped instead of faulting the monitor thread.
      auto committed = [](const volatile void* p) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (::VirtualQuery(const_cast<void*>(const_cast<const void*>(p)), &mbi, sizeof(mbi)) == 0) return false;
        return mbi.State == MEM_COMMIT &&
               (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_WRITECOPY)) != 0;
      };
      auto safe_sample = [&](uint32_t& tbl, uint32_t& flag, uint32_t& head) -> bool {
        tbl = flag = head = 0;
        if (!committed(GuestPtr(base, kTablePtrGuest))) return false;
        tbl = *GuestPtr(base, kTablePtrGuest);
        flag = *GuestByte(base, kFlagGuestAddr);
        if (tbl != 0) {
          if (!committed(GuestPtr(base, tbl + 12))) return false;
          head = *GuestPtr(base, tbl + 12);
        }
        return true;
      };
      bool first_valid = false;
      bool ever_valid = false;
      int null_samples = 0;
      while (true) {
        uint32_t tbl, flag, head;
        if (!safe_sample(tbl, flag, head)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
          continue;  // page not committed yet
        }
        if (tbl != 0 && head != 0 && !first_valid) {
          first_valid = true; ever_valid = true;
          REXSYS_ERROR("[alloc-watch] FIRST VALID head: t={}ms tbl@83496BD4=0x{:08X} head=0x{:08X} flag=0x{:02X}",
                       ms(), tbl, head, flag);
        }
        if (tbl != 0 && head != 0) ever_valid = true;
        // Log the transition valid->null (the zeroing) with full context.
        static uint32_t s_prev_head = 0;
        static bool s_have_prev = false;
        if (tbl != 0) {
          if (s_have_prev && s_prev_head != 0 && head == 0) {
            uint32_t ev = null_samples++;
            if (ev < 6) {
              REXSYS_ERROR("[alloc-watch] HEAD WENT NULL: t={}ms tbl=0x{:08X} prevHead=0x{:08X} flag=0x{:02X} ever_valid={} ; dump:",
                           ms(), tbl, s_prev_head, flag, ever_valid);
              for (int o = 0; o < 20; o += 4)
                REXSYS_ERROR("[alloc-watch]   table+{:2d}=0x{:08X}", o, *GuestPtr(base, tbl + o));
              for (uint32_t a = 0x83496BC0; a < 0x83496BF0; a += 4)
                REXSYS_ERROR("[alloc-watch]   0x{:08X}=0x{:08X}", a, *GuestPtr(base, a));
            }
          }
          s_prev_head = head; s_have_prev = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }).detach();

    SetGuestFrameStats([]() {
      rex::ui::FrameStats stats;
      auto ft_us =
          rex::perf::GetSnapshotCounter(rex::perf::CounterId::kFrameTimeUs);
      if (ft_us > 0) {
        stats.frame_time_ms = double(ft_us) / 1000.0;
        stats.fps = 1000000.0 / double(ft_us);
        stats.frame_count = 1;  // non-zero = "has data" (gates overlay text)
      }
      return stats;
    });
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}

  // Content root (mounted as game:/ in the guest VFS). Resolved relative to
  // the executable's own location: the content (default.xex, data/, ...) is
  // expected next to the exe or in an ancestor directory (the exe lives in
  // out/build/<preset>/, the content at the project root). Override on the
  // command line with --game_data_root=<path>.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    std::filesystem::path content_dir;
    if (paths.game_data_root.empty()) {
      auto dir = rex::filesystem::GetExecutableFolder();
      for (int i = 0; i < 5; ++i) {
        if (std::filesystem::is_regular_file(dir / "default.xex")) {
          content_dir = dir;
          break;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
      }
      paths.game_data_root = content_dir;
    } else {
      content_dir = paths.game_data_root;
    }

    // Keep user data (save files, settings, profiles) in the game's own
    // folder (the folder containing default.xex) instead of the SDK's
    // default per-user platform location, which caused save corruption.
    // NOTE: set unconditionally because the SDK default is non-empty, so an
    // empty-check would never trigger (overrides this cvar/CLI default).
    if (!content_dir.empty()) {
      paths.user_data_root = content_dir;
      std::error_code ec;
      std::filesystem::create_directories(content_dir, ec);
    }
    // Mount the $SystemUpdate folder (next to the game content) as update:\
    // so VdSetGraphicsInterruptCallback-era update partition lookups resolve
    // instead of failing with "device not found".
    if (paths.update_data_root.empty() && !paths.game_data_root.empty()) {
      auto update_dir = paths.game_data_root / "$SystemUpdate";
      if (std::filesystem::is_directory(update_dir)) {
        paths.update_data_root = update_dir;
      }
    }
  }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
