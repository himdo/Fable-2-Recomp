#include <rex/system/gpu_plugin.h>
#include <rex/graphics/metal/metal_graphics_system.h>
#include <rex/logging.h>
extern "C" REX_GPU_PLUGIN_EXPORT uint32_t rex_gpu_abi_version() {
  return rex::system::kGpuPluginAbiVersion;
}
extern "C" REX_GPU_PLUGIN_EXPORT rex::system::IGraphicsSystem* rex_gpu_create(
    uint32_t abi, const rex::system::GpuCreateInfo* info) {
  if (abi != rex::system::kGpuPluginAbiVersion || !info ||
      info->struct_size < sizeof(rex::system::GpuCreateInfo)) return nullptr;
  std::string_view backend = info->backend ? info->backend : "any";
  if (backend != "any" && backend != "metal") return nullptr;
  REXLOG_INFO("XeniOS Metal renderer, ReXGlue plugin");
  return new rex::graphics::metal::MetalGraphicsSystem();
}
