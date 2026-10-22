// progress_checklist.h - Custom renderer: shared (backend-agnostic) feature
// checklist + counters. See plans/custom-d3d12-renderer.md.
//
// This is the single source of truth for "what the renderer implements and
// what it has seen so far". It is plain data (no D3D12/Vulkan, no SDK
// dependencies) so a future Vulkan renderer reuses it unchanged. Each backend
// renders it in its own way: the D3D12 renderer feeds the window title and
// logs/renderer-custom.log (progress_report), and the app's ImGui overlay
// reads the text snapshot through the `rex_custom_renderer_snapshot` plugin
// export.
//
// Thread-safety: counters are atomics; the checklist table itself is written
// once in Init() before any guest code runs and only read afterwards.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace customrenderer {
namespace progress {

struct Feature {
  const char* name = nullptr;       // short machine-friendly name
  const char* description = nullptr; // human description (HUD/log)
  const char* milestone = nullptr;  // plan milestone where it becomes real
  bool implemented = false;         // this build does real work (vs. no-op)
  std::atomic<uint64_t> count{0};        // times seen/issued
  std::atomic<bool> first_logged{false}; // one-shot "first time" log marker
};

constexpr int kMaxFeatures = 32;

inline Feature (&Features())[kMaxFeatures] {
  static Feature features[kMaxFeatures];
  return features;
}

inline int& FeatureCount() {
  static int count = 0;
  return count;
}

// Register a feature. Returns its index (or -1 if the table is full).
inline int AddFeature(const char* name, const char* description,
                      const char* milestone, bool implemented) {
  int& count = FeatureCount();
  if (count >= kMaxFeatures) {
    return -1;
  }
  Feature& f = Features()[count];
  f.name = name;
  f.description = description;
  f.milestone = milestone;
  f.implemented = implemented;
  f.count.store(0, std::memory_order_relaxed);
  f.first_logged.store(false, std::memory_order_relaxed);
  return count++;
}

inline Feature* Find(int index) {
  if (index < 0 || index >= FeatureCount()) {
    return nullptr;
  }
  return &Features()[index];
}

// Bump a counter by index. Never asserts: a bad index is a benign no-op.
inline void Bump(int index, uint64_t n = 1) {
  if (index < 0 || index >= FeatureCount()) {
    return;
  }
  Features()[index].count.fetch_add(n, std::memory_order_relaxed);
}

inline uint32_t PercentComplete() {
  int total = FeatureCount();
  if (total <= 0) {
    return 0;
  }
  int done = 0;
  for (int i = 0; i < total; ++i) {
    if (Features()[i].implemented) {
      ++done;
    }
  }
  return uint32_t(uint64_t(done) * 100 / uint64_t(total));
}

// Register every feature this build knows about. Called once from the plugin
// entry point before anything else. The (name, implemented) pairs are the
// per-milestone progress report.
inline void Init() {
  FeatureCount() = 0;
  // --- Already real (base Xenos command-stream layer + our wiring) --------
  AddFeature("mmio_ring", "GPU ring buffer + MMIO register file", "M0", true);
  AddFeature("interrupts", "vblank/GPU interrupt dispatch", "M0", true);
  AddFeature("register_stream", "PM4 register writes (state decode)", "M0", true);
  // --- Counted but no-op until the later milestone -------------------------
  AddFeature("swap", "IssueSwap: present the guest front buffer", "M1", false);
  AddFeature("textures", "texture cache: guest memory -> GPU textures", "M2", false);
  AddFeature("render_targets", "render target cache + bins", "M3", false);
  AddFeature("copies", "IssueCopy: blits / copy engine", "M3", false);
  AddFeature("clears", "viewport/depth/stencil clears", "M3", false);
  AddFeature("shaders", "Xenos ALU -> DXBC -> DXIL shader path", "M4", false);
  AddFeature("draws", "IssueDraw: vertex fetch + draws", "M4", false);
  AddFeature("gamma", "gamma ramp (DC LUT)", "M7", false);
}

inline void FormatHumanCount(uint64_t v, char* out, int size) {
  if (v >= 1000000000ull) {
    std::snprintf(out, size, "%.2fB", double(v) / 1e9);
  } else if (v >= 1000000ull) {
    std::snprintf(out, size, "%.1fM", double(v) / 1e6);
  } else if (v >= 10000ull) {
    std::snprintf(out, size, "%.1fK", double(v) / 1e3);
  } else {
    std::snprintf(out, size, "%llu", (unsigned long long)v);
  }
}

// Multi-line text snapshot for the title/overlay/log.
inline void FormatSnapshot(char* buf, int size) {
  int offset = 0;
  offset += std::snprintf(buf + offset, size_t(size) > size_t(offset) ? size - offset : 0,
                          "Custom D3D12 renderer (M0 boot) - %d%% implemented",
                          int(PercentComplete()));
  offset += std::snprintf(buf + offset, size_t(size) > size_t(offset) ? size - offset : 0,
                          "\n  register writes: ");
  {
    char tmp[16];
    FormatHumanCount(Find(2) ? Features()[2].count.load(std::memory_order_relaxed) : 0,
                     tmp, sizeof(tmp));
    offset += std::snprintf(buf + offset,
                            size_t(size) > size_t(offset) ? size - offset : 0, "%s", tmp);
  }
  offset += std::snprintf(buf + offset, size_t(size) > size_t(offset) ? size - offset : 0,
                          "  swaps: ");
  {
    char tmp[16];
    FormatHumanCount(Find(3) ? Features()[3].count.load(std::memory_order_relaxed) : 0,
                     tmp, sizeof(tmp));
    offset += std::snprintf(buf + offset,
                            size_t(size) > size_t(offset) ? size - offset : 0, "%s", tmp);
  }
  for (int i = 4; i < FeatureCount() && offset < size - 4; ++i) {
    const Feature& f = Features()[i];
    if (f.count.load(std::memory_order_relaxed) == 0 && !f.implemented) {
      continue;  // only list features that have actually been seen
    }
    char tmp[16];
    FormatHumanCount(f.count.load(std::memory_order_relaxed), tmp, sizeof(tmp));
    offset += std::snprintf(
        buf + offset, size_t(size) > size_t(offset) ? size - offset : 0,
        "  %s: %s%s", f.name, tmp, f.implemented ? "" : " (no-op)");
  }
}

}  // namespace progress
}  // namespace customrenderer
