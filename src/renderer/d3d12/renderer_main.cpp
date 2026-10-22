// renderer_main.cpp - Custom D3D12 renderer (rexgpu-custom): plugin entry.
//
// Implements the GPU plugin ABI v1 (rex/system/gpu_plugin.h):
//   rex_gpu_abi_version() -> 1
//   rex_gpu_create(abi, info) -> CustomGraphicsSystem
//
// Plus one extra C export for the app side:
//   rex_custom_renderer_snapshot() -> latest multi-line progress text, read
//   by the app's ImGui overlay (src/diagnostics/fable2_custom_renderer_overlay.h).
//
// See plans/custom-d3d12-renderer.md.

#include <string_view>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/gpu_plugin.h>

#include "../common/progress_checklist.h"
#include "../common/progress_report.h"
#include "custom_graphics_system.h"

using namespace customrenderer::progress;  // progress checklist counters

// Plugin-side bookkeeping: the exe directory (for logs/) and the started
// report thread. The plugin is never unloaded (see gpu_plugin.h), so a
// process-lifetime static is safe; the report thread stops via atexit.
bool InitBookkeeping() {
  customrenderer::progress::Init();
  // The exe directory doubles as the content root for this app (self-
  // contained layout), and logs/ lives next to the exe.
  customrenderer::progress_report::Start(rex::filesystem::GetExecutableFolder());
  return true;
}

extern "C" REX_GPU_PLUGIN_EXPORT uint32_t rex_gpu_abi_version(void) {
  return rex::system::kGpuPluginAbiVersion;
}

extern "C" REX_GPU_PLUGIN_EXPORT rex::system::IGraphicsSystem* rex_gpu_create(
    uint32_t abi_version, const rex::system::GpuCreateInfo* info) {
  if (abi_version != rex::system::kGpuPluginAbiVersion) {
    REXSYS_ERROR("rexgpu-custom: host requested ABI {}, plugin is ABI {}", abi_version,
                 rex::system::kGpuPluginAbiVersion);
    return nullptr;
  }
  if (!info || info->struct_size < sizeof(rex::system::GpuCreateInfo)) {
    REXSYS_ERROR("rexgpu-custom: invalid GpuCreateInfo");
    return nullptr;
  }
  const std::string_view backend = info->backend ? info->backend : "any";
  if (backend != "any" && backend != "d3d12") {
    REXSYS_ERROR("rexgpu-custom: requested backend '{}' - only d3d12 exists (Vulkan is a "
                 "later milestone)",
                 std::string(backend));
    return nullptr;
  }

  InitBookkeeping();
  REXSYS_INFO(
      "[custom-renderer] plugin loaded: ABI {} - D3D12 no-op backend (M0 boot); "
      "progress via window title, logs/renderer-custom.log and the HUD overlay",
      int(abi_version));
  return new customrenderer::d3d12::CustomGraphicsSystem();
}

// Extra export (not part of the GPU plugin ABI): the app's ImGui overlay
// reads this for the on-screen progress report.
extern "C" REX_GPU_PLUGIN_EXPORT const char* rex_custom_renderer_snapshot(void) {
  return customrenderer::progress_report::GetSnapshotText();
}
