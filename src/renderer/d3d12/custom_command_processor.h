// custom_command_processor.h
//
// The custom renderer's guest-facing command processor.
//
// CustomCommandProcessor IS the go-to renderer: CustomGraphicsSystem returns an
// instance of THIS class, and the guest's ring-buffer dispatches into it. It
// reuses the built-in D3D12 rendering engine (rex::graphics::d3d12::
// D3D12CommandProcessor) for the heavy lifting - Xenos->DXBC->DXIL shader
// translation, shared memory, texture/render-target caches, primitive
// processing, the pipeline cache, and the present path - and layers the custom
// renderer's behavior (progress HUD, cvars, and CPU fast paths) on top.
//
// M6 (CPU fast path):
//  - The base PM4 dispatch is replaced by fast_command_processor.cpp (batched
//    register writes).
//  - IssueDraw implements a "repeated static-state draw" fast path: when a draw
//    uses the same D3D state as the previous draw (same shaders, primitive
//    processing result, depth/color control, render targets, viewport and
//    textures), the expensive per-draw state setup (shader modification,
//    render-target update, shader translation, ConfigurePipeline, texture
//    requests, fixed-function state, system constants) is skipped and the draw
//    is re-issued directly. Only the per-draw constant buffers (e.g. the model
//    view projection) are refreshed. The built-in renderer does not use this
//    path, so it remains the stock, unmodified implementation.
#pragma once

#include <cstdint>
#include <unordered_set>

#include <rex/graphics/command_processor.h>
#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/d3d12/graphics_system.h>
#include <rex/graphics/primitive_processor.h>
#include <rex/graphics/xenos.h>

namespace rex::system {
class KernelState;
}

namespace customrenderer {
namespace d3d12 {

class CustomCommandProcessor : public rex::graphics::d3d12::D3D12CommandProcessor {
 public:
  CustomCommandProcessor(rex::graphics::d3d12::D3D12GraphicsSystem* graphics_system,
                         rex::system::KernelState* kernel_state);
  ~CustomCommandProcessor() override;

  bool SetupContext() override;
  void ShutdownContext() override;
  void WriteRegister(uint32_t index, uint32_t value) override;
  rex::graphics::Shader* LoadShader(rex::graphics::xenos::ShaderType shader_type,
                                    uint32_t guest_address, const uint32_t* host_address,
                                    uint32_t dword_count) override;
  bool IssueDraw(rex::graphics::xenos::PrimitiveType prim_type, uint32_t index_count,
                 rex::graphics::CommandProcessor::IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override;
  bool IssueCopy() override;
  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height) override;

 private:
  // Fingerprint of the D3D state a draw establishes. When a draw's fingerprint
  // matches the previous draw's, the D3D pipeline / index buffer / topology /
  // viewport / texture bindings are already in place and only the per-draw
  // constant buffers need refreshing before re-issuing the draw. The float
  // constant (MVP) values are intentionally excluded: they are expected to
  // change per draw, and only the cbuffer contents (not the D3D state) differ.
  struct DrawFingerprint {
    uint64_t vertex_shader = 0;
    uint64_t pixel_shader = 0;
    // The index buffer base/count/length are deliberately NOT fingerprinted: they
    // vary per mesh, and the fast path re-issues the draw with the *current*
    // index buffer (read from the draw's own parameters). Only the index format
    // and endianness are captured, since those affect the D3D pipeline.
    uint32_t ibi_format = 0;
    uint32_t ibi_endianness = 0;
    uint32_t regs[32] = {};
    // The texture IDENTITY (base_address + format, and size) for each USED
    // texture slot, fingerprinted to detect a texture change. The fast path
    // skips RequestTextures (it relies on the current shared SRV heap), so a
    // draw is only re-issuable when its bound textures are identical to the
    // establishing draw's. We fingerprint only the identity fields (fetch
    // constant dword_1: format+endianness+base_address, and dword_2: size) for
    // each used slot — NOT the full 6-dword fetch constant, because the sampler
    // state (LOD bias, mip min/max level, filters, swizzle) varies per draw and
    // would break the match every time (measured 0% hit rate). Without any
    // texture fingerprint, consecutive same-pipeline draws that use *different*
    // textures (exactly what UI/menus do) would draw with the wrong texture.
    uint32_t tex_id[64] = {};
    bool operator==(const DrawFingerprint&) const = default;
    // A scrolling/streaming texture's base_address drifts by a few 4 KB pages
    // per frame. The fast path skips RequestTextures, so a base_address within
    // this tolerance re-issues with a *slightly* stale texture page. A tiny
    // (a few-page) tolerance keeps the 3D scrolling effect's offset subtle and
    // never visible, while a genuinely different texture (a UI glyph, or a
    // splash-screen / montage scene whose base_address is far away) exceeds it
    // and breaks the match, falling through to the full built-in draw (which
    // calls RequestTextures). A larger tolerance was measured to visibly smear
    // the Fable II title screen (3 different scenes shown side by side).
    static constexpr uint32_t kBasePageTolerance = 2;
    // Full match: shaders + state regs + index format exact, and the per-used
    // texture identity (format, size exact; base_address within tolerance).
    bool Matches(const DrawFingerprint& o) const {
      if (vertex_shader != o.vertex_shader || pixel_shader != o.pixel_shader) {
        return false;
      }
      if (ibi_format != o.ibi_format || ibi_endianness != o.ibi_endianness) {
        return false;
      }
      for (int i = 0; i < 32; i++) {
        if (regs[i] != o.regs[i]) {
          return false;
        }
      }
      for (int slot = 0; slot < 32; slot++) {
        const uint32_t a1 = tex_id[slot * 2 + 0];  // dword_1: format|endianness|base_address
        const uint32_t b1 = o.tex_id[slot * 2 + 0];
        const uint32_t a2 = tex_id[slot * 2 + 1];  // dword_2: size
        const uint32_t b2 = o.tex_id[slot * 2 + 1];
        if (a1 == 0 && b1 == 0) {
          continue;  // both unused
        }
        if (a2 != b2) {
          return false;  // size must match exactly
        }
        if ((a1 & 0x0FFFu) != (b1 & 0x0FFFu)) {
          return false;  // format + endianness + flags must match
        }
        const uint32_t ab = a1 >> 12;  // base_address (page index)
        const uint32_t bb = b1 >> 12;
        const uint32_t diff = ab > bb ? ab - bb : bb - ab;
        if (diff > kBasePageTolerance) {
          return false;  // a different texture
        }
      }
      return true;
    }
    // FNV-1a over the whole fingerprint; used only by the M6 diagnostic that
    // counts how many *distinct* pipelines the slow-miss draws cycle through
    // (small => a multi-entry pipeline cache would raise the fast-path hit rate).
    uint64_t Hash() const {
      uint64_t h = 1469598103934665603ULL;
      auto mix = [&h](uint64_t v) {
        for (int b = 0; b < 8; b++) {
          h ^= (v >> (8 * b)) & 0xFF;
          h *= 1099511628211ULL;
        }
      };
      mix(vertex_shader);
      mix(pixel_shader);
      mix(ibi_format);
      mix(ibi_endianness);
      for (int i = 0; i < 32; i++) mix(regs[i]);
      for (int i = 0; i < 64; i++) mix(tex_id[i]);
      return h;
    }
  };

  // M6 (CPU fast path) draw fast-path state. Single-entry: the fast path fires
  // only when the current draw's fingerprint (shaders + 26 state registers +
  // 192 texture-fetch dwords) EXACTLY matches the previous draw's fingerprint.
  // This is transitivity-correct: the D3D pipeline + shared SRV heap +
  // viewport + fixed-function + system-constants are all set by the last base
  // draw (the "establishing draw"), and a matching fingerprint guarantees the
  // current draw has identical pipeline, textures, and dynamic state. A
  // multi-entry table was tried but is incorrect: the shared SRV heap is a
  // single region set by the *last* base draw, so a stale table entry can match
  // a draw whose textures no longer match the SRV heap (wrong textures).
  bool m6_fast_draw_ = false;
  bool fast_path_valid_ = false;
  struct FastDrawEntry {
    DrawFingerprint fp{};
    bool valid = false;
    void* pipeline_handle = nullptr;
    ID3D12RootSignature* root_signature = nullptr;
    uint32_t used_texture_mask = 0;
    rex::graphics::PrimitiveProcessor::ProcessingResult ppr{};
  };
  FastDrawEntry fast_entry_{};
  // Rebind the D3D state (pipeline, root signature, textures, fixed function,
  // system constants) to a cached entry. A no-op when the entry is already the
  // current pipeline (the cheap common case). Returns false if the rebind cannot
  // proceed (fall back to the full base draw).
  bool MfpRebind(const FastDrawEntry& e);
  // The fast re-issue: refresh cbuffers, ensure vertex buffers resident, set
  // topology, issue the draw. Assumes MfpRebind bound the entry's D3D state.
  bool MfpFastReissue(rex::graphics::xenos::PrimitiveType prim_type, uint32_t index_count,
                      rex::graphics::CommandProcessor::IndexBufferInfo* index_buffer_info,
                      const FastDrawEntry& e);
  uint64_t m6_draw_fast_hits_ = 0;
  uint64_t m6_draw_fast_misses_ = 0;
  // Diagnostic fingerprint-match breakdown (accumulated per [m6] 5s window):
  // how many draws match the cached fingerprint (fp_match), of those how many
  // are indexed (re-issuable by the fast path) vs non-indexed (currently
  // rejected), plus how many do not match at all. This separates "fingerprint
  // mismatch" misses from "matches but not re-issuable" misses.
  uint64_t m6_diag_match_ = 0;
  uint64_t m6_diag_match_idx_ = 0;
  uint64_t m6_diag_nomatch_ = 0;
  // Fast-path sub-op timing (gated on m6_breakdown_), accumulated per successful
  // fast-path hit: total time inside TryFastDraw and the UpdateBindings share of
  // it. Isolates the per-fast-draw cost to find the dominant component to cut.
  uint64_t m6_fast_total_ns = 0, m6_fast_total_n = 0;
  uint64_t m6_fast_bind_ns = 0, m6_fast_bind_n = 0;
  // Diagnostic: count of fast-path hits where the vertex-index remapping
  // registers (VGT_INDX_OFFSET / MIN / MAX) changed since the cached state was
  // established. Nonzero means the cached system constants are stale for those
  // draws. Also a counter used to gate the per-draw register dump.
  uint64_t m6_diag_n_ = 0;
  uint64_t m6_vgt_indx_mismatch_ = 0;
  // Cumulative draw counter used to gate the heavy-gameplay fingerprint
  // diagnostic at a fixed point deep into sustained heavy gameplay.
  uint64_t m6_diag_total_ = 0;
  // M6 diagnostic (gated on m6_draw_profile_): set of distinct slow-miss
  // pipeline fingerprints seen in the current 5s window. Its size tells us
  // whether the misses cycle through a small pipeline set (multi-entry cache
  // would help) or are all distinct (they cannot be cached cheaply).
  std::unordered_set<uint64_t> m6_miss_fp_hashes_;

  // M6 (CPU fast path) self-profiling on the GPU-Commands thread.
  void M6MaybeSample();

  void ComputeDrawFingerprint(rex::graphics::xenos::PrimitiveType prim_type, uint32_t index_count,
                              rex::graphics::CommandProcessor::IndexBufferInfo* index_buffer_info,
                              DrawFingerprint* out) const;
  // Re-issues the cached draw (same D3D state as the previous draw) after
  // refreshing the per-draw constant buffers. Returns false if the state is not
  // actually re-issuable (fall back to the full slow path).
  bool TryFastDraw(rex::graphics::xenos::PrimitiveType prim_type, uint32_t index_count,
                   rex::graphics::CommandProcessor::IndexBufferInfo* index_buffer_info);

  uint64_t swap_count_ = 0;
  uint64_t m6_qpc_freq_ = 0;
  int64_t m6_last_qpc_ = 0;
  uint64_t m6_last_cpu100ns_ = 0;
  bool m6_breakdown_ = false;
  uint64_t m6_draw_n = 0, m6_copy_n = 0, m6_swap_n = 0, m6_write_n = 0, m6_shader_n = 0;
  uint64_t m6_draw_ns = 0, m6_copy_ns = 0, m6_swap_ns = 0, m6_write_ns = 0, m6_shader_ns = 0;
};

}  // namespace d3d12
}  // namespace customrenderer
