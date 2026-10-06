// fable_2 - ReXGlue Recompiled Project
//
// GPU backend selection for the dual-backend rexgpu-xenos plugin (D3D12 and
// Vulkan in one DLL, built by tools/build_runtime_sdk.cmd). Shared by
// Fable2App::OnPreSetup and tests/gpu/test_gpu_backends.cpp.

#pragma once

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>

#include <rex/logging.h>
#include <rex/system/gpu_plugin.h>

namespace fable2::gpu {

// "d3d12" or "vulkan", case-insensitive. "any" is accepted as d3d12 (which
// already falls back to Vulkan); anything else warns and uses d3d12.
inline std::string NormalizeBackend(std::string backend) {
  std::ranges::transform(backend, backend.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (backend == "vulkan" || backend == "d3d12") return backend;
  if (backend != "any") {
    REXSYS_WARN("[gpu] unknown gpu_backend '{}'; using d3d12", backend);
  }
  return "d3d12";
}

struct Started {
  std::unique_ptr<rex::system::IGraphicsSystem> graphics;
  std::string backend;  // the backend that started; empty if neither did
};

// Load `plugin` with `requested` ("d3d12" or "vulkan") and bring up its device
// and presenter now. IGraphicsSystem::SetupPresentation is idempotent, so
// ReXApp's own call afterwards is a no-op. A backend that loads but cannot
// start (no Vulkan driver, no D3D12 feature level 11_0 adapter) falls back to
// the other one instead of failing the launch.
inline Started Start(std::string_view plugin, std::string_view requested,
                     rex::ui::WindowedAppContext* app_context) {
  const auto start = [&](std::string_view backend) {
    auto graphics = rex::system::LoadGpuPlugin(plugin, backend);
    if (graphics && XFAILED(graphics->SetupPresentation(app_context))) {
      REXSYS_WARN("[gpu] {} backend failed to initialize", backend);
      graphics.reset();
    }
    return graphics;
  };
  Started result{start(requested), std::string(requested)};
  if (!result.graphics) {
    const char* fallback = requested == "vulkan" ? "d3d12" : "vulkan";
    REXSYS_WARN("[gpu] falling back from {} to {}", requested, fallback);
    result = {start(fallback), fallback};
  }
  if (!result.graphics) result.backend.clear();
  return result;
}

}  // namespace fable2::gpu
