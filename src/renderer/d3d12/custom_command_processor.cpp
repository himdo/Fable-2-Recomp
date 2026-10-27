// custom_command_processor.cpp - The custom D3D12 renderer.
//
// CustomCommandProcessor delegates every render operation to the built-in
// D3D12CommandProcessor (the known-working renderer) and adds the custom
// renderer's progress-HUD accounting plus the M6 (CPU fast path) draw
// fast-path.
//
// M6 draw fast path:
//   A large fraction of a frame's draws share the same D3D state (same vertex
//   and pixel shader, same render targets, same viewport, same textures); only
//   the per-draw constant buffers (the model view projection, per-mesh) change.
//   The built-in D3D12CommandProcessor::IssueDraw recomputes the whole state
//   (shader modification, render-target update, shader translation, pipeline
//   configuration, texture requests, fixed-function state, system constants) on
//   every draw. This subclass detects the "same static state as the previous
//   draw" case via a cheap register fingerprint and, instead of re-running that
//   setup, refreshes only the per-draw constant buffers and re-issues the draw
//   with the already-bound pipeline / index buffer / topology / viewport /
//   textures. The built-in renderer never uses this path, so it stays the stock
//   implementation.
//
// M6 self-profiling: an in-process GetThreadTimes-based CPU sampler logs the
// GPU-Commands thread CPU every 5 seconds, and, when FABLE2_M6_BREAKDOWN=1,
// splits it into draw / copy / swap / write / shader plus the base-dispatch
// residual.
#include "custom_command_processor.h"

#include "../common/progress_checklist.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <windows.h>

#include <rex/logging.h>
#include <rex/math.h>

// Declared in fast_command_processor.cpp (the custom plugin's base dispatch);
// exposes the FABLE2_M6_DISPATCH_PROFILE=1 sub-op snapshot for the M6 profiler.
namespace rex::graphics {
void M6DispatchProfileSnap(uint64_t&, uint64_t&, uint64_t&, uint64_t&,
                           uint64_t&, uint64_t&, uint64_t&, uint64_t&);
}

namespace customrenderer {
namespace d3d12 {

using rex::graphics::CommandProcessor;
using rex::graphics::PrimitiveProcessor;
using rex::graphics::Shader;
using rex::graphics::d3d12::D3D12CommandProcessor;
using rex::graphics::d3d12::D3D12Shader;
using rex::graphics::xenos::FetchConstantType;
using rex::graphics::xenos::IndexFormat;
using rex::graphics::xenos::PrimitiveType;
using rex::graphics::xenos::xe_gpu_vertex_fetch_t;
namespace rg = rex::graphics;

namespace {
// Progress checklist indices (order fixed by progress::Init()).
constexpr int kFeatRegisterStream = 2;
constexpr int kFeatSwap = 3;
constexpr int kFeatCopies = 7;
constexpr int kFeatShaders = 9;
constexpr int kFeatDraws = 10;

inline uint64_t FileTime64(const FILETIME& ft) {
  return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// CPU time (user+kernel) of the calling thread, in 100ns units.
inline uint64_t ThisThreadCpu100ns() {
  FILETIME creation, exit, kernel, user;
  if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)) {
    return 0;
  }
  return FileTime64(kernel) + FileTime64(user);
}
}  // namespace

CustomCommandProcessor::CustomCommandProcessor(
    rex::graphics::d3d12::D3D12GraphicsSystem* graphics_system,
    rex::system::KernelState* kernel_state)
    : D3D12CommandProcessor(graphics_system, kernel_state) {}

CustomCommandProcessor::~CustomCommandProcessor() = default;

void CustomCommandProcessor::M6MaybeSample() {
  LARGE_INTEGER li;
  QueryPerformanceCounter(&li);
  if (m6_qpc_freq_ == 0) {
    LARGE_INTEGER fq;
    QueryPerformanceFrequency(&fq);
    m6_qpc_freq_ = static_cast<uint64_t>(fq.QuadPart);
    m6_last_qpc_ = li.QuadPart;
    m6_last_cpu100ns_ = ThisThreadCpu100ns();
    const char* bd = std::getenv("FABLE2_M6_BREAKDOWN");
    m6_breakdown_ = bd != nullptr && bd[0] != '0' && bd[0] != '\0';
    // M6 fast draw path: ON by default (the custom renderer is the fast
    // version); disable with FABLE2_M6_FAST_DRAW=0 to measure the built-in
    // equivalent baseline through the custom plugin.
    const char* fd = std::getenv("FABLE2_M6_FAST_DRAW");
    // OFF by default: the fast draw path skips RequestTextures, so it re-issues
    // with the last base draw's texture binding (the shared SRV heap). That is
    // invisible in 3D scenes (consecutive same-pipeline draws share textures)
    // but produces scanline smearing on full-screen splash video and the
    // pause-menu UI (many small draws that share a pipeline but use different
    // textures). Enable it with FABLE2_M6_FAST_DRAW=1 for a heavier draw-path
    // speedup at the cost of those artifacts. The fast dispatch (batch stores)
    // stays ON by default and is always correct.
    m6_fast_draw_ = fd != nullptr && fd[0] == '1';
    const char* fdp = std::getenv("FABLE2_M6_FAST_DISPATCH");
    const bool fast_dispatch = fdp == nullptr || (fdp[0] != '0' && fdp[0] != '\0');
    REXGPU_INFO("[m6] GPU-Commands self-profiler armed (breakdown={} fast_draw={} "
                "fast_dispatch={} qpc_freq={})",
                m6_breakdown_, m6_fast_draw_, fast_dispatch, m6_qpc_freq_);
    return;
  }
  const double secs =
      static_cast<double>(li.QuadPart - m6_last_qpc_) / static_cast<double>(m6_qpc_freq_);
  if (secs < 5.0) {
    return;
  }
  const uint64_t cpu100ns = ThisThreadCpu100ns();
  // FILETIME is 100ns units; 10,000 of them = 1 ms.
  const double cpu_ms = static_cast<double>(cpu100ns - m6_last_cpu100ns_) / 10000.0;
  m6_last_cpu100ns_ = cpu100ns;
  m6_last_qpc_ = li.QuadPart;
  const double cpu_pct = cpu_ms / (secs * 1000.0) * 100.0;
  const double total_s = cpu_ms / 1000.0;
  // Per-method accumulators are QPC cycles; convert with the real frequency.
  const double freq = static_cast<double>(m6_qpc_freq_);
  const double draw_s = m6_draw_ns / freq;
  const double copy_s = m6_copy_ns / freq;
  const double swap_s = m6_swap_ns / freq;
  const double write_s = m6_write_ns / freq;
  const double shader_s = m6_shader_ns / freq;
  const double measured_s = draw_s + copy_s + swap_s + write_s + shader_s;
  const double dispatch_s = total_s - measured_s;
  REXGPU_INFO(
      "[m6] GPU-Commands CPU={:.1f}% over {:.1f}s ({:.3f}s) | "
      "draw {:.3f}s ({} c) copy {:.3f}s ({} c) swap {:.3f}s ({} c) "
      "write {:.3f}s ({} c) shader {:.3f}s ({} c) | base-dispatch(residual) {:.3f}s",
      cpu_pct, secs, total_s, draw_s, m6_draw_n, copy_s, m6_copy_n, swap_s, m6_swap_n,
      write_s, m6_write_n, shader_s, m6_shader_n, dispatch_s);
  if (m6_fast_draw_ && (m6_draw_fast_hits_ + m6_draw_fast_misses_) != 0) {
    REXGPU_INFO("[m6] draw fast path: {} hits / {} misses | fp match {} (idx {} nonidx {}) / nomatch {}",
                m6_draw_fast_hits_, m6_draw_fast_misses_, m6_diag_match_, m6_diag_match_idx_,
                m6_diag_match_ - m6_diag_match_idx_, m6_diag_nomatch_);
    if (m6_breakdown_ && m6_fast_total_n != 0) {
      const double total_us =
          static_cast<double>(m6_fast_total_ns) / m6_fast_total_n / freq * 1e6;
      const double bind_us =
          static_cast<double>(m6_fast_bind_ns) / m6_fast_bind_n / freq * 1e6;
      REXGPU_INFO("[m6] fast-path per-hit: {:.3f}us (bind {:.3f}us other {:.3f}us, {} hits)",
                  total_us, bind_us, total_us - bind_us, m6_fast_total_n);
    }
  }
  // M6 dispatch profile (FABLE2_M6_DISPATCH_PROFILE=1): loop = total dispatch
  // (includes the Issue* + write time), t0/t3 = per-type totals, wait = WAIT_REG_MEM.
  {
    uint64_t dl = 0, dln = 0, dt0 = 0, dt0n = 0, dt3 = 0, dt3n = 0, dw = 0, dawn = 0;
    ::rex::graphics::M6DispatchProfileSnap(dl, dln, dt0, dt0n, dt3, dt3n, dw, dawn);
    if (dl + dt3 + dw > 0) {
      const double q = freq;
      REXGPU_INFO("[m6disp] loop={:.4f}s t0={:.4f}s/{} t3={:.4f}s/{} wait={:.4f}s/{}",
                  dl / q, dt0 / q, (unsigned long long)dt0n, dt3 / q, (unsigned long long)dt3n,
                  dw / q, (unsigned long long)dawn);
    }
  }
  // M6 draw-path sub-op profile (FABLE2_M6_DRAW_PROFILE=1): per-slow-draw cost of
  // each IssueDraw sub-op (the cold-config breakdown).
  if (m6_draw_profile_ && m6_so_total_n != 0) {
    const double q = freq;
    // cycles/op / (cycles/s) = seconds/op; *1e6 = microseconds/op.
    auto us = [q](uint64_t ns, uint64_t n) { return n ? ns / (double)n / q * 1e6 : 0.0; };
    REXGPU_INFO(
        "[m6so] base={} (n={}): TOTAL={:.3f}us analyze={:.3f} begin={:.3f} process={:.3f} "
        "rt={:.3f} trans={:.3f} config={:.3f} text={:.3f} sys={:.3f} bind={:.3f} "
        "viewport={:.3f} drawissue={:.3f}",
        "slow", (unsigned long long)m6_so_total_n, us(m6_so_total_ns, m6_so_total_n),
        us(m6_so_analyze_ns, m6_so_analyze_n), us(m6_so_begin_ns, m6_so_begin_n),
        us(m6_so_process_ns, m6_so_process_n), us(m6_so_rt_ns, m6_so_rt_n),
        us(m6_so_trans_ns, m6_so_trans_n), us(m6_so_config_ns, m6_so_config_n),
        us(m6_so_text_ns, m6_so_text_n), us(m6_so_sys_ns, m6_so_sys_n),
        us(m6_so_bind_ns, m6_so_bind_n), us(m6_so_viewport_ns, m6_so_viewport_n),
        us(m6_so_drawissue_ns, m6_so_drawissue_n));
    m6_so_process_ns = 0; m6_so_process_n = 0;
    m6_so_rt_ns = 0; m6_so_rt_n = 0;
    m6_so_config_ns = 0; m6_so_config_n = 0;
    m6_so_trans_ns = 0; m6_so_trans_n = 0;
    m6_so_text_ns = 0; m6_so_text_n = 0;
    m6_so_sys_ns = 0; m6_so_sys_n = 0;
    m6_so_bind_ns = 0; m6_so_bind_n = 0;
    m6_so_analyze_ns = 0; m6_so_analyze_n = 0;
    m6_so_begin_ns = 0; m6_so_begin_n = 0;
    m6_so_drawissue_ns = 0; m6_so_drawissue_n = 0;
    m6_so_total_ns = 0; m6_so_total_n = 0;
  }
  m6_draw_n = 0;
  m6_copy_n = 0;
  m6_swap_n = 0;
  m6_write_n = 0;
  m6_shader_n = 0;
  m6_draw_ns = 0;
  m6_copy_ns = 0;
  m6_swap_ns = 0;
  m6_write_ns = 0;
  m6_shader_ns = 0;
  m6_draw_fast_hits_ = 0;
  m6_draw_fast_misses_ = 0;
  m6_diag_match_ = 0;
  m6_diag_match_idx_ = 0;
  m6_diag_nomatch_ = 0;
  m6_fast_total_ns = 0;
  m6_fast_total_n = 0;
  m6_fast_bind_ns = 0;
  m6_fast_bind_n = 0;
}

void CustomCommandProcessor::ComputeDrawFingerprint(
    PrimitiveType prim_type, uint32_t index_count, CommandProcessor::IndexBufferInfo* index_buffer_info,
    DrawFingerprint* out) const {
  out->vertex_shader = reinterpret_cast<uint64_t>(active_vertex_shader());
  out->pixel_shader = reinterpret_cast<uint64_t>(active_pixel_shader());
  if (index_buffer_info != nullptr) {
    out->ibi_format = uint32_t(index_buffer_info->format);
    out->ibi_endianness = uint32_t(index_buffer_info->endianness);
  }
  const auto& regs = *register_file_;
  const uint32_t* src[] = {
      &regs[rg::XE_GPU_REG_PA_SU_SC_MODE_CNTL],
      &regs[rg::XE_GPU_REG_RB_DEPTHCONTROL],
      &regs[rg::XE_GPU_REG_RB_MODECONTROL],
      &regs[rg::XE_GPU_REG_VGT_HOS_CNTL],
      &regs[rg::XE_GPU_REG_PA_CL_CLIP_CNTL],
      &regs[rg::XE_GPU_REG_PA_CL_VTE_CNTL],
      &regs[rg::XE_GPU_REG_PA_SU_VTX_CNTL],
      &regs[rg::XE_GPU_REG_PA_SC_WINDOW_OFFSET],
      &regs[rg::XE_GPU_REG_PA_CL_VPORT_XSCALE],
      &regs[rg::XE_GPU_REG_PA_CL_VPORT_XOFFSET],
      &regs[rg::XE_GPU_REG_PA_CL_VPORT_YSCALE],
      &regs[rg::XE_GPU_REG_PA_CL_VPORT_YOFFSET],
      &regs[rg::XE_GPU_REG_PA_CL_VPORT_ZSCALE],
      &regs[rg::XE_GPU_REG_PA_CL_VPORT_ZOFFSET],
      &regs[rg::XE_GPU_REG_RB_SURFACE_INFO],
      &regs[rg::XE_GPU_REG_RB_COLOR_INFO],
      &regs[rg::XE_GPU_REG_RB_COLOR1_INFO],
      &regs[rg::XE_GPU_REG_RB_COLOR2_INFO],
      &regs[rg::XE_GPU_REG_RB_COLOR3_INFO],
      &regs[rg::XE_GPU_REG_RB_DEPTH_INFO],
      &regs[rg::XE_GPU_REG_RB_COLORCONTROL],
      &regs[rg::XE_GPU_REG_RB_ALPHA_REF],
      &regs[rg::XE_GPU_REG_RB_STENCILREFMASK],
      &regs[rg::XE_GPU_REG_RB_STENCILREFMASK_BF],
      &regs[rg::XE_GPU_REG_SQ_CONTEXT_MISC],
      &regs[rg::XE_GPU_REG_VGT_DRAW_INITIATOR],
  };
  const size_t n = rex::countof(src);
  for (size_t i = 0; i < n && i < rex::countof(out->regs); ++i) {
    uint32_t v = *src[i];
    if (src[i] == &regs[rg::XE_GPU_REG_VGT_DRAW_INITIATOR]) {
      // Exclude num_indices (bits 16-31): it varies per mesh and is re-read from
      // the draw's own index count on the fast path. The pipeline-relevant fields
      // (prim_type, source_select, major_mode, index_size, not_eop) are bits 0-15.
      v &= 0x0000FFFFu;
    }
    out->regs[i] = v;
  }
  // Fingerprint the texture IDENTITY for each USED texture slot so the fast
  // path only re-issues a draw whose bound textures are identical to the
  // establishing draw's. The fast path skips RequestTextures (it relies on the
  // current shared SRV heap), so a texture change must break the match. We use
  // only the identity fields — fetch constant dword_1 (format + endianness +
  // base_address) and dword_2 (size) — not the full 6-dword constant, because
  // the sampler state (LOD bias, mip level, filters) varies per draw. Unused
  // slots are zeroed (their fetch constants may hold per-draw dummy values).
  const uint32_t fetch_base = rg::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
  uint32_t used_mask = 0;
  if (auto* vs = static_cast<D3D12Shader*>(active_vertex_shader())) {
    used_mask |= vs->GetUsedTextureMaskAfterTranslation();
  }
  if (auto* ps = static_cast<D3D12Shader*>(active_pixel_shader())) {
    used_mask |= ps->GetUsedTextureMaskAfterTranslation();
  }
  for (int slot = 0; slot < 32; ++slot) {
    if (used_mask & (1u << slot)) {
      const uint32_t* fc = &regs.values[fetch_base + slot * 6];
      out->tex_id[slot * 2 + 0] = fc[1];  // dword_1: format + endianness + base_address
      out->tex_id[slot * 2 + 1] = fc[2];  // dword_2: size (width/height/depth)
    } else {
      out->tex_id[slot * 2 + 0] = 0;
      out->tex_id[slot * 2 + 1] = 0;
    }
  }
  // Diagnostic: log the used mask + the USED slots' texture identity for a few
  // consecutive draws deep in heavy gameplay, to see which fields vary.
  if (m6_diag_total_ > 400000 && m6_diag_total_ <= 400010) {
    REXGPU_INFO("[m6tex] draw={} used=0x{:X} vs={:p} ps={:p} | 13={:08X}/{:08X} 14={:08X}/{:08X} 15={:08X}/{:08X} 16={:08X}/{:08X}",
                static_cast<unsigned long long>(m6_diag_total_), used_mask,
                reinterpret_cast<void*>(out->vertex_shader),
                reinterpret_cast<void*>(out->pixel_shader), out->tex_id[26], out->tex_id[27],
                out->tex_id[28], out->tex_id[29], out->tex_id[30], out->tex_id[31],
                out->tex_id[32], out->tex_id[33]);
  }
  (void)prim_type;
  (void)index_count;
}

bool CustomCommandProcessor::MfpRebind(const FastDrawEntry& e) {
  // If the entry is already the current pipeline + root signature, no rebind is
  // needed (the common 85% case) and the fast re-issue can proceed directly.
  if (e.pipeline_handle == current_guest_pipeline_ &&
      e.root_signature == current_graphics_root_signature_) {
    return true;
  }
  if (current_graphics_root_signature_ == nullptr) {
    return false;
  }
  const auto& regs = *register_file_;
  auto* vs = static_cast<D3D12Shader*>(active_vertex_shader());
  auto* ps = static_cast<D3D12Shader*>(active_pixel_shader());
  if (vs == nullptr) {
    return false;
  }
  rg::reg::RB_DEPTHCONTROL normalized_depth_control = rg::draw_util::GetNormalizedDepthControl(regs);
  bool primitive_polygonal = rg::draw_util::IsPrimitivePolygonal(regs);
  uint32_t normalized_color_mask =
      ps ? rg::draw_util::GetNormalizedColorMask(regs, ps->writes_color_targets()) : 0;

  // Match the base IssueDraw ordering: set the root signature (forcing a root
  // parameter rebind), re-resolve this pipeline's textures (re-populating the
  // shared SRV heap), then bind the pipeline state object, and finally re-apply
  // the dynamic viewport / scissor / fixed-function / system-constant state for
  // this pipeline.
  if (e.root_signature != current_graphics_root_signature_) {
    deferred_command_list_.D3DSetGraphicsRootSignature(e.root_signature);
    current_graphics_root_signature_ = e.root_signature;
    current_graphics_root_up_to_date_ = 0;
  }
  texture_cache_->RequestTextures(e.used_texture_mask);
  if (e.pipeline_handle != current_guest_pipeline_) {
    deferred_command_list_.SetPipelineStateHandle(e.pipeline_handle);
    current_guest_pipeline_ = e.pipeline_handle;
    current_external_pipeline_ = nullptr;
  }

  // Recompute the dynamic viewport, reusing the base's viewport cache (keyed on
  // the viewport registers, which are part of the draw fingerprint). The base's
  // previous_viewport_key_ / viewport_cache_valid_ are shared, so an unchanged
  // viewport is a cache hit.
  D3D12CommandProcessor::ViewportCacheKey viewport_key;
  viewport_key.pa_cl_clip_cntl = regs[rg::XE_GPU_REG_PA_CL_CLIP_CNTL];
  viewport_key.pa_cl_vte_cntl = regs[rg::XE_GPU_REG_PA_CL_VTE_CNTL];
  viewport_key.pa_su_sc_mode_cntl = regs[rg::XE_GPU_REG_PA_SU_SC_MODE_CNTL];
  viewport_key.pa_su_vtx_cntl = regs[rg::XE_GPU_REG_PA_SU_VTX_CNTL];
  viewport_key.pa_sc_window_offset = regs[rg::XE_GPU_REG_PA_SC_WINDOW_OFFSET];
  viewport_key.normalized_depth_control = normalized_depth_control.value;
  std::memcpy(viewport_key.vport_regs, &regs[rg::XE_GPU_REG_PA_CL_VPORT_XSCALE],
              sizeof(viewport_key.vport_regs));
  bool host_render_targets_used =
      render_target_cache_->GetPath() ==
      rg::d3d12::D3D12RenderTargetCache::Path::kHostRenderTargets;
  bool convert_z_to_float24 =
      host_render_targets_used && render_target_cache_->depth_float24_convert_in_pixel_shader();
  bool ps_writes_depth = ps && ps->writes_depth();
  viewport_key.flags = (uint32_t(convert_z_to_float24) << 0) |
                       (uint32_t(host_render_targets_used) << 1) | (uint32_t(ps_writes_depth) << 2);
  rg::draw_util::ViewportInfo viewport_info;
  if (viewport_cache_valid_ && viewport_key == previous_viewport_key_) {
    viewport_info = previous_viewport_info_;
  } else {
    rg::draw_util::GetHostViewportInfo(
        regs, texture_cache_->draw_resolution_scale_x(),
        texture_cache_->draw_resolution_scale_y(), true, D3D12_VIEWPORT_BOUNDS_MAX,
        D3D12_VIEWPORT_BOUNDS_MAX, false, normalized_depth_control, convert_z_to_float24,
        host_render_targets_used, ps_writes_depth, viewport_info);
    previous_viewport_key_ = viewport_key;
    previous_viewport_info_ = viewport_info;
    viewport_cache_valid_ = true;
  }
  rg::draw_util::Scissor scissor;
  rg::draw_util::GetScissor(regs, scissor);
  scissor.offset[0] *= texture_cache_->draw_resolution_scale_x();
  scissor.offset[1] *= texture_cache_->draw_resolution_scale_y();
  scissor.extent[0] *= texture_cache_->draw_resolution_scale_x();
  scissor.extent[1] *= texture_cache_->draw_resolution_scale_y();
  UpdateFixedFunctionState(viewport_info, scissor, primitive_polygonal, normalized_depth_control);
  UpdateSystemConstantValues(/*shared_memory_is_uav=*/false, primitive_polygonal,
                             e.ppr.line_loop_closing_index, e.ppr.host_shader_index_endian,
                             viewport_info, e.used_texture_mask, normalized_depth_control,
                             normalized_color_mask);
  return true;
}

bool CustomCommandProcessor::MfpFastReissue(PrimitiveType prim_type, uint32_t index_count,
                                            CommandProcessor::IndexBufferInfo* index_buffer_info,
                                            const FastDrawEntry& e) {
  // Re-issue only the simple cases the fingerprint fully determines: not
  // tessellated, no memexport, and an indexed draw whose index buffer is read
  // directly from shared memory. The cached processing result comes from the
  // draw that established the state; the index buffer base/count and vertex
  // data are re-read from the current draw's own parameters (the fingerprint
  // deliberately excludes them), so the re-issue is correct for any mesh that
  // shares the same D3D pipeline.
  const auto& ppr = e.ppr;
  if (ppr.IsTessellated()) {
    return false;
  }
  // Two re-issuable cases the fingerprint fully determines:
  //  - indexed draw whose index buffer is read directly from shared memory
  //    (kGuestDMA); the index base/count/format come from the current draw.
  //  - non-indexed draw (kNone); the vertex count is vgt_draw_initiator.num_indices
  //    (== index_count here), and the vertex data comes from the current vfetch.
  // For kNone standard-primitive draws the primitive processor leaves
  // host_draw_vertex_count == num_indices, so index_count is the correct count.
  // (Non-standard non-indexed types - fan/quad/line-loop - are converted to
  // kHostBuiltinForAuto by the processor and are therefore excluded here.)
  const bool is_indexed = index_buffer_info != nullptr && index_count != 0 &&
                          ppr.index_buffer_type ==
                              PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA;
  const bool is_non_indexed = index_buffer_info == nullptr && index_count != 0 &&
                              ppr.index_buffer_type ==
                                  PrimitiveProcessor::ProcessedIndexBufferType::kNone;
  if (!is_indexed && !is_non_indexed) {
    return false;
  }

  // Fast-path sub-op timing (gated on m6_breakdown_): total time inside the
  // re-issue work, and the UpdateBindings share of it.
  LARGE_INTEGER ft0, ft1, bt0, bt1;
  const bool ftime = m6_breakdown_;
  if (ftime) QueryPerformanceCounter(&ft0);

  // The submission is already open for the frame (the establishing draw opened
  // it). BeginSubmission is a cheap no-op in this case and guards against the
  // (unlikely) case the frame was reopened.
  if (!BeginSubmission(true)) {
    return false;
  }
  if (current_graphics_root_signature_ == nullptr) {
    return false;
  }

  auto* vertex_shader = static_cast<D3D12Shader*>(active_vertex_shader());
  auto* pixel_shader = static_cast<D3D12Shader*>(active_pixel_shader());
  if (vertex_shader == nullptr) {
    return false;
  }

  // Refresh the per-draw constant buffers. The model view projection (float
  // constants) changes per mesh, so the register writes that precede this draw
  // have cleared the relevant up_to_date flags; UpdateBindings re-uploads only
  // those buffers and re-sets their root parameters, leaving the pipeline,
  // root signature, texture and sampler tables untouched.
  if (ftime) QueryPerformanceCounter(&bt0);
  if (!UpdateBindings(vertex_shader, pixel_shader, current_graphics_root_signature_,
                      /*shared_memory_is_uav=*/false)) {
    return false;
  }
  if (ftime) {
    QueryPerformanceCounter(&bt1);
    m6_fast_bind_ns += static_cast<uint64_t>(bt1.QuadPart - bt0.QuadPart);
    ++m6_fast_bind_n;
  }

  // Ensure vertex buffers are resident (mirrors the base's per-vfetch sync).
  const auto& regs = *register_file_;
  const Shader::ConstantRegisterMap& constant_map_vertex = vertex_shader->constant_register_map();
  for (uint32_t i = 0; i < rex::countof(constant_map_vertex.vertex_fetch_bitmap); ++i) {
    uint32_t vfetch_bits_remaining = constant_map_vertex.vertex_fetch_bitmap[i];
    uint32_t j;
    while (rex::bit_scan_forward(vfetch_bits_remaining, &j)) {
      vfetch_bits_remaining &= ~(uint32_t(1) << j);
      uint32_t vfetch_index = i * 32 + j;
      uint64_t vfetch_bit = uint64_t(1) << (vfetch_index & 63);
      if (vertex_buffers_in_sync_[vfetch_index >> 6] & vfetch_bit) {
        continue;
      }
      xe_gpu_vertex_fetch_t vfetch_constant = regs.GetVertexFetch(vfetch_index);
      if (vfetch_constant.type != FetchConstantType::kVertex &&
          vfetch_constant.type != FetchConstantType::kInvalidVertex) {
        return false;
      }
      D3D12CommandProcessor::VertexBufferState& state = vertex_buffer_states_[vfetch_index];
      if (state.address == vfetch_constant.address && state.size == vfetch_constant.size) {
        vertex_buffers_in_sync_[vfetch_index >> 6] |= vfetch_bit;
        continue;
      }
      if (!shared_memory_->RequestRange(vfetch_constant.address << 2,
                                        vfetch_constant.size << 2)) {
        return false;
      }
      state.address = vfetch_constant.address;
      state.size = vfetch_constant.size;
      vertex_buffers_in_sync_[vfetch_index >> 6] |= vfetch_bit;
    }
  }

  // Re-issue the draw with the already-bound pipeline, index buffer, topology,
  // viewport and textures.
  D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  switch (ppr.host_primitive_type) {
    case PrimitiveType::kPointList:
      topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
      break;
    case PrimitiveType::kLineList:
      topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
      break;
    case PrimitiveType::kLineStrip:
      topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
      break;
    case PrimitiveType::kTriangleList:
    case PrimitiveType::kRectangleList:
      topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      break;
    case PrimitiveType::kTriangleStrip:
      topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
      break;
    case PrimitiveType::kQuadList:
      topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
      break;
    default:
      return false;
  }
  SetPrimitiveTopology(topology);
  shared_memory_->UseForReading();
  SubmitBarriers();
  if (is_indexed) {
    // Ensure the current index buffer is resident in shared memory, then re-issue
    // the draw with the current index buffer base, count and format.
    if (!shared_memory_->RequestRange(index_buffer_info->guest_base, index_buffer_info->length)) {
      return false;
    }
    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.SizeInBytes = index_count;
    if (index_buffer_info->format == IndexFormat::kInt16) {
      ibv.SizeInBytes *= sizeof(uint16_t);
      ibv.Format = DXGI_FORMAT_R16_UINT;
    } else {
      ibv.SizeInBytes *= sizeof(uint32_t);
      ibv.Format = DXGI_FORMAT_R32_UINT;
    }
    ibv.BufferLocation = shared_memory_->GetGPUAddress() + index_buffer_info->guest_base;
    deferred_command_list_.D3DIASetIndexBuffer(&ibv);
    deferred_command_list_.D3DDrawIndexedInstanced(index_count, 1, 0, 0, 0);
  } else {
    // Non-indexed: draw index_count vertices straight from the (current) vfetch.
    deferred_command_list_.D3DDrawInstanced(index_count, 1, 0, 0);
  }
  (void)prim_type;
  if (ftime) {
    QueryPerformanceCounter(&ft1);
    m6_fast_total_ns += static_cast<uint64_t>(ft1.QuadPart - ft0.QuadPart);
    ++m6_fast_total_n;
  }
  return true;
}

bool CustomCommandProcessor::SetupContext() {
  bool ok = D3D12CommandProcessor::SetupContext();
  REXGPU_INFO("Custom D3D12 renderer: context ready via built-in D3D12 engine (ok={})", ok);
  return ok;
}

void CustomCommandProcessor::ShutdownContext() { D3D12CommandProcessor::ShutdownContext(); }

void CustomCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  LARGE_INTEGER t0, t1;
  const bool time = m6_breakdown_;
  if (time) {
    QueryPerformanceCounter(&t0);
  }
  progress::Bump(kFeatRegisterStream);
  D3D12CommandProcessor::WriteRegister(index, value);
  if (time) {
    QueryPerformanceCounter(&t1);
    m6_write_ns += static_cast<uint64_t>(t1.QuadPart - t0.QuadPart);
  }
  ++m6_write_n;
  M6MaybeSample();
}

rex::graphics::Shader* CustomCommandProcessor::LoadShader(
    rex::graphics::xenos::ShaderType shader_type, uint32_t guest_address,
    const uint32_t* host_address, uint32_t dword_count) {
  LARGE_INTEGER t0, t1;
  const bool time = m6_breakdown_;
  if (time) {
    QueryPerformanceCounter(&t0);
  }
  rex::graphics::Shader* shader =
      D3D12CommandProcessor::LoadShader(shader_type, guest_address, host_address, dword_count);
  if (time) {
    QueryPerformanceCounter(&t1);
    m6_shader_ns += static_cast<uint64_t>(t1.QuadPart - t0.QuadPart);
  }
  ++m6_shader_n;
  if (shader != nullptr) {
    progress::Bump(kFeatShaders);
  }
  M6MaybeSample();
  return shader;
}

bool CustomCommandProcessor::IssueDraw(PrimitiveType prim_type, uint32_t index_count,
                                       CommandProcessor::IndexBufferInfo* index_buffer_info,
                                       bool major_mode_explicit) {
  LARGE_INTEGER t0, t1;
  const bool time = m6_breakdown_;
  if (time) {
    QueryPerformanceCounter(&t0);
  }
  progress::Bump(kFeatDraws);
  ++m6_diag_total_;

  // M6 single-entry draw fast path. The fast path fires only when the current
  // draw's full fingerprint (shaders + 26 state registers + 192 texture-fetch
  // dwords) EXACTLY matches the previous draw's fingerprint AND the D3D
  // pipeline + root signature are already the ones that previous draw set (no
  // rebind). This is transitivity-correct: the shared SRV heap (textures),
  // viewport, fixed-function, and system-constants are all set by the last base
  // draw (the "establishing draw"), and an exact fingerprint match guarantees
  // the current draw has identical pipeline, textures, and dynamic state. A
  // multi-entry table (and a rebind path) were tried but are incorrect, because
  // the shared SRV heap is a single region set by the LAST base draw — a stale
  // entry or a re-bound pipeline can leave textures that don't match the current
  // draw, producing garbled UI/menus and artifacts.
  if (m6_fast_draw_) {
    DrawFingerprint fp;
    ComputeDrawFingerprint(prim_type, index_count, index_buffer_info, &fp);
    if (fast_path_valid_ && fast_entry_.valid && fp.Matches(fast_entry_.fp) &&
        fast_entry_.pipeline_handle == current_guest_pipeline_ &&
        fast_entry_.root_signature == current_graphics_root_signature_ &&
        MfpFastReissue(prim_type, index_count, index_buffer_info, fast_entry_)) {
      ++m6_draw_fast_hits_;
      if (time) {
        QueryPerformanceCounter(&t1);
        m6_draw_ns += static_cast<uint64_t>(t1.QuadPart - t0.QuadPart);
      }
      ++m6_draw_n;
      M6MaybeSample();
      return true;
    }
    ++m6_draw_fast_misses_;

    // Full (correct) path: the built-in implementation.
    LARGE_INTEGER tb0, tb1;
    if (time) {
      QueryPerformanceCounter(&tb0);
    }
    bool result =
        D3D12CommandProcessor::IssueDraw(prim_type, index_count, index_buffer_info,
                                         major_mode_explicit);
    if (time) {
      QueryPerformanceCounter(&tb1);
      m6_so_total_ns += static_cast<uint64_t>(tb1.QuadPart - tb0.QuadPart);
      ++m6_so_total_n;
    }
    if (result) {
      // Cache the pipeline state so future identical draws can re-bind cheaply.
      const auto& regs = *register_file_;
      if (regs.Get<rex::graphics::reg::RB_MODECONTROL>().edram_mode !=
              rex::graphics::xenos::EdramMode::kCopy &&
          active_vertex_shader() != nullptr && current_guest_pipeline_ != nullptr &&
          current_graphics_root_signature_ != nullptr &&
          regs.Get<rex::graphics::reg::RB_SURFACE_INFO>().surface_pitch != 0) {
        PrimitiveProcessor::ProcessingResult ppr;
        if (primitive_processor_->Process(ppr) && ppr.host_draw_vertex_count != 0 &&
            !ppr.IsTessellated() &&
            (ppr.index_buffer_type == PrimitiveProcessor::ProcessedIndexBufferType::kNone ||
             ppr.index_buffer_type == PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA)) {
          auto* vs = static_cast<D3D12Shader*>(active_vertex_shader());
          auto* ps = static_cast<D3D12Shader*>(active_pixel_shader());
          uint32_t mask = 0;
          if (vs) mask |= vs->GetUsedTextureMaskAfterTranslation();
          if (ps) mask |= ps->GetUsedTextureMaskAfterTranslation();
          fast_entry_.valid = true;
          fast_path_valid_ = true;
          fast_entry_.fp = fp;
          fast_entry_.pipeline_handle = current_guest_pipeline_;
          fast_entry_.root_signature = current_graphics_root_signature_;
          fast_entry_.used_texture_mask = mask;
          fast_entry_.ppr = ppr;
        }
      }
    }
    if (time) {
      QueryPerformanceCounter(&t1);
      m6_draw_ns += static_cast<uint64_t>(t1.QuadPart - t0.QuadPart);
    }
    ++m6_draw_n;
    M6MaybeSample();
    return result;
  }

  // Fast draw path disabled: just delegate to the built-in implementation.
  bool result =
      D3D12CommandProcessor::IssueDraw(prim_type, index_count, index_buffer_info,
                                       major_mode_explicit);
  if (time) {
    QueryPerformanceCounter(&t1);
    m6_draw_ns += static_cast<uint64_t>(t1.QuadPart - t0.QuadPart);
  }
  ++m6_draw_n;
  M6MaybeSample();
  return result;
}

bool CustomCommandProcessor::IssueCopy() {
  LARGE_INTEGER t0, t1;
  const bool time = m6_breakdown_;
  if (time) {
    QueryPerformanceCounter(&t0);
  }
  progress::Bump(kFeatCopies);
  bool result = D3D12CommandProcessor::IssueCopy();
  if (time) {
    QueryPerformanceCounter(&t1);
    m6_copy_ns += static_cast<uint64_t>(t1.QuadPart - t0.QuadPart);
  }
  ++m6_copy_n;
  M6MaybeSample();
  return result;
}

void CustomCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                                       uint32_t frontbuffer_height) {
  LARGE_INTEGER t0, t1;
  const bool time = m6_breakdown_;
  if (time) {
    QueryPerformanceCounter(&t0);
  }
  progress::Bump(kFeatSwap);
  ++swap_count_;
  D3D12CommandProcessor::IssueSwap(frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
  // The swap closes the frame/submission, so the previously bound state no
  // longer applies to the next frame.
  fast_path_valid_ = false;
  if (time) {
    QueryPerformanceCounter(&t1);
    m6_swap_ns += static_cast<uint64_t>(t1.QuadPart - t0.QuadPart);
  }
  ++m6_swap_n;
  M6MaybeSample();
}

}  // namespace d3d12
}  // namespace customrenderer
