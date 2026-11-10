// fable_2 - Photo mode settings panel (ImGui, drawn on the UI thread).
//
// Registered once in Fable2App::OnCreateDialogs and drawn every frame by the
// SDK's ImGuiDrawer; it draws nothing while photo mode is off. It only edits
// fable2::photomode::Settings (under its mutex) and raises one-shot requests;
// the guest thread (src/core/fable2_photo_mode.h) applies them to the game.
// The screenshot reads the game's own frame, so the panel never shows in it.
//
// Needs rexruntime.dll to export ImGuiDialog's constructor/destructor and the
// ImGui functions used here (see the SDK's src/system/rexruntime.def).
#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <imgui.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "fable2_config.h"
#include "photo_mode_state.h"

namespace fable2::photomode {

class PhotoModeDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit PhotoModeDialog(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!active()) {
      ui_wants_mouse().store(false, std::memory_order_relaxed);
      ui_wants_keyboard().store(false, std::memory_order_relaxed);
      return;
    }
    // Mouse wheel over the game (not over the panel) zooms.
    if (!io.WantCaptureMouse && io.MouseWheel != 0.0f) {
      float w = wheel_acc().load(std::memory_order_relaxed);
      while (!wheel_acc().compare_exchange_weak(w, w + io.MouseWheel)) {
      }
    }
    if (!panel_visible().load(std::memory_order_relaxed)) {
      ui_wants_mouse().store(false, std::memory_order_relaxed);
      ui_wants_keyboard().store(false, std::memory_order_relaxed);
      DrawHint(io);
      return;
    }

    Settings s = settings_copy();
    const Settings before = s;
    DrawPanel(io, s);
    {
      std::lock_guard<std::mutex> lock(settings_mutex());
      // Only copy what the panel changed, so a value the guest moved in the
      // meantime (roll / FOV keys) is not overwritten with a stale copy.
      MergeChanged(before, s, settings_unlocked());
    }
    ui_wants_mouse().store(io.WantCaptureMouse, std::memory_order_relaxed);
    ui_wants_keyboard().store(io.WantCaptureKeyboard, std::memory_order_relaxed);
  }

 private:
  static void MergeChanged(const Settings& before, const Settings& after, Settings& live) {
#define FABLE2_PM_MERGE(f) \
  if (std::memcmp(&before.f, &after.f, sizeof(before.f)) != 0) \
    std::memcpy(&live.f, &after.f, sizeof(live.f));
    FABLE2_PM_MERGE(freeze_world)
    FABLE2_PM_MERGE(hide_hud)
    FABLE2_PM_MERGE(hide_world_icons)
    FABLE2_PM_MERGE(move_speed)
    FABLE2_PM_MERGE(look_speed)
    FABLE2_PM_MERGE(fov_deg)
    FABLE2_PM_MERGE(roll_deg)
    FABLE2_PM_MERGE(dof_mode)
    FABLE2_PM_MERGE(dof_focus)
    FABLE2_PM_MERGE(dof_range)
    FABLE2_PM_MERGE(dof_falloff)
    FABLE2_PM_MERGE(dof_strength)
    FABLE2_PM_MERGE(dof_raw)
    FABLE2_PM_MERGE(dof_alpha)
    FABLE2_PM_MERGE(vignette_a)
    FABLE2_PM_MERGE(vignette_b)
    FABLE2_PM_MERGE(blur)
    FABLE2_PM_MERGE(exposure)
    FABLE2_PM_MERGE(contrast)
    FABLE2_PM_MERGE(saturation)
    FABLE2_PM_MERGE(brightness)
    FABLE2_PM_MERGE(bloom)
    FABLE2_PM_MERGE(bloom_threshold)
    FABLE2_PM_MERGE(sun_beams)
    FABLE2_PM_MERGE(overlay)
    FABLE2_PM_MERGE(fog)
    FABLE2_PM_MERGE(ground_mist)
    FABLE2_PM_MERGE(rain)
    FABLE2_PM_MERGE(snow)
    FABLE2_PM_MERGE(stars)
    FABLE2_PM_MERGE(moon_glare)
    FABLE2_PM_MERGE(sun_disc)
    FABLE2_PM_MERGE(filter)
    FABLE2_PM_MERGE(override_time)
    FABLE2_PM_MERGE(time_hours)
#undef FABLE2_PM_MERGE
  }

  void DrawHint(ImGuiIO& io) {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 16.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);
    if (ImGui::Begin("##photo_hint", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs)) {
      ImGui::TextDisabled("Photo mode  -  H / pad Y: panel   %s / pad A: screenshot   Esc / pad B: exit",
                          config::Get().photo_mode_screenshot_key.c_str());
    }
    ImGui::End();
  }

  void DrawPanel(ImGuiIO& io, Settings& s) {
    const float width = 360.0f;
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, io.DisplaySize.y > 200.0f ? io.DisplaySize.y - 32.0f : 600.0f),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.82f);
    if (!ImGui::Begin("Photo Mode", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::End();
      return;
    }
    ImGui::PushItemWidth(-130.0f);

    const std::string st = status();
    if (!st.empty()) {
      ImGui::TextWrapped("%s", st.c_str());
      ImGui::Separator();
    }

    if (ImGui::Button("Take screenshot", ImVec2(0.0f, 0.0f))) {
      {
        std::lock_guard<std::mutex> lock(shot_plan_mutex());
        ShotPlan& p = shot_plan_unlocked();
        p.shots = std::clamp(p.shots, 1, 10);
      }
      screenshot_request().store(true);
    }
    if (ImGui::CollapsingHeader("Screenshot", 0)) {
      std::lock_guard<std::mutex> lock(shot_plan_mutex());
      ShotPlan& p = shot_plan_unlocked();
      ImGui::SliderInt("Burst count", &p.shots, 1, 10);
      ImGui::SliderInt("Self-timer", &p.delay_s, 0, 30, "%d s");
      ImGui::SliderInt("Burst spacing", &p.spacing_ms, 100, 2000, "%d ms");
      ImGui::TextDisabled("Burst and timer apply to the next Take screenshot or F12.");
    }
    ImGui::SameLine(0.0f, -1.0f);
    if (ImGui::Button("Hide panel", ImVec2(0.0f, 0.0f))) panel_visible().store(false);
    ImGui::SameLine(0.0f, -1.0f);
    if (ImGui::Button("Exit", ImVec2(0.0f, 0.0f))) request_exit();

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("Pause the world", &s.freeze_world);
      ImGui::Checkbox("Hide HUD", &s.hide_hud);
      ImGui::Checkbox("Hide world icons", &s.hide_world_icons);
      ImGui::SliderFloat("Field of view", &s.fov_deg, 5.0f, 120.0f, "%.1f deg", 0);
      ImGui::SliderFloat("Roll", &s.roll_deg, -180.0f, 180.0f, "%.1f deg", 0);
      ImGui::SliderFloat("Move speed", &s.move_speed, 0.1f, 40.0f, "%.1f m/s",
                         ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("Look speed", &s.look_speed, 0.1f, 4.0f, "%.2fx", 0);
      if (ImGui::Button("Reset camera", ImVec2(0.0f, 0.0f))) reset_camera_request().store(true);
      ImGui::SameLine(0.0f, -1.0f);
      if (ImGui::Button("Level roll", ImVec2(0.0f, 0.0f))) s.roll_deg = 0.0f;
    }

    if (ImGui::CollapsingHeader("Depth of field", ImGuiTreeNodeFlags_DefaultOpen)) {
      int mode = static_cast<int>(s.dof_mode);
      if (ImGui::Combo("Mode", &mode, "Game default\0Off\0Focus\0Raw values\0", -1))
        s.dof_mode = static_cast<DofMode>(mode);
      if (s.dof_mode == DofMode::kFocus) {
        ImGui::SliderFloat("Focus distance", &s.dof_focus, 0.2f, 200.0f, "%.2f m",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Sharp range", &s.dof_range, 0.0f, 50.0f, "%.2f m",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Falloff", &s.dof_falloff, 0.1f, 100.0f, "%.2f m",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Blur strength", &s.dof_strength, 0.0f, 1.0f, "%.2f", 0);
      } else if (s.dof_mode == DofMode::kRaw) {
        ImGui::TextDisabled("The camera's six depth-of-field values.");
        ImGui::SliderFloat("1 near blur", &s.dof_raw[0], 0.0f, 1.0f, "%.3f", 0);
        ImGui::SliderFloat("2 far blur", &s.dof_raw[1], 0.0f, 1.0f, "%.3f", 0);
        ImGui::SliderFloat("3 near plane", &s.dof_raw[2], 0.0f, 200.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("4 near focus", &s.dof_raw[3], 0.0f, 200.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("5 far focus", &s.dof_raw[4], 0.0f, 500.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("6 far plane", &s.dof_raw[5], 0.0f, 1000.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Alpha", &s.dof_alpha, 0.0f, 1.0f, "%.2f", 0);
      }
    }

    if (ImGui::CollapsingHeader("Lens", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::SliderFloat("Vignette", &s.vignette_a, 0.0f, 1.0f, "%.2f", 0);
      ImGui::SliderFloat("Vignette 2", &s.vignette_b, 0.0f, 1.0f, "%.2f", 0);
      ImGui::SliderFloat("Screen blur", &s.blur, 0.0f, 1.0f, "%.2f", 0);
    }

    if (ImGui::CollapsingHeader("Colour and light", ImGuiTreeNodeFlags_DefaultOpen)) {
      std::string names;
      for (int i = 0; i < kFilterPresetCount; ++i) {
        names += kFilterPresets[i].name;
        names += '\0';
      }
      if (ImGui::Combo("Filter", &s.filter, names.c_str(), -1)) {
        apply_filter(s, kFilterPresets[s.filter]);
      }
      ImGui::TextDisabled("1.00x = the game's own value");
      ImGui::SliderFloat("Exposure", &s.exposure, 0.0f, 4.0f, "%.2fx", 0);
      ImGui::SliderFloat("Contrast", &s.contrast, 0.0f, 3.0f, "%.2fx", 0);
      ImGui::SliderFloat("Saturation", &s.saturation, 0.0f, 3.0f, "%.2fx", 0);
      ImGui::SliderFloat("Brightness", &s.brightness, 0.0f, 3.0f, "%.2fx", 0);
      ImGui::SliderFloat("Bloom", &s.bloom, 0.0f, 5.0f, "%.2fx", 0);
      ImGui::SliderFloat("Bloom threshold", &s.bloom_threshold, 0.0f, 3.0f, "%.2fx", 0);
      ImGui::SliderFloat("Sun beams", &s.sun_beams, 0.0f, 5.0f, "%.2fx", 0);
      ImGui::SliderFloat("Film overlay", &s.overlay, 0.0f, 4.0f, "%.2fx", 0);
      ImGui::SliderFloat("Fog", &s.fog, 0.0f, 4.0f, "%.2fx", 0);
      ImGui::SliderFloat("Ground mist", &s.ground_mist, 0.0f, 4.0f, "%.2fx", 0);
      if (ImGui::Button("Reset colour", ImVec2(0.0f, 0.0f))) {
        s.filter = 0;
        apply_filter(s, kFilterPresets[0]);
        s.bloom_threshold = s.ground_mist = 1.0f;
      }
    }

    if (ImGui::CollapsingHeader("Weather and sky", 0)) {
      ImGui::TextDisabled("Weather and sky effects, 1.00x = the game's own value");
      ImGui::SliderFloat("Rain", &s.rain, 0.0f, 3.0f, "%.2fx", 0);
      ImGui::SliderFloat("Snow", &s.snow, 0.0f, 3.0f, "%.2fx", 0);
      ImGui::SliderFloat("Stars", &s.stars, 0.0f, 3.0f, "%.2fx", 0);
      ImGui::SliderFloat("Moon glare", &s.moon_glare, 0.0f, 3.0f, "%.2fx", 0);
      ImGui::SliderFloat("Sun disc", &s.sun_disc, 0.0f, 3.0f, "%.2fx", 0);
    }

    if (ImGui::CollapsingHeader("Time of day", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("Set time of day", &s.override_time);
      if (s.override_time) {
        const int hh = static_cast<int>(s.time_hours) % 24;
        const int mm = static_cast<int>((s.time_hours - std::floor(s.time_hours)) * 60.0f);
        char label[16];
        std::snprintf(label, sizeof(label), "%02d:%02d", hh, mm);
        ImGui::SliderFloat("Time", &s.time_hours, 0.0f, 24.0f, label, 0);
      }
    }

    if (ImGui::CollapsingHeader("Controls", 0)) {
      ImGui::TextWrapped(
          "Keyboard: WASD move, E / Q up and down, hold the right mouse button and drag "
          "to look (or the arrow keys), mouse wheel or PageUp / PageDown zoom, Z / C roll, "
          "X level roll, Shift fast, Ctrl slow, R reset camera, %s screenshot, H hide this "
          "panel, %s or Esc exit.",
          config::Get().photo_mode_screenshot_key.c_str(),
          config::Get().photo_mode_toggle_key.c_str());
      ImGui::TextWrapped(
          "Controller: left stick move, right stick look, LT / RT down and up, LB / RB "
          "roll, D-pad up / down zoom, hold L3 fast, A screenshot, X reset camera, Y hide "
          "this panel, B exit. Click both sticks to enter or leave.");
    }

    ImGui::PopItemWidth();
    ImGui::End();
  }
};

}  // namespace fable2::photomode
