// cvars.cpp - Custom renderer: cvar definitions.
// See src/renderer/common/cvars.h.
//
// NOTE: must be global scope (not an anonymous namespace): REXCVAR_DECLARE in
// cvars.h references the generated FLAGS_*_storage_() symbols across
// translation units.

#include "cvars.h"

// Show the progress overlay (app-side ImGui dialog reading the
// rex_custom_renderer_snapshot export). On until the renderer reaches M5;
// defaulting on is intentional so early milestones are visible.
REXCVAR_DEFINE_BOOL(custom_renderer_hud, true, "GPU/Custom",
                    "Show the custom renderer progress overlay (ImGui).")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Mirror the progress into the host window title (2 Hz).
REXCVAR_DEFINE_BOOL(custom_renderer_title, true, "GPU/Custom",
                    "Update the window title with the custom renderer progress.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Seconds between progress lines in logs/renderer-custom.log (0 = off).
REXCVAR_DEFINE_INT32(custom_renderer_log_interval, 2, "GPU/Custom",
                     "Seconds between custom renderer progress log lines (0 disables).")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// M3: front-buffer render-target clear color (0xRRGGBB, 0 = black). Used to
// verify the GPU present path before the guest's own clear op is implemented
// in M4 (which will supersede this).
REXCVAR_DEFINE_INT32(custom_renderer_clear_color, 0x000000, "GPU/Custom",
                     "Custom renderer front-buffer clear color (0xRRGGBB; 0 = black).")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
