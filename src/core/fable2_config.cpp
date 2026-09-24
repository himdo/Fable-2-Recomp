// fable_2 - ReXGlue Recompiled Project
//
// fable2_config.toml: the recomp's own user config (separate from the ReXGlue
// SDK cvar config fable_2.toml). Look for it next to the exe, create it with
// defaults if missing, parse it, and expose the values via Get().
//
// Growth model: add a member to Values (with its default), read it in Load()
// with one of the Read* helpers, and document the key in the template below.
// Missing keys -> built-in default. Wrong type -> warning + default. Unknown
// sections -> warning. A broken file -> error + dialog + defaults (the game
// still runs), so a hand edit can never wedge the launch.

#include "fable2_config.h"

#include <array>
#include <format>
#include <fstream>

#include <rex/logging/macros.h>
#include <rex/system.h>

#include <toml++/toml.hpp>

namespace fable2::config {
namespace {

Values g_values;

// Kept in sync with config/fable2_config.toml (staged next to the exe by the
// build); this copy is what a first launch writes out.
const char kTemplate[] =
R"TOML_EOF(# =============================================================================
# fable2_config.toml - Fable 2 recomp user configuration
# =============================================================================
#
# Loaded by fable_2.exe on every launch, from the directory the exe lives in.
# If the file is missing, the game recreates it at startup with these
# defaults (the embedded template in src/core/fable2_config.cpp must stay in sync
# with this file).
#
# - Human-readable TOML: # starts a comment, sections are [bracketed].
# - Missing keys are fine: the game falls back to its built-in defaults.
# - Unknown sections/keys are ignored (a warning is logged for unknown
#   sections), so the file can grow over time without breaking the game.
# - A syntax error does not stop the game: it is logged to logs/, a dialog
#   is shown, and the built-in defaults are used instead.
#
# NOTE: this is the recomp's own config. fable_2.toml (also next to the exe)
# is the ReXGlue SDK cvar config - a different file.
# =============================================================================

[general]
# Bumped when this file's format gains new required structure. The game
# tolerates any version it does not know about.
# Default: 1
config_version = 1

[input]
# Host keyboard -> guest gamepad map: "Key:Button,Key:Button,..." (one line;
# do not add spaces or newlines inside the string). Key = Windows virtual-key
# name (see README.md, Keyboard controls); Button = A/B/X/Y, LB/RB, LT/RT,
# Up/Down/Left/Right, Pause, Select, L3/R3, StickUp/StickDown/StickLeft/
# StickRight. Empty string disables the keyboard gamepad.
# --keyboard_gamepad_map, REX_* environment variables and fable_2.toml still
# take priority over this value; the F3 console can change it live.
# Default: the built-in layout
#   E:A,2:B,1:X,3:Y,W:StickUp,S:StickDown,A:StickLeft,D:StickRight,
#   Escape:Pause,M:Select,Q:LT,Tab:RT,F1:Up,F2:Down,F3:Left,F4:Right
keyboard_gamepad_map = "E:A,2:B,1:X,3:Y,W:StickUp,S:StickDown,A:StickLeft,D:StickRight,Escape:Pause,M:Select,Q:LT,Tab:RT,F1:Up,F2:Down,F3:Left,F4:Right"

# Map mouse movement to the guest right stick (camera look): sweep to look,
# stop to stop. false disables mouse look entirely.
# Default: true
mouse_look = true

# Mouse-look sensitivity: right-stick units per pixel of mouse movement
# (1..4096; larger = more sensitive).
# Default: 256
mouse_look_scale = 256

[patches]
# Toggles for the recomp-level (mid-asm hook) patches, consulted at runtime
# by the hook bodies (src/core/fable2_hooks.cpp) - no rebuild needed. Guest-image
# DATA patches are a different file: fable2_patches.toml next to the exe.
# 60 FPS (mid-asm hook fable2_hook_60fps; Xenia "60 FPS" by Margen67):
# lifts the guest main loop from 30/s to ~60/s. false = 30/s (original).
# Default: true
fps_60 = true

# Unlock Website Items (mid-asm hooks fable2_hook_website_g1/g1b/grantnew;
# Xenia "Unlock Website Items" by Guy): forces the website-registration gates
# AND the grant-method result in the Guild-chest item getter so the website
# items are granted at save load. false = locked (original).
# Default: true
unlock_website = true

# Unlock Collectors Edition Content (mid-asm hooks fable2_hook_ce_g1/g1b/
# grantavail; Xenia "Unlock Collectors Edition Content" by Guy): forces the
# CE-registration gates AND the grant-method result in the CE-content getter so
# the CE items are granted at save load. false = locked (original).
# Default: true
unlock_ce = true

[remote]
# Remote control server (AI/automation input channel): a localhost TCP
# server that accepts JSON-lines commands to drive the guest gamepad -
# press/release/stick, timed scripts, cvar get/set. See
# src/input/remote_control_server.h and plans/ai-remote-input-control.md.
# Default: true
enabled = true
# Interface to bind. "127.0.0.1" = this machine only (default). "0.0.0.0" =
# all interfaces (use with a token; a remote attacker could then drive the
# game and set cvars).
# Default: "127.0.0.1"
host = "127.0.0.1"
# TCP port. If the port is busy the game tries port+1..port+9 (the bound
# port is logged at startup). Default: 8791
port = 8791
# Shared token. Empty = no auth. When set, every client connection must send
# {"cmd":"auth","token":"..."} as its first line.
# Default: ""
token = ""

[perf]
# NtYieldExecution batching for the hot-function overrides (see
# src/core/hotfunc/hotfunc_yield.h): every Nth guest yield does the real
# SwitchToThread; the others take a full memory fence. The work-loop yield
# was the single biggest cost in the 475 render chain; on multi-core hosts
# the yield is only a politeness hint, so batch it. 1 = original (yield
# every call); 0 = never yield; larger = fewer context switches.
# Default: 8
hotfunc_yield_every = 8
)TOML_EOF";

std::string_view TypeName(toml::node_type t) {
  return toml::impl::node_type_friendly_names[static_cast<std::size_t>(t)];
}

// Read a typed key from a section. Missing key -> default (silent); the key
// exists but is the wrong kind -> warning + default.
template <typename T>
T Read(const toml::table& section, std::string_view section_name,
       std::string_view key, std::string_view type_name, T default_value) {
  const toml::path key_path{key};
  const auto view = section[key_path];
  if (!view) return default_value;
  if (auto v = view.value_exact<T>()) return static_cast<T>(*v);
  REXSYS_WARN(
      "[fable2-config] [{}] {}: expected {}, got {}; using built-in default",
      section_name, key, type_name, TypeName(view.type()));
  return default_value;
}

}  // namespace

bool Load(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
      REXSYS_ERROR("[fable2-config] could not create {}", path.string());
      return false;
    }
    out << kTemplate;
    REXSYS_INFO("[fable2-config] {} not found; created with defaults",
                path.string());
  }

  // toml++ (v3, exceptions on) returns the table directly and throws a
  // toml::parse_error on a broken file.
  toml::table root;
  try {
    root = toml::parse_file(path.native());
  } catch (const toml::parse_error& err) {
    REXSYS_ERROR("[fable2-config] {} failed to parse at line {}, column {}: {}",
                 path.string(), err.source().begin.line, err.source().begin.column,
                 err.description());
    const std::string msg = std::format(
        "fable2_config.toml could not be parsed\n\n"
        "{}\n\n"
        "File: {}\n"
        "The game will continue with built-in defaults. Fix the file (or "
        "delete it to recreate the defaults) and start again.",
        err.description(), path.string());
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    return false;
  }

  // Warn about unknown top-level sections (usually a typo or a file written
  // for a newer build).
  static constexpr std::array<std::string_view, 5> kKnownSections = {
      "general", "input", "patches", "remote", "perf"};
  for (const auto& [key, value] : root) {
    const bool known =
        std::ranges::find(kKnownSections, key.str()) != kKnownSections.end();
    if (!known && value.is_table())
      REXSYS_WARN("[fable2-config] unknown section [{}] ignored", key.str());
  }

  Values values;
  const toml::path general_path{"general"};
  const auto general = root[general_path];
  if (general.is_table()) {
    values.config_version = static_cast<int32_t>(
        Read<int64_t>(*general.as_table(), "general", "config_version",
                      "integer", 1));
  }
  const toml::path input_path{"input"};
  const auto input = root[input_path];
  if (input.is_table()) {
    const toml::table& input_table = *input.as_table();
    values.keyboard_gamepad_map = Read<std::string>(
        input_table, "input", "keyboard_gamepad_map", "string",
        std::string{kDefaultKeyboardGamepadMap});
    // Defaults come from Values itself (single source of truth).
    values.mouse_look =
        Read<bool>(input_table, "input", "mouse_look", "boolean",
                   values.mouse_look);
    values.mouse_look_scale = static_cast<int32_t>(Read<int64_t>(
        input_table, "input", "mouse_look_scale", "integer",
        values.mouse_look_scale));
  }
  const toml::path patches_path{"patches"};
  const auto patches = root[patches_path];
  if (patches.is_table()) {
    const toml::table& patches_table = *patches.as_table();
    values.fps_60 = Read<bool>(patches_table, "patches", "fps_60", "boolean",
                               values.fps_60);
    values.unlock_website = Read<bool>(patches_table, "patches",
                                       "unlock_website", "boolean",
                                       values.unlock_website);
    values.unlock_ce = Read<bool>(patches_table, "patches", "unlock_ce",
                                  "boolean", values.unlock_ce);
  }
  const toml::path remote_path{"remote"};
  const auto remote = root[remote_path];
  if (remote.is_table()) {
    const toml::table& remote_table = *remote.as_table();
    values.remote_enabled =
        Read<bool>(remote_table, "remote", "enabled", "boolean",
                   values.remote_enabled);
    values.remote_host = Read<std::string>(
        remote_table, "remote", "host", "string", values.remote_host);
    values.remote_port = static_cast<int32_t>(Read<int64_t>(
        remote_table, "remote", "port", "integer", values.remote_port));
    values.remote_token = Read<std::string>(
        remote_table, "remote", "token", "string", values.remote_token);
  }
  const toml::path perf_path{"perf"};
  const auto perf = root[perf_path];
  if (perf.is_table()) {
    const toml::table& perf_table = *perf.as_table();
    values.hotfunc_yield_every = static_cast<int32_t>(Read<int64_t>(
        perf_table, "perf", "hotfunc_yield_every", "integer",
        values.hotfunc_yield_every));
  }

  g_values = values;
  REXSYS_INFO("[fable2-config] loaded {} (config_version={})", path.string(),
              values.config_version);
  return true;
}

const Values& Get() { return g_values; }

const char* DefaultTemplate() { return kTemplate; }

}  // namespace fable2::config
