import sys
import os
SDK=os.environ.get('REXGLUE_SDK_SOURCE','/Volumes/EXT/fable2/rexglue-sdk')+'/src/graphics/vulkan/'
SP=sys.argv[1] if len(sys.argv)>1 else os.path.dirname(os.path.abspath(__file__))
cp=open(SDK+'command_processor.cpp').read().split('\n')
def ins(lines, lineno, text, expect):  # insert before 1-based lineno
    assert expect in lines[lineno-1], (lineno, lines[lineno-1])
    lines[lineno-1:lineno-1] = text.split('\n')
DRAW = r'''  {  // TEMPDIAG draw counters
    auto& d = g_td;
    ++d.draws;
    uint32_t vt = uint32_t(primitive_processing_result.host_vertex_shader_type);
    ++d.vs_type[vt & 15];
    uint32_t gp = uint32_t(primitive_processing_result.guest_primitive_type) & 63;
    ++d.prim[gp];
    bool tess = primitive_processing_result.IsTessellated();
    if (tess != d.last_tess) ++d.tess_switch;
    d.last_tess = tess;
    if (tess) {
      uint32_t tm = uint32_t(primitive_processing_result.tessellation_mode) & 3;
      ++d.tess_prim_mode[gp][tm];
      ++d.tmin[regs.Get<float>(XE_GPU_REG_VGT_HOS_MIN_TESS_LEVEL)];
      ++d.tmax[regs.Get<float>(XE_GPU_REG_VGT_HOS_MAX_TESS_LEVEL)];
      if (primitive_processing_result.tessellation_mode == xenos::TessellationMode::kAdaptive) {
        ++d.adraws;
        xenos::Endian e = regs.Get<reg::VGT_DMA_SIZE>().swap_mode;
        const uint32_t* p = memory_->TranslatePhysical<const uint32_t*>(
            primitive_processing_result.guest_index_base);
        uint32_t n = std::min(primitive_processing_result.guest_draw_vertex_count, 4096u);
        for (uint32_t k = 0; p && k < n; ++k) {
          uint32_t u = xenos::GpuSwap(p[k], e);
          float f;
          std::memcpy(&f, &u, 4);
          d.amin = std::min(d.amin, f);
          d.amax = std::max(d.amax, f);
          ++d.afac[std::round(f * 4.0f) * 0.25f];
          ++d.asamples;
        }
      }
    }
  }'''
COPY = '  ++g_tempdiag_copy[{}];  // TEMPDIAG'
RPFBO = r'''  {  // TEMPDIAG fbo render pass begin
    auto k = render_target_cache_->last_update_render_pass_key();
    ++g_td.rp_fbo;
    uint32_t used = k.depth_and_color_used;
    ++g_td.rp_cfg[(uint32_t(__builtin_popcount(used >> 1)) << 8) | ((used & 1) << 4) |
                  uint32_t(k.msaa_samples)];
    ++g_td.rp_ext[(uint64_t(framebuffer->host_extent.width) << 32) |
                  framebuffer->host_extent.height];
  }'''
SWAP = r'''  {  // TEMPDIAG per-second dump
    auto& d = g_td;
    ++d.frames;
    auto now = std::chrono::steady_clock::now();
    if (now - d.t0 >= std::chrono::seconds(1)) {
      double f = double(d.frames);
      std::string vs, pr, tpm, cfg, ext;
      for (int i = 0; i < 16; ++i) if (d.vs_type[i]) vs += fmt::format("{}:{:.1f} ", i, d.vs_type[i] / f);
      for (int i = 0; i < 64; ++i) if (d.prim[i]) pr += fmt::format("0x{:X}:{:.1f} ", i, d.prim[i] / f);
      for (int i = 0; i < 64; ++i) for (int m = 0; m < 4; ++m)
        if (d.tess_prim_mode[i][m]) tpm += fmt::format("p0x{:X}/m{}:{:.1f} ", i, m, d.tess_prim_mode[i][m] / f);
      for (auto& [c, n] : d.rp_cfg)
        cfg += fmt::format("[col{} dep{} msaa{}]:{:.1f} ", c >> 8, (c >> 4) & 1, 1u << (c & 3), n / f);
      for (auto& [c, n] : d.rp_ext) ext += fmt::format("{}x{}:{:.1f} ", c >> 32, c & 0xFFFFFFFF, n / f);
      REXGPU_INFO("TEMPDIAG frames={} draws/f={:.1f} vs_type/f: {}| prim/f: {}| tess prim/mode/f: {}| tess_switch/f={:.1f}",
                  d.frames, d.draws / f, vs, pr, tpm, d.tess_switch / f);
      REXGPU_INFO("TEMPDIAG tess MIN_LEVEL [min {} max {}] top5 {}| MAX_LEVEL [min {} max {}] top5 {}| adaptive draws/f={:.1f} samples={} range [{} .. {}] top5 {}",
                  d.tmin.empty() ? 0.f : d.tmin.begin()->first, d.tmin.empty() ? 0.f : d.tmin.rbegin()->first, TempDiagTop(d.tmin, 5),
                  d.tmax.empty() ? 0.f : d.tmax.begin()->first, d.tmax.empty() ? 0.f : d.tmax.rbegin()->first, TempDiagTop(d.tmax, 5),
                  d.adraws / f, d.asamples, d.amin, d.amax, TempDiagTop(d.afac, 5));
      REXGPU_INFO("TEMPDIAG rp_fbo/f={:.1f} rp_transfer/f={:.1f} cfg/f: {}| host extent/f: {}| copies/f sm309={:.1f} sm328={:.1f} cp4081={:.1f} cp4217={:.1f} cp4342={:.1f} cp4636={:.1f} cp4646={:.1f} | front {}x{} scale {}x{}",
                  d.rp_fbo / f, d.rp_transfer / f, cfg, ext,
                  g_tempdiag_copy[0] / f, g_tempdiag_copy[1] / f, g_tempdiag_copy[2] / f, g_tempdiag_copy[3] / f,
                  g_tempdiag_copy[4] / f, g_tempdiag_copy[5] / f, g_tempdiag_copy[6] / f,
                  frontbuffer_width, frontbuffer_height,
                  render_target_cache_ ? render_target_cache_->draw_resolution_scale_x() : 0,
                  render_target_cache_ ? render_target_cache_->draw_resolution_scale_y() : 0);
      d = TempDiag();
      d.t0 = now;
      for (auto& c : g_tempdiag_copy) c = 0;
    }
  }'''
# bottom-up
ins(cp, 4646, COPY.format(6), 'CmdVkCopyBuffer(shared_memory_->buffer()')
ins(cp, 4636, COPY.format(5), 'CmdVkCopyBuffer(resolve_downscale_buffer_')
ins(cp, 4342, COPY.format(4), 'CmdVkCopyBuffer(shared_memory_->buffer()')
ins(cp, 4217, COPY.format(3), 'CmdVkCopyBuffer(shared_memory_->buffer()')
ins(cp, 4118, DRAW, '// Draw.')
ins(cp, 4081, COPY.format(2), 'CmdVkCopyBuffer(shared_memory_->buffer()')
ins(cp, 3253, '  ++g_td.rp_transfer;  // TEMPDIAG', 'in_render_pass_ = true;')
ins(cp, 3161, RPFBO, 'in_render_pass_ = true;')
ins(cp, 2275, SWAP, 'vertex_buffers_in_sync_[0] = 0;')
ins(cp, 74, open(SP+'/tempdiag_block.inc').read().rstrip('\n'), 'namespace {')
ins(cp, 23, '#include <chrono>  // TEMPDIAG\n#include <cmath>  // TEMPDIAG\n#include <map>  // TEMPDIAG', '')
open(SDK+'command_processor.cpp','w').write('\n'.join(cp))
sm=open(SDK+'shared_memory.cpp').read().split('\n')
ins(sm, 328, COPY.format(1).replace('  ','    ',1), 'command_buffer.CmdVkCopyBuffer(upload_buffer_previous')
ins(sm, 309, COPY.format(0).replace('  ','        ',1), 'command_buffer.CmdVkCopyBuffer(upload_buffer_previous')
ins(sm, 25, 'namespace rex::graphics::vulkan { extern uint64_t g_tempdiag_copy[8]; }  // TEMPDIAG', '')
open(SDK+'shared_memory.cpp','w').write('\n'.join(sm))
print('done')
