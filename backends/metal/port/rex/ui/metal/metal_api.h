/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

#ifndef XENIA_UI_METAL_METAL_API_H_
#define XENIA_UI_METAL_METAL_API_H_

// metal-cpp C++ wrappers for Metal and Foundation.
#include "third_party/metal-cpp/Metal/MTLDevice.hpp"
#include "third_party/metal-cpp/Metal/Metal.hpp"

#ifdef METAL_SHADER_CONVERTER_AVAILABLE
#ifndef IR_RUNTIME_METALCPP
#define IR_RUNTIME_METALCPP
#endif
#include "third_party/metal-shader-converter/include/metal_irconverter/metal_irconverter.h"
#endif  // METAL_SHADER_CONVERTER_AVAILABLE

#endif
