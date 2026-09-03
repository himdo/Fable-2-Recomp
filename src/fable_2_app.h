// fable_2 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/filesystem.h>

#include <rex/rex_app.h>

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
    if (paths.game_data_root.empty()) {
      auto dir = rex::filesystem::GetExecutableFolder();
      for (int i = 0; i < 5; ++i) {
        if (std::filesystem::is_regular_file(dir / "default.xex")) {
          paths.game_data_root = dir;
          break;
        }
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
      }
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
