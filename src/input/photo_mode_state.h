// fable_2 - Photo mode: state shared by the input driver, the guest hooks and
// the on-screen panel.
//
// Photo mode is split over three threads:
//   * the host input poll (KeyboardGamepadDriver::GetDeviceState, see
//     src/input/keyboard_gamepad.h) edge-detects the hotkeys, and while photo
//     mode is on frees the cursor, turns RMB-drag into look deltas and stops
//     feeding the keyboard map into the guest pad;
//   * the guest main thread (src/core/fable2_photo_mode.h) freezes the world,
//     flies the camera, writes the lens/colour settings into the game and
//     restores everything on exit;
//   * the UI thread draws the settings panel (src/core/photo_mode_dialog.h).
// The panel writes Settings under the mutex; the guest thread copies it once
// per world update. Everything else is a single atomic.
#pragma once

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace rex::ui {
class Presenter;
}

namespace fable2::photomode {

// Startup log tag: makes a stale exe obvious in the log.
inline constexpr const char* kBuildTag = "photo-mode test build 1";

// Theme (colour grading) parameters the panel scales. Index = slot in the
// game's 194-entry theme table (descriptor table at 0x8349E848).
enum ThemeSlot : int {
  kFogNearDensity = 16,
  kFogFarDensity = 18,
  kExposure = 22,
  kContrast = 23,
  kBloomThreshold = 34,
  kBloomStrength = 35,
  kOverlayStrength = 43,
  kSaturation = 44,
  kBrightness = 46,
  kSunBeams = 48,  // RadialBlurFactor (the "SunBeams" group)
  kGroundMist = 189,
  kRainDensity = 74,
  kRainSize = 73,
  kSnowSize = 71,
  kSnowFallSpeed = 72,
  kStarBrightness = 70,
  kMoonGlare = 67,
  kSunDiscIntensity = 94,
};

// What the depth-of-field section does.
enum class DofMode : int {
  kGame = 0,    // leave the game's own depth of field alone
  kOff = 1,     // no depth of field
  kFocus = 2,   // focus distance / range / strength sliders
  kRaw = 3,     // the camera's six raw depth-of-field values
};

struct Settings {
  // Camera.
  bool freeze_world = true;
  bool hide_hud = true;
  bool hide_world_icons = true;
  float move_speed = 4.0f;    // metres per second
  float look_speed = 1.0f;    // multiplier on mouse/stick/arrow look
  float fov_deg = 60.0f;
  float roll_deg = 0.0f;

  // Depth of field.
  DofMode dof_mode = DofMode::kGame;
  float dof_focus = 4.0f;     // metres from the camera
  float dof_range = 2.0f;     // depth that stays sharp
  float dof_falloff = 6.0f;   // distance over which blur ramps to full
  float dof_strength = 1.0f;  // 0..1
  float dof_raw[6] = {0, 0, 0, 0, 0, 0};
  float dof_alpha = 1.0f;

  // Lens.
  float vignette_a = 0.0f;  // camera "screen darkening" parameter 1 (cam+196)
  float vignette_b = 0.0f;  // camera "screen darkening" parameter 2 (cam+204)
  float blur = 0.0f;        // full-screen blur (cam+212)

  // Colour: applied by the GPU colour grade over the finished frame (1 = as is).
  float exposure = 1.0f;
  float contrast = 1.0f;
  float saturation = 1.0f;
  float brightness = 1.0f;
  // Light: multipliers on the game's own live values (1 = as is).
  float bloom = 1.0f;
  float bloom_threshold = 1.0f;
  float sun_beams = 1.0f;
  float overlay = 1.0f;
  float fog = 1.0f;
  float ground_mist = 1.0f;
  // Weather and sky (multipliers on the game's own values).
  float rain = 1.0f;
  float snow = 1.0f;
  float stars = 1.0f;
  float moon_glare = 1.0f;
  float sun_disc = 1.0f;
  int filter = 0;  // index into kFilterPresets (the menu's Filter list)
  float filter_strength = 1.0f;  // 0..1, blends the filter in
  float warmth = 0.0f;           // -1 cool .. +1 warm (on top of the filter)
  float tint = 0.0f;             // -1 green .. +1 magenta
  // Composition aids (drawn on screen only; never in a screenshot).
  int guides = 0;      // 0 none, 1 rule of thirds, 2 centre grid, 3 both
  int letterbox = 0;   // 0 none, 1 1.85:1, 2 2.35:1, 3 2.39:1 (cinema bars)

  // Time of day (0..24 h). Only applied while override_time is on.
  bool override_time = false;
  float time_hours = 12.0f;

  // Hero (outfits and pose cards).
  int outfit = 0;          // index into kOutfits (0 = as worn)
  int pose = -1;           // last pose card played (index into kPoses), -1 none
  float pose_hold = 1.5f;  // seconds the world runs after a pose / outfit before it freezes again
};

// --- Hero: outfits and pose cards ---------------------------------------------
// Outfits are the game's own full outfits (Debug.SetHeroWearing<Name>Outfit,
// the console's `outfit <key>`). Poses are the hero's expressions
// (ExpressionPerformer.PerformExpression, the console's `pose <record>`), each
// with the game's own expression icon from gui_textures.bnk.
struct OutfitDef {
  const char* label;
  const char* key;  // console `outfit` name; nullptr = leave as worn
};
inline constexpr OutfitDef kOutfits[] = {
    {"As Worn", nullptr},          {"Barbarian", "barbarian"},     {"Brawler", "brawler"},
    {"Martial Arts", "martialarts"}, {"Pirate", "pirate"},         {"Swashbuckler", "swashbuckler"},
    {"Pure Evil", "pureevil"},     {"Goth", "goth"},               {"Cross-Dresser", "crossdresser"},
    {"Year Outfit", "year"},
};
inline constexpr int kOutfitCount = int(sizeof(kOutfits) / sizeof(kOutfits[0]));

struct PoseDef {
  const char* label;
  const char* record;  // GDB expression record
  const char* icon;    // gui_textures.bnk entry (lower case)
};
inline constexpr PoseDef kPoses[] = {
    {"Heroic Pose", "ExpressionFlexLoop", "art\\gui\\expressions\\flirty\\heroic_pose.tex"},
    {"Sexy Pose", "ExpressionSexyPose", "art\\gui\\expressions\\flirty\\icon_sexy_pose_m.tex"},
    {"Victory Arm Pump", "ExpressionVictoryArmPump", "art\\gui\\expressions\\fun\\victory_arm_pump.tex"},
    {"Dance", "ExpressionDance", "art\\gui\\expressions\\fun\\dance.tex"},
    {"Laugh", "ExpressionLaugh", "art\\gui\\expressions\\fun\\laugh.tex"},
    {"Sock Puppet", "ExpressionSockPuppet", "art\\gui\\expressions\\fun\\sock_puppet.tex"},
    {"Hat, Moustache, Headband", "ExpressionHatMoustacheHeadband",
     "art\\gui\\expressions\\fun\\hat_headband_moustache.tex"},
    {"Thumbs Up", "ExpressionThumbsUp", "art\\gui\\expressions\\social\\thumbsup.tex"},
    {"Thumbs Down", "ExpressionThumbsDown", "art\\gui\\expressions\\social\\thumbsdown.tex"},
    {"Trophy", "ExpressionTrophy", "art\\gui\\expressions\\social\\trophy.tex"},
    {"Wait", "ExpressionWait", "art\\gui\\expressions\\social\\wait.tex"},
    {"Blow Kiss", "ExpressionBlowKiss", "art\\gui\\expressions\\flirty\\blow_kiss.tex"},
    {"Kiss", "ExpressionKiss", "art\\gui\\expressions\\flirty\\kiss.tex"},
    {"Pick-Up Line", "ExpressionPickUpLine", "art\\gui\\expressions\\flirty\\pick_up_line.tex"},
    {"Whistle", "ExpressionWhistle", "art\\gui\\expressions\\flirty\\whistle.tex"},
    {"Worship", "ExpressionWorship", "art\\gui\\expressions\\flirty\\worship.tex"},
    {"Come Back to Mine", "ExpressionComeBackToMyPlace", "art\\gui\\expressions\\flirty\\come_back_to_mine.tex"},
    {"Apologise", "ExpressionApologise", "art\\gui\\expressions\\friendly\\apologise.tex"},
    {"Follow", "ExpressionFollow", "art\\gui\\expressions\\friendly\\follow.tex"},
    {"Gift", "ExpressionGift", "art\\gui\\expressions\\friendly\\gift.tex"},
    {"Bloodlust Roar", "ExpressionBloodlustRoar", "art\\gui\\expressions\\rude\\bloodlust_roar.tex"},
    {"Threaten", "ExpressionThreaten", "art\\gui\\expressions\\scary\\icon_threaten.tex"},
    {"Growl", "ExpressionGrowl", "art\\gui\\expressions\\scary\\growl.tex"},
    {"Scary Laugh", "ExpressionScaryLaugh", "art\\gui\\expressions\\scary\\icon_scary_laugh.tex"},
    {"Fake-Out", "ExpressionFakeout", "art\\gui\\expressions\\scary\\fakeout.tex"},
    {"Slap", "ExpressionSlap", "art\\gui\\expressions\\scary\\slap.tex"},
    {"Extort", "ExpressionExtort", "art\\gui\\expressions\\scary\\icon_extort.tex"},
    {"Point and Laugh", "ExpressionPointAndLaugh", "art\\gui\\expressions\\rude\\point_and_laugh.tex"},
    {"Insult", "ExpressionInsult", "art\\gui\\expressions\\rude\\insult.tex"},
    {"Chicken", "ExpressionChicken", "art\\gui\\expressions\\rude\\chicken.tex"},
    {"Vulgar Thrust", "ExpressionVulgarThrust", "art\\gui\\expressions\\rude\\vulgar_thrust.tex"},
    {"Kiss My Ass", "ExpressionKissMyAss", "art\\gui\\expressions\\rude\\kiss_my_ass.tex"},
    {"Play Dead", "ExpressionPlayDead", "art\\gui\\expressions\\rude\\play_dead.tex"},
    {"Beg", "ExpressionBeg", "art\\gui\\expressions\\rude\\beg.tex"},
    {"Belch", "ExpressionBelch", "art\\gui\\expressions\\rude\\belch.tex"},
    {"Fart", "ExpressionFart", "art\\gui\\expressions\\rude\\fart.tex"},
};
inline constexpr int kPoseCount = int(sizeof(kPoses) / sizeof(kPoses[0]));
inline constexpr int kPoseColumns = 4;
inline constexpr int kPoseRows = 5;
inline constexpr int kPosePage = kPoseColumns * kPoseRows;

// Requests from the menu to the guest thread (index + 1; 0 = none).
inline std::atomic<int>& pose_request() {
  static std::atomic<int> v{0};
  return v;
}
inline std::atomic<int>& outfit_request() {
  static std::atomic<int> v{0};
  return v;
}
// The pose card grid is open (B / Esc go back to the list instead of leaving).
inline std::atomic<bool>& pose_grid_open() {
  static std::atomic<bool> v{false};
  return v;
}
inline std::atomic<int>& nav_back() {
  static std::atomic<int> v{0};
  return v;
}
// Let the world run this many ms (e.g. so a cancelled preview plays out).
inline std::atomic<int>& world_run_request() {
  static std::atomic<int> v{0};
  return v;
}
// Pose cards: steps to move Pose Hold (LT / RT, [ / ]); each step is 0.1 s.
inline std::atomic<int>& nav_scrub() {
  static std::atomic<int> v{0};
  return v;
}
// Runs one developer-console line in the game's Lua (set by the app to
// fable2::console::enqueue_text, which runs it in the game's script update).
inline std::function<void(const std::string&)>& game_command() {
  static std::function<void(const std::string&)> f;
  return f;
}
// True while a line handed to game_command() has not finished running (the
// console's Lua pump answers from the world's script update).
inline std::function<bool()>& game_command_busy() {
  static std::function<bool()> f;
  return f;
}

// Camera filters: colour grades run on the GPU over the finished frame (see
// rex_set_swap_color_grade / color_grade_cs.hlsl), so they work on any scene
// and are in the screenshots. "Filter Strength" blends each one in; the
// Exposure / Contrast / Saturation / Brightness / Warmth / Tint sliders apply
// on top.
struct ColorFilter {
  const char* name;
  float saturation;       // 1 = as is, 0 = black and white
  float warmth, tint;     // white balance: + warmer / + magenta
  float contrast, gamma;  // contrast around mid grey; gamma > 1 darkens
  float tone[3];          // monochrome toning colour (sepia, cyanotype)
  float tone_amount;      // 0..1
  float shadows[3];       // colour pushed into the shadows
  float highlights[3];    // colour pushed into the highlights
  float fade[3];          // faded / lifted blacks colour
  float fade_amount;
};

inline constexpr ColorFilter kFilterPresets[] = {
    // name               sat   warm   tint   con   gamma  tone                tone  shadows                highlights             fade                 fade
    {"Original",          1.00f, 0.00f, 0.00f, 1.00f, 1.00f, {1, 1, 1},          0.0f, {0, 0, 0},             {0, 0, 0},             {0, 0, 0},             0.00f},
    {"Black and White",   0.00f, 0.00f, 0.00f, 1.12f, 1.00f, {1, 1, 1},          0.0f, {0, 0, 0},             {0, 0, 0},             {0, 0, 0},             0.00f},
    {"Noir",              0.00f, 0.00f, 0.00f, 1.45f, 0.95f, {1, 1, 1},          0.0f, {0, 0, 0},             {0, 0, 0},             {0, 0, 0},             0.00f},
    {"Sepia",             1.00f, 0.00f, 0.00f, 1.05f, 0.95f, {1.10f, 0.86f, 0.62f}, 1.0f, {0, 0, 0},          {0, 0, 0},             {0.25f, 0.18f, 0.10f}, 0.06f},
    {"Cyanotype",         1.00f, 0.00f, 0.00f, 1.10f, 1.00f, {0.62f, 0.86f, 1.18f}, 1.0f, {0, 0, 0.02f},      {0, 0, 0},             {0.05f, 0.10f, 0.20f}, 0.06f},
    {"Vintage",           0.70f, 0.35f, 0.00f, 0.90f, 1.00f, {1, 1, 1},          0.0f, {0.03f, 0.01f, -0.02f}, {0.04f, 0.02f, -0.03f}, {0.22f, 0.18f, 0.12f}, 0.12f},
    {"Faded Film",        0.72f, 0.05f, 0.00f, 0.82f, 0.95f, {1, 1, 1},          0.0f, {0.00f, 0.02f, 0.04f},  {0.02f, 0.01f, 0.00f},  {0.20f, 0.20f, 0.22f}, 0.16f},
    {"Teal and Orange",   1.15f, 0.00f, 0.00f, 1.12f, 1.00f, {1, 1, 1},          0.0f, {-0.07f, 0.03f, 0.09f}, {0.10f, 0.03f, -0.07f}, {0, 0, 0},             0.00f},
    {"Golden Hour",       1.12f, 0.55f, 0.05f, 1.05f, 0.95f, {1, 1, 1},          0.0f, {0.02f, 0.00f, -0.02f}, {0.08f, 0.04f, -0.05f}, {0, 0, 0},             0.00f},
    {"Cool Night",        0.70f, -0.60f, 0.05f, 1.08f, 1.12f, {1, 1, 1},         0.0f, {-0.02f, 0.00f, 0.06f}, {0.00f, 0.02f, 0.05f},  {0.02f, 0.03f, 0.08f}, 0.05f},
    {"Dusk",              0.95f, 0.20f, 0.25f, 1.05f, 1.08f, {1, 1, 1},          0.0f, {0.00f, -0.02f, 0.06f}, {0.08f, 0.02f, -0.02f}, {0, 0, 0},             0.00f},
    {"Pastel",            0.60f, 0.05f, 0.05f, 0.80f, 0.85f, {1, 1, 1},          0.0f, {0.02f, 0.00f, 0.04f},  {0, 0, 0},             {0.95f, 0.92f, 0.95f}, 0.12f},
    {"Dreamy",            0.90f, 0.10f, 0.15f, 0.85f, 0.85f, {1, 1, 1},          0.0f, {0.02f, 0.00f, 0.04f},  {0.04f, 0.02f, 0.03f},  {1.00f, 0.93f, 0.95f}, 0.10f},
    {"Polaroid",          0.85f, 0.15f, 0.00f, 0.90f, 0.95f, {1, 1, 1},          0.0f, {0.00f, 0.03f, 0.05f},  {0.05f, 0.03f, -0.03f}, {0.95f, 0.92f, 0.84f}, 0.08f},
    {"Kodachrome",        1.35f, 0.12f, 0.00f, 1.15f, 1.00f, {1, 1, 1},          0.0f, {0.00f, 0.00f, 0.02f},  {0.03f, 0.01f, -0.03f}, {0, 0, 0},             0.00f},
    {"Cross-Process",     1.20f, 0.00f, -0.15f, 1.25f, 1.00f, {1, 1, 1},         0.0f, {0.00f, 0.04f, 0.10f},  {0.08f, 0.08f, -0.10f}, {0, 0, 0},             0.00f},
    {"Matte",             0.85f, 0.00f, 0.00f, 0.85f, 1.00f, {1, 1, 1},          0.0f, {0, 0, 0},             {0, 0, 0},             {0.12f, 0.12f, 0.12f}, 0.14f},
    {"Bleach Bypass",     0.35f, 0.00f, 0.00f, 1.35f, 0.92f, {1, 1, 1},          0.0f, {0, 0, 0},             {0, 0, 0},             {0, 0, 0},             0.00f},
    {"High Key",          0.90f, 0.00f, 0.00f, 0.85f, 0.72f, {1, 1, 1},          0.0f, {0, 0, 0},             {0, 0, 0},             {1, 1, 1},             0.06f},
    {"Low Key",           0.90f, 0.00f, 0.00f, 1.30f, 1.12f, {1, 1, 1},          0.0f, {0, 0, 0},             {0, 0, 0},             {0, 0, 0},             0.00f},
    {"Vivid",             1.50f, 0.00f, 0.00f, 1.12f, 1.00f, {1, 1, 1},          0.0f, {0, 0, 0},             {0, 0, 0},             {0, 0, 0},             0.00f},
};
inline constexpr int kFilterPresetCount = static_cast<int>(sizeof(kFilterPresets) / sizeof(kFilterPresets[0]));

// Picking a filter starts it at full strength; the other sliders are kept.
inline void apply_filter(Settings& s, const ColorFilter&) { s.filter_strength = 1.0f; }

inline std::mutex& settings_mutex() {
  static std::mutex m;
  return m;
}
inline Settings& settings_unlocked() {
  static Settings s;
  return s;
}
inline Settings settings_copy() {
  std::lock_guard<std::mutex> lock(settings_mutex());
  return settings_unlocked();
}

// --- On/off -----------------------------------------------------------------
// want: what the user asked for (hotkey / pad / panel). active: what the guest
// side is actually doing (it enters on the next world update, or refuses when
// the world is not running, e.g. in a menu).
inline std::atomic<bool>& want_active() {
  static std::atomic<bool> v{false};
  return v;
}
inline std::atomic<bool>& active_flag() {
  static std::atomic<bool> v{false};
  return v;
}
inline bool active() { return active_flag().load(std::memory_order_acquire); }
// When the last toggle was asked for (steady-clock ms). The guest drops an
// "enter" request it could not serve quickly (e.g. pressed in a menu), so photo
// mode never pops up later on its own.
inline std::atomic<int64_t>& toggle_time_ms() {
  static std::atomic<int64_t> v{0};
  return v;
}
inline int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
inline void request_toggle() {
  toggle_time_ms().store(now_ms());
  want_active().store(!want_active().load());
}
inline void request_exit() { want_active().store(false); }

// --- One-shot requests from the input poll / panel ---------------------------
inline std::atomic<bool>& screenshot_request() {
  static std::atomic<bool> v{false};
  return v;
}
// Timed and burst screenshots: the panel sets the count, delay and spacing;
// the guest thread takes the shots when their time comes.
struct ShotPlan {
  int shots = 1;           // frames to capture (burst)
  int delay_s = 0;         // wait before the first shot (self-timer)
  int spacing_ms = 400;    // gap between burst shots
};
inline std::mutex& shot_plan_mutex() {
  static std::mutex m;
  return m;
}
inline ShotPlan& shot_plan_unlocked() {
  static ShotPlan p;
  return p;
}
inline std::atomic<int64_t>& shot_next_ms() {  // 0 = nothing scheduled
  static std::atomic<int64_t> v{0};
  return v;
}
inline std::atomic<int>& shots_left() {
  static std::atomic<int> v{0};
  return v;
}
inline std::atomic<bool>& reset_camera_request() {
  static std::atomic<bool> v{false};
  return v;
}
// Panel visibility (H / pad Y). The panel is never in the screenshot anyway.
inline std::atomic<bool>& panel_visible() {
  static std::atomic<bool> v{true};
  return v;
}

// --- Menu navigation (pad on the guest thread, keys on the input poll -> the
// menu on the UI thread). Counters: the menu drains them each frame. ----------
inline std::atomic<int>& nav_y() {  // +1 = down
  static std::atomic<int> v{0};
  return v;
}
inline std::atomic<int>& nav_x() {  // +1 = right (increase)
  static std::atomic<int> v{0};
  return v;
}
inline std::atomic<int>& nav_tab() {  // +1 = next tab
  static std::atomic<int> v{0};
  return v;
}
inline std::atomic<int>& nav_tab_direct() {  // 1..N = jump to tab, 0 = none
  static std::atomic<int> v{0};
  return v;
}
inline std::atomic<int>& nav_activate() {  // A / Enter
  static std::atomic<int> v{0};
  return v;
}
// Which prompts to show: true after pad input, false after keys / mouse.
inline std::atomic<bool>& prompts_pad() {
  static std::atomic<bool> v{false};
  return v;
}

// Key / button auto-repeat: fires on press, then repeats after a pause, faster
// the longer it is held.
struct Repeater {
  bool held = false;
  int64_t start = 0, next = 0;
  int Fire(bool down, int64_t now) {
    if (!down) {
      held = false;
      return 0;
    }
    if (!held) {
      held = true;
      start = next = now;
      next = now + 380;
      return 1;
    }
    if (now < next) return 0;
    const int64_t h = now - start;
    next = now + (h > 1600 ? 35 : h > 900 ? 60 : 100);
    return 1;
  }
};

// --- Host input -> guest -----------------------------------------------------
// RMB-drag look: the input poll adds cursor pixels, the guest drains them.
inline std::atomic<int32_t>& look_dx() {
  static std::atomic<int32_t> v{0};
  return v;
}
inline std::atomic<int32_t>& look_dy() {
  static std::atomic<int32_t> v{0};
  return v;
}
// Mouse wheel over the game (not over the panel): FOV steps, drained by guest.
inline std::atomic<float>& wheel_acc() {
  static std::atomic<float> v{0.0f};
  return v;
}
// Game window focused (set by the input poll; keys are ignored otherwise).
inline std::atomic<bool>& window_focused() {
  static std::atomic<bool> v{false};
  return v;
}
// ImGui wants the mouse / keyboard (cursor over the panel, typing a value).
inline std::atomic<bool>& ui_wants_mouse() {
  static std::atomic<bool> v{false};
  return v;
}
inline std::atomic<bool>& ui_wants_keyboard() {
  static std::atomic<bool> v{false};
  return v;
}

// Pad state captured before photo mode blanks it for the game (guest thread
// writes and reads; kept here so the panel can show "pad connected").
struct Pad {
  uint16_t buttons = 0;
  uint8_t lt = 0, rt = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

// --- Controller shortcut that opens / closes photo mode ----------------------
// A set of X_INPUT_GAMEPAD buttons (0x0400 = Guide) that must all be held; 0 =
// off. Set from [photo_mode] gamepad_toggle at startup and from the menu.
inline constexpr uint16_t kPadGuide = 0x0400;
struct PadButtonName {
  uint16_t mask;
  const char* name;
};
inline constexpr PadButtonName kPadButtonNames[] = {
    {0x0400, "Guide"}, {0x1000, "A"},     {0x2000, "B"},     {0x4000, "X"},    {0x8000, "Y"},
    {0x0100, "LB"},    {0x0200, "RB"},    {0x0010, "Start"}, {0x0020, "Back"}, {0x0040, "L3"},
    {0x0080, "R3"},    {0x0001, "Up"},    {0x0002, "Down"},  {0x0004, "Left"}, {0x0008, "Right"}};

// "Guide", "L3 + R3", "back+start", "Off" -> mask. Returns false on an unknown name.
inline bool ParsePadCombo(const std::string& text, uint16_t& out) {
  uint16_t m = 0;
  std::string tok;
  auto flush = [&]() -> bool {
    size_t a = tok.find_first_not_of(" \t"), b = tok.find_last_not_of(" \t");
    std::string t = a == std::string::npos ? "" : tok.substr(a, b - a + 1);
    tok.clear();
    if (t.empty()) return true;
    for (char& c : t) c = char(std::tolower(uint8_t(c)));
    if (t == "off" || t == "none" || t == "false") return true;
    if (t == "true") t = "guide";
    if (t == "ls" || t == "lthumb") t = "l3";
    if (t == "rs" || t == "rthumb") t = "r3";
    if (t == "select") t = "back";
    if (t == "home" || t == "xbox") t = "guide";
    for (const auto& n : kPadButtonNames) {
      std::string ln = n.name;
      for (char& c : ln) c = char(std::tolower(uint8_t(c)));
      if (ln == t) {
        m |= n.mask;
        return true;
      }
    }
    return false;
  };
  for (char c : text) {
    if (c == '+' || c == ',') {
      if (!flush()) return false;
    } else {
      tok += c;
    }
  }
  if (!flush()) return false;
  out = m;
  return true;
}
inline std::string PadComboName(uint16_t mask) {
  if (mask == 0) return "Off";
  std::string s;
  for (const auto& n : kPadButtonNames) {
    if (!(mask & n.mask)) continue;
    if (!s.empty()) s += " + ";
    s += n.name;
  }
  return s;
}
inline std::atomic<uint16_t>& pad_toggle_mask() {
  static std::atomic<uint16_t> v{kPadGuide};
  return v;
}
// Recording a new shortcut from the menu: the buttons held together are
// collected until all are let go. The pad drives nothing else meanwhile.
inline std::atomic<bool>& pad_rebinding() {
  static std::atomic<bool> v{false};
  return v;
}
inline std::atomic<int64_t>& pad_rebind_started_ms() {
  static std::atomic<int64_t> v{0};
  return v;
}
// Saves the shortcut to fable2_config.toml (set by the app).
inline std::function<void(const std::string&)>& pad_toggle_saver() {
  static std::function<void(const std::string&)> f;
  return f;
}
inline void set_pad_toggle(uint16_t mask) {
  pad_toggle_mask().store(mask);
  if (pad_toggle_saver()) pad_toggle_saver()(PadComboName(mask));
}

// Presenter for screenshots (set by Fable2App; the presenter may not exist yet
// at startup, so it is queried when a screenshot is taken).
inline std::function<rex::ui::Presenter*()>& presenter_provider() {
  static std::function<rex::ui::Presenter*()> f;
  return f;
}

// --- Status shown in the panel -----------------------------------------------
inline std::mutex& status_mutex() {
  static std::mutex m;
  return m;
}
inline std::string& status_text() {
  static std::string s;
  return s;
}
inline void set_status(std::string text) {
  std::lock_guard<std::mutex> lock(status_mutex());
  status_text() = std::move(text);
}
inline std::string status() {
  std::lock_guard<std::mutex> lock(status_mutex());
  return status_text();
}

}  // namespace fable2::photomode
