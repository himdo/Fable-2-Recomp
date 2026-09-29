#pragma once

#include <rex/cvar.h>

namespace fable2 {
// Call after the selected graphics plugin has registered its flags. Explicit
// SDK config, environment, command-line and runtime settings retain priority.
inline bool SeedHeroDogReadback(bool metal, bool enabled) {
  const auto name = metal ? "metal_fable_morph_readback"
                          : "readback_resolve_force_addresses";
  if (rex::cvar::GetFlagSource(name) != rex::cvar::Source::kDefault) return true;
  return rex::cvar::SetFlagByName(
      name, metal ? (enabled ? "true" : "false")
                  : (enabled ? "0x12704000" : ""));
}
}  // namespace fable2
