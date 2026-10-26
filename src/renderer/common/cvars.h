// cvars.h - Custom renderer: shared cvar declarations.
//
// Definitions live in cvars.cpp (registered with the shared SDK cvar
// registry; other code can reach them by name, e.g. the app seeds/reads
// "custom_renderer_hud" via rex::cvar::GetFlagByName). Inside this plugin,
// translation units may include this header and use REXCVAR_GET directly.

#pragma once

#include <rex/cvar.h>

REXCVAR_DECLARE(bool, custom_renderer_hud);
REXCVAR_DECLARE(bool, custom_renderer_title);
REXCVAR_DECLARE(int32_t, custom_renderer_log_interval);
REXCVAR_DECLARE(int32_t, custom_renderer_clear_color);
