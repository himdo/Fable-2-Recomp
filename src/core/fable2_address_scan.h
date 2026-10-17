// fable2_address_scan.h - see fable2_address_scan.cpp. The mid-asm hook
// fable2_hook_address_scan_skip (0x82B4DE50, fable_2_manifest.toml) needs the
// guest memory manager; the app hands it over once the image is loaded.
#pragma once

namespace rex::memory {
class Memory;
}

namespace fable2::addressscan {

void SetMemory(rex::memory::Memory* memory);

}  // namespace fable2::addressscan
