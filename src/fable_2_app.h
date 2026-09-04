// fable_2 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/filesystem.h>

#include <rex/rex_app.h>

#include "keyboard_gamepad.h"

class Fable2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Fable2App>(new Fable2App(ctx, "fable_2",
        PPCImageConfig));
  }

  // Emulate the Xbox 360 Xenos GPU. The plugin DLL (rexgpu-xenos[d].dll) is
  // staged next to the exe by CMake (GPU_PLUGINS xenos). Override on the
  // command line with --gpu_plugin <name> (empty disables GPU emulation).
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }

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
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
