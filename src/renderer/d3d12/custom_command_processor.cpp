// custom_command_processor.cpp - The custom D3D12 renderer.
//
// CustomCommandProcessor delegates every render operation to the built-in
// D3D12CommandProcessor (the known-working renderer) and adds the custom
// renderer's progress-HUD accounting. This is the guest-facing command
// processor: CustomGraphicsSystem returns an instance of this class.
//
// M6 (CPU fast path) adds an in-process self-profiler on the GPU-Commands
// thread: it measures the thread's total CPU (GetThreadTimes) and, when
// FABLE2_M6_BREAKDOWN=1, splits it into draw / copy / swap / write / shader
// plus the base-dispatch residual, so we can see where to optimize.
#include "custom_command_processor.h"

#include "../common/progress_checklist.h"

#include <cstdlib>

#include <windows.h>

#include <rex/logging.h>

namespace customrenderer {
namespace d3d12 {

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
    REXGPU_INFO("[m6] GPU-Commands self-profiler armed (breakdown={} qpc_freq={})", m6_breakdown_, m6_qpc_freq_);
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

bool CustomCommandProcessor::IssueDraw(rex::graphics::xenos::PrimitiveType prim_type,
                                       uint32_t index_count,
                                       rex::graphics::CommandProcessor::IndexBufferInfo* index_buffer_info,
                                       bool major_mode_explicit) {
  LARGE_INTEGER t0, t1;
  const bool time = m6_breakdown_;
  if (time) {
    QueryPerformanceCounter(&t0);
  }
  progress::Bump(kFeatDraws);
  bool result = D3D12CommandProcessor::IssueDraw(prim_type, index_count, index_buffer_info,
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
  if (time) {
    QueryPerformanceCounter(&t1);
    m6_swap_ns += static_cast<uint64_t>(t1.QuadPart - t0.QuadPart);
  }
  ++m6_swap_n;
  M6MaybeSample();
}

}  // namespace d3d12
}  // namespace customrenderer
