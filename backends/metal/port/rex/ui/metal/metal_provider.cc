/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#include "rex/ui/metal/metal_provider.h"

#include "third_party/metal-cpp/Metal/Metal.hpp"

#include "rex/logging.h"
#include "rex/ui/metal/metal_immediate_drawer.h"
#include "rex/ui/metal/metal_presenter.h"

namespace rex {
namespace ui {
namespace metal {

namespace {

void ConfigureMetalValidationEnvironment() {
  // Enable the Metal validation layer in Debug and Checked builds before any
  // Metal device is created.
#if !defined(NDEBUG) || defined(MTL_DEBUG_LAYER)
  setenv("METAL_DEVICE_WRAPPER_TYPE", "1", 1);
  setenv("METAL_DEBUG_ERROR_MODE", "assert", 1);
#endif
}

}  // namespace

bool MetalProvider::IsMetalAPIAvailable() {
  ConfigureMetalValidationEnvironment();
  MTL::Device* device = MTL::CreateSystemDefaultDevice();
  bool available = (device != nullptr);
  if (device) {
    device->release();
  }
  return available;
}

std::unique_ptr<MetalProvider> MetalProvider::Create() {
  auto provider = std::unique_ptr<MetalProvider>(new MetalProvider());
  if (!provider->Initialize()) {
    rex::FatalError(
        "Unable to Initialize Metal Graphics Subsystem.\n"
        "\n"
        "Ensure you have the latest OS your device supports\n");
    return nullptr;
  }
  return provider;
}

MetalProvider::MetalProvider() = default;

MetalProvider::~MetalProvider() {
  if (command_queue_) {
    command_queue_->release();
  }
  if (device_) {
    device_->release();
  }
}

bool MetalProvider::Initialize() {
  ConfigureMetalValidationEnvironment();
  device_ = MTL::CreateSystemDefaultDevice();
  if (!device_) {
    REXLOG_ERROR("Failed to create Metal device");
    return false;
  }
  REXLOG_INFO("Metal device created: {}", device_->name()->utf8String());
  command_queue_ = device_->newCommandQueue();
  if (!command_queue_) {
    REXLOG_ERROR("Failed to create Metal Command Queue");
    return false;
  }

#if !defined(NDEBUG) || defined(MTL_DEBUG_LAYER)
  REXLOG_INFO("Metal validation layer enabled");
#endif
  return true;
}

std::unique_ptr<Presenter> MetalProvider::CreatePresenter(
    Presenter::HostGpuLossCallback host_gpu_loss_callback) {
  auto presenter =
      std::make_unique<MetalPresenter>(this, host_gpu_loss_callback);
  if (!presenter->Initialize()) {
    REXLOG_ERROR("Metal presenter failed to initialize");
    return nullptr;
  }
  return std::move(presenter);
}

std::unique_ptr<ImmediateDrawer> MetalProvider::CreateImmediateDrawer() {
  auto drawer = std::make_unique<MetalImmediateDrawer>(this);
  if (!drawer->Initialize()) {
    REXLOG_ERROR("Metal immediate drawer failed to initialize");
    return nullptr;
  }
  return std::move(drawer);
}

}  // namespace metal
}  // namespace ui
}  // namespace xe
