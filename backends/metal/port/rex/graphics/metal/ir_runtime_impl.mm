/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

// Single compilation unit for Metal IR Converter Runtime implementation
#include "third_party/metal-cpp/Metal/Metal.hpp"

#ifndef IR_RUNTIME_METALCPP
#define IR_RUNTIME_METALCPP
#endif
#define IR_PRIVATE_IMPLEMENTATION  // Generate the implementation exactly once

// Use the actual runtime header with absolute path
#include "third_party/metal-shader-converter/include/metal_irconverter_runtime.h"
