// Opt-in GPU backend test: needs real GPU drivers, so it is not part of
// tests/run_native_tests.cmd. Run via tests/run_gpu_tests.cmd.
//
// Loads the dual-backend rexgpu-xenos plugin through fable2::gpu::Start (the
// same path Fable2App::OnPreSetup takes) and brings up a device + offscreen
// presenter, so the SDK logs which adapter each backend selected.
//
//   test_gpu_backends d3d12      expect D3D12 to start
//   test_gpu_backends vulkan     expect Vulkan to start
//   test_gpu_backends nodriver   hide every Vulkan driver from the loader;
//                                expect vulkan to fall back to D3D12

#include <cstdio>
#include <string>
#include <string_view>

#include <windows.h>

#include <rex/logging.h>

#include "gpu_backend.h"

// wmain, like the game's wWinMain: rex::filesystem::GetExecutableFolder uses
// _get_wpgmptr, which fast-fails in a narrow-main process.
int wmain(int argc, wchar_t** argv) {
  std::string arg;
  for (const wchar_t* c = argc > 1 ? argv[1] : L""; *c; ++c) {
    arg += static_cast<char>(*c);
  }
  const std::string_view mode = arg;
  std::string requested(mode);
  std::string expected(mode);
  if (mode == "nodriver") {
    // The Vulkan loader reads these when the instance is created; a driver
    // list naming no real file leaves it with no ICDs at all.
    SetEnvironmentVariableA("VK_DRIVER_FILES", "C:\\fable2-no-such-driver.json");
    SetEnvironmentVariableA("VK_ICD_FILENAMES", "C:\\fable2-no-such-driver.json");
    requested = "vulkan";
    expected = "d3d12";
  } else if (mode != "d3d12" && mode != "vulkan") {
    std::fprintf(stderr, "usage: test_gpu_backends d3d12|vulkan|nodriver\n");
    return 2;
  }

  rex::LogConfig log;
  log.log_to_console = true;
  rex::InitLogging(log);

  fable2::gpu::Started gpu = fable2::gpu::Start("xenos", requested, nullptr);
  const bool ok = gpu.graphics && gpu.backend == expected;
  std::printf("[%s] %s: requested=%s running=%s (expected %s)\n",
              ok ? "PASS" : "FAIL", std::string(mode).c_str(), requested.c_str(),
              gpu.backend.empty() ? "none" : gpu.backend.c_str(), expected.c_str());
  std::fflush(stdout);
  // Skip static/plugin teardown: plugins stay loaded for the process lifetime
  // and the presenter is never connected to a window here.
  std::_Exit(ok ? 0 : 1);
}
