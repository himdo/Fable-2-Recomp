// Finds where prompt textures sit in guest memory: one pass over memory for
// any number of textures, trying only 4 KB aligned addresses because Xenos
// texture base addresses are 4 KB aligned.
#ifndef FABLE2_CORE_KEYCAP_TEXTURE_SCAN_H
#define FABLE2_CORE_KEYCAP_TEXTURE_SCAN_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fable2::keycap {

struct ScanTarget {
  std::vector<uint8_t> bytes;  // the exact bytes to find
  int found = 0;
  uint64_t offsets[4] = {0, 0, 0, 0};  // byte offsets from the start of memory
};

inline constexpr uint64_t kTextureAlignment = 0x1000;

// Offset of the first 8-byte window that isn't one repeated byte, so the quick
// check rejects blank pages.
inline size_t DistinctiveWindow(const std::vector<uint8_t>& bytes) {
  for (size_t k = 0; k + 8 <= bytes.size(); ++k)
    if (std::memcmp(bytes.data() + k, bytes.data() + k + 1, 7) != 0) return k;
  return 0;
}

// Records up to four places each target occurs. All-zero targets are skipped:
// they would match any blank page.
inline void FindAll(const uint8_t* memory, uint64_t size, std::vector<ScanTarget>& targets) {
  struct Search {
    ScanTarget* target;
    size_t key;
  };
  std::vector<Search> searches;
  for (auto& t : targets) {
    t.found = 0;
    const bool blank = std::all_of(t.bytes.begin(), t.bytes.end(), [](uint8_t v) { return v == 0; });
    if (t.bytes.size() >= 8 && !blank) searches.push_back({&t, DistinctiveWindow(t.bytes)});
  }
  for (uint64_t off = 0; off < size; off += kTextureAlignment) {
    for (auto& [t, key] : searches) {
      const auto& b = t->bytes;
      if (t->found == 4 || off + b.size() > size) continue;
      if (std::memcmp(memory + off + key, b.data() + key, 8) != 0) continue;
      if (std::memcmp(memory + off, b.data(), b.size()) == 0) t->offsets[t->found++] = off;
    }
  }
}

}  // namespace fable2::keycap

#endif  // FABLE2_CORE_KEYCAP_TEXTURE_SCAN_H
