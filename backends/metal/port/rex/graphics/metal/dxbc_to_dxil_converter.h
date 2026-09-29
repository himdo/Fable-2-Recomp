/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license.                                          *
 * See backends/metal/licenses/XeniOS-LICENSE for the full terms.             *
 ******************************************************************************
 */

/**
 * DXBC to DXIL converter wrapper for Metal backend.
 *
 * Converts DXBC to DXIL using the in-process dxilconv library (no CLI spawn).
 */

#ifndef DXBC_TO_DXIL_CONVERTER_H_
#define DXBC_TO_DXIL_CONVERTER_H_

#include <cstdint>
#include <string>
#include <vector>

struct IDxbcConverter;

namespace rex {
namespace graphics {
namespace metal {

class DxbcToDxilConverter {
 public:
  static constexpr uint32_t kCacheVersion = 1;

  DxbcToDxilConverter();
  ~DxbcToDxilConverter();

  // Initialize the converter (ensure dxilconv is available).
  bool Initialize();

  // Convert DXBC bytecode to DXIL bytecode
  // Returns true on success, false on failure
  bool Convert(const std::vector<uint8_t>& dxbc_data,
               std::vector<uint8_t>& dxil_data_out,
               std::string* error_message = nullptr);

  // Check if the converter is available
  bool IsAvailable() const { return is_available_; }
  uint64_t GetOptionsHash() const;

 private:
  bool is_available_ = false;
  std::wstring extra_options_;

  // Lazily created per-thread converter instance.
  IDxbcConverter* GetThreadConverter(std::string* error_message);

  // Write vector to file (for debug dumps).
  bool WriteFile(const std::string& path, const std::vector<uint8_t>& data);
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // DXBC_TO_DXIL_CONVERTER_H_
