// Disable MSAA: strong-symbol override of InitRenderSettings_8238DE58, the
// render settings setup (renamed from sub_8238DE58 in fable_2_manifest.toml).
//
// The original stores the main render setup into the object in r3 (kept in r31):
//   0x8238DF3C  li r9,2      -> stw r9,156(r31)   MSAA mode (2 = 2x)
//   0x8238DF4C  li r10,720   -> stw r10,152(r31)  render height
//   0x8238DF58  li r11,1120  -> stw r11,148(r31)  render width (960 in 4:3)
// The Xenia "Disable MSAA" patch for this executable changes the first to
// li r9,1 (be8 0x8238DF3F = 0x01). Code is recompiled, so the same result is
// made here: run the original, then store mode 1 into the same field.
// The cvar disable_msaa (off by default), seeded from [patches] disable_msaa in
// fable2_config.toml and listed in the F4 settings menu, decides it.
#include "fable_2_pch.h"

#include <rex/cvar.h>
#include <rex/logging/macros.h>

REXCVAR_DECLARE(bool, disable_msaa);

extern "C" void __imp__InitRenderSettings_8238DE58(PPCContext& ctx, uint8_t* base);

extern "C" void InitRenderSettings_8238DE58(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t settings = ctx.r3.u32;
  __imp__InitRenderSettings_8238DE58(ctx, base);
  const uint32_t mode = REX_LOAD_U32(settings + 156);
  const uint32_t width = REX_LOAD_U32(settings + 148);
  const uint32_t height = REX_LOAD_U32(settings + 152);
  if (REXCVAR_GET(disable_msaa)) {
    REX_STORE_U32(settings + 156, 1u);
  }
  static int logged = 0;
  if (logged++ < 3)
    REXSYS_INFO("[msaa] render setup at 0x{:08X}: {}x{}, MSAA mode {} -> {}", settings, width,
              height, mode, REX_LOAD_U32(settings + 156));
}
