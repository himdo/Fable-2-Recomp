#include "fable2_readback_config.h"
#include <cstdio>
#include <cstdlib>

REXCVAR_DEFINE_BOOL(metal_fable_morph_readback, false, "Test", "Metal readback");
REXCVAR_DEFINE_STRING(readback_resolve_force_addresses, "", "Test", "Other readback");

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "Failed at %d: %s\n", __LINE__, #x); std::abort(); } } while (0)

int main() {
  using namespace rex::cvar;
  for (bool metal : {false, true}) {
    const auto selected = metal ? "metal_fable_morph_readback" : "readback_resolve_force_addresses";
    const auto other = metal ? "readback_resolve_force_addresses" : "metal_fable_morph_readback";
    for (bool enabled : {false, true}) {
      ResetAllToDefaults();
      auto untouched = GetFlagByName(other);
      CHECK(fable2::SeedHeroDogReadback(metal, enabled));
      CHECK(GetFlagByName(selected) == (metal ? (enabled ? "true" : "false") : (enabled ? "0x12704000" : "")));
      CHECK(GetFlagByName(other) == untouched);
    }
    ResetAllToDefaults();
    const auto override_value = metal ? "false" : "0x12340000";
    CHECK(SetFlagFromCommandLine(selected, override_value));
    CHECK(fable2::SeedHeroDogReadback(metal, true));
    CHECK(GetFlagByName(selected) == override_value);
    CHECK(GetFlagSource(selected) == Source::kCommandLine);
    ResetAllToDefaults();
    CHECK(SetFlagByName(selected, override_value));
    CHECK(fable2::SeedHeroDogReadback(metal, true));
    CHECK(GetFlagByName(selected) == override_value);
  }
  std::puts("Readback backend selection, config on/off and explicit overrides passed");
}
