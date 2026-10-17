// fable_2 - Direct mouse-to-camera look (shared host/guest state)
//
// Separates the mouse from the keyboard->gamepad mapping. Instead of writing
// mouse movement into the synthetic pad's right stick (which the game then
// integrates at a fixed stick rate), the host input driver accumulates the raw
// cursor displacement here (src/input/keyboard_gamepad.h) and a mid-asm hook in
// the active camera's per-frame update (fable2_hook_direct_mouse_look in
// src/core/fable2_hooks.cpp) drains it once per frame and applies it directly
// to the camera orientation. The result is a desktop-FPS style "move the mouse
// that far, turn the camera that far" feel with no rate cap.
//
// Threading: the input driver (host/SDK input poll) only ever ADDS to the
// accumulators; the guest camera hook only ever EXCHANGES them to zero. A delta
// can be split across two consecutive drains by a poll that lands between them,
// which is sub-perceptual and self-corrects.
#pragma once

#include <cmath>
#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/system/xmemory.h>  // rex::memory::Memory (guest address -> host pointer)

namespace fable2::mouselook {

// Accumulated raw cursor displacement (host pixels) since the last drain.
// X = right is positive, Y = down is positive (screen coordinates); the hook
// applies the sign/scale from fable2_config.
inline std::atomic<int32_t>& dx_acc() {
  static std::atomic<int32_t> v{0};
  return v;
}
inline std::atomic<int32_t>& dy_acc() {
  static std::atomic<int32_t> v{0};
  return v;
}

// Host input driver: add this poll's cursor displacement.
inline void add_delta(int32_t dx, int32_t dy) {
  dx_acc().fetch_add(dx, std::memory_order_relaxed);
  dy_acc().fetch_add(dy, std::memory_order_relaxed);
}

// Guest camera hook: consume and reset the accumulated displacement.
inline void take_delta(int32_t& out_dx, int32_t& out_dy) {
  out_dx = dx_acc().exchange(0, std::memory_order_relaxed);
  out_dy = dy_acc().exchange(0, std::memory_order_relaxed);
}

// Guest memory, set once at image load (Fable2App::OnPostLoadXexImage) so the
// camera hook can translate the camera object's guest address to a host
// pointer. Null until set; the hook no-ops while it is null.
inline std::atomic<rex::memory::Memory*>& memory_ref() {
  static std::atomic<rex::memory::Memory*> m{nullptr};
  return m;
}
inline void set_memory(rex::memory::Memory* mem) { memory_ref().store(mem, std::memory_order_release); }

// --- Big-endian guest float access (matches REX_LOAD_U32 / REX_STORE_U32) ----
inline float read_guest_float(rex::memory::Memory* mem, uint32_t guest_addr) {
  const uint32_t* p = mem->TranslateVirtual<const uint32_t*>(guest_addr);
  uint32_t bits = __builtin_bswap32(*p);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}
inline void write_guest_float(rex::memory::Memory* mem, uint32_t guest_addr, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, 4);
  uint32_t* p = mem->TranslateVirtual<uint32_t*>(guest_addr);
  *p = __builtin_bswap32(bits);
}
inline uint32_t read_guest_u32(rex::memory::Memory* mem, uint32_t guest_addr) {
  const uint32_t* p = mem->TranslateVirtual<const uint32_t*>(guest_addr);
  return __builtin_bswap32(*p);
}
// True if guest_addr points into a plausibly-valid guest region. Two ranges
// matter: the recompiled image (code + data + globals) is mapped at 0x82xxxxxx,
// while live heap objects (the CECCamera, its controller, etc.) sit lower at
// 0x42xxxxxx. Accepting both lets multi-level chains hop between heap objects
// and image globals. Excludes 0/1 sentinels and tiny garbage.
inline bool is_plausible_guest_ptr(uint32_t a) {
  return (a >= 0x40000000u && a < 0x48000000u) ||  // live heap objects
         (a >= 0x82000000u && a < 0x84000000u);      // recompiled image
}
// Bounds-checked u32 read: returns ok=false (and leaves *out=0) if the address
// is not a plausible guest pointer. Never dereferences an out-of-range address.
inline bool try_read_guest_u32(rex::memory::Memory* mem, uint32_t guest_addr,
                               uint32_t* out) {
  if (!is_plausible_guest_ptr(guest_addr)) { *out = 0; return false; }
  *out = read_guest_u32(mem, guest_addr);
  return true;
}

// --- Big-endian guest quaternion (4 floats) access ---------------------------
// The active camera's orientation target is a 4-float vector (x, y, z, w) at
// camera+144 that CECCameraUpdateFrame lerps into the rendered view.
struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
};

inline Quat read_guest_quat(rex::memory::Memory* mem, uint32_t guest_addr) {
  Quat q;
  q.x = read_guest_float(mem, guest_addr + 0);
  q.y = read_guest_float(mem, guest_addr + 4);
  q.z = read_guest_float(mem, guest_addr + 8);
  q.w = read_guest_float(mem, guest_addr + 12);
  return q;
}
inline void write_guest_quat(rex::memory::Memory* mem, uint32_t guest_addr, const Quat& q) {
  write_guest_float(mem, guest_addr + 0, q.x);
  write_guest_float(mem, guest_addr + 4, q.y);
  write_guest_float(mem, guest_addr + 8, q.z);
  write_guest_float(mem, guest_addr + 12, q.w);
}

// Quaternion multiply (a * b).
inline Quat quat_mul(const Quat& a, const Quat& b) {
  return Quat{
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// Build a unit quaternion rotating `angle` radians around the given axis
// (0=X, 1=Y, 2=Z).
inline Quat axis_quat(int axis, float angle) {
  Quat r{0.0f, 0.0f, 0.0f, 1.0f};
  const float s = std::sin(angle * 0.5f);
  r.w = std::cos(angle * 0.5f);
  if (axis == 0) r.x = s;
  else if (axis == 1) r.y = s;
  else r.z = s;
  return r;
}

// Rotate a guest quaternion in place by a yaw around `yaw_axis` and a pitch
// around `pitch_axis` (each 0=X, 1=Y, 2=Z), applied in the camera's local frame
// (post-multiply), then renormalized.
inline void rotate_guest_quat(rex::memory::Memory* mem, uint32_t guest_addr, float yaw,
                              float pitch, int yaw_axis = 1, int pitch_axis = 0) {
  Quat q = read_guest_quat(mem, guest_addr);
  if (std::abs(q.x) < 1e-9f && std::abs(q.y) < 1e-9f && std::abs(q.z) < 1e-9f &&
      std::abs(q.w) < 1e-9f)
    return;  // all-zero vector, not a quaternion -- leave it alone
  Quat n = quat_mul(q, quat_mul(axis_quat(yaw_axis, yaw), axis_quat(pitch_axis, pitch)));
  float m = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z + n.w * n.w);
  if (m > 1e-6f) { n.x /= m; n.y /= m; n.z /= m; n.w /= m; }
  write_guest_quat(mem, guest_addr, n);
}

// --- Big-endian guest 3-vector access ---------------------------------------
// The active camera's world-space EYE position is the 3-float at camera+144
// (the 4th float at +156 is always 0). When the game's own right-stick look
// orbits the camera (hero stationary), this vector sweeps a circle of radius
// ~3.1 around the hero -- it is the orbiting eye, not the look-at/hero point.
struct Vec3 {
  float x = 0, y = 0, z = 0;
};
inline Vec3 read_guest_vec3(rex::memory::Memory* mem, uint32_t guest_addr) {
  return Vec3{read_guest_float(mem, guest_addr + 0),
              read_guest_float(mem, guest_addr + 4),
              read_guest_float(mem, guest_addr + 8)};
}
inline void write_guest_vec3(rex::memory::Memory* mem, uint32_t guest_addr, const Vec3& v) {
  write_guest_float(mem, guest_addr + 0, v.x);
  write_guest_float(mem, guest_addr + 4, v.y);
  write_guest_float(mem, guest_addr + 8, v.z);
}
inline float vec3_len(const Vec3& v) {
  return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
inline Vec3 vec3_cross(const Vec3& a, const Vec3& b) {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 vec3_normalize(Vec3 v) {
  const float m = vec3_len(v);
  if (m < 1e-9f) return Vec3{0.0f, 0.0f, 0.0f};
  return Vec3{v.x / m, v.y / m, v.z / m};
}

// Rotate vector v by quaternion q (q * v * conj(q)).
inline Vec3 quat_rotate_vec(const Quat& q, const Vec3& v) {
  const Vec3 qv{q.x, q.y, q.z};
  // t = 2 * cross(qv, v)
  const Vec3 t{2.0f * (qv.y * v.z - qv.z * v.y),
               2.0f * (qv.z * v.x - qv.x * v.z),
               2.0f * (qv.x * v.y - qv.y * v.x)};
  // v' = v + qw*t + cross(qv, t)
  return Vec3{v.x + q.w * t.x + (qv.y * t.z - qv.z * t.y),
              v.y + q.w * t.y + (qv.z * t.x - qv.x * t.z),
              v.z + q.w * t.z + (qv.x * t.y - qv.y * t.x)};
}

// Unit quaternion rotating `angle` radians around an arbitrary (normalized)
// axis vector. A zero axis yields the identity.
inline Quat axis_quat_vec(const Vec3& axis, float angle) {
  const float m = vec3_len(axis);
  if (m < 1e-9f) return Quat{0.0f, 0.0f, 0.0f, 1.0f};
  const float s = std::sin(angle * 0.5f) / m;
  return Quat{axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
}

// Unit axis vector for a 0=X, 1=Y, 2=Z axis index (matches the int-axis
// convention of the config's mouse_look_*_axis keys).
inline Vec3 axis_vec(int axis) {
  Vec3 v{1.0f, 0.0f, 0.0f};
  if (axis == 1) v = Vec3{0.0f, 1.0f, 0.0f};
  else if (axis == 2) v = Vec3{0.0f, 0.0f, 1.0f};
  return v;
}

// Quaternion for the orthonormal basis whose columns are the world-space
// directions of the camera's local X (right), Y (forward), Z (up) axes.
// Produces a roll-free look-at orientation when right/forward/up are built from
// a forward vector and a world-up vector.
inline Quat quat_from_basis(const Vec3& right, const Vec3& forward, const Vec3& up) {
  const float m00 = right.x, m01 = forward.x, m02 = up.x;
  const float m10 = right.y, m11 = forward.y, m12 = up.y;
  const float m20 = right.z, m21 = forward.z, m22 = up.z;
  Quat q{0.0f, 0.0f, 0.0f, 1.0f};
  const float tr = m00 + m11 + m22;
  if (tr > 0.0f) {
    const float s = 0.5f / std::sqrt(tr + 1.0f);
    q.w = 0.25f / s;
    q.x = (m21 - m12) * s;
    q.y = (m02 - m20) * s;
    q.z = (m10 - m01) * s;
  } else if (m00 > m11 && m00 > m22) {
    const float s = 2.0f * std::sqrt(1.0f + m00 - m11 - m22);
    q.w = (m21 - m12) / s;
    q.x = 0.25f * s;
    q.y = (m01 + m10) / s;
    q.z = (m02 + m20) / s;
  } else if (m11 > m22) {
    const float s = 2.0f * std::sqrt(1.0f + m11 - m00 - m22);
    q.w = (m02 - m20) / s;
    q.x = (m01 + m10) / s;
    q.y = 0.25f * s;
    q.z = (m12 + m21) / s;
  } else {
    const float s = 2.0f * std::sqrt(1.0f + m22 - m00 - m11);
    q.w = (m10 - m01) / s;
    q.x = (m02 + m20) / s;
    q.y = (m12 + m21) / s;
    q.z = 0.25f * s;
  }
  const float m = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (m > 1e-6f) { q.x /= m; q.y /= m; q.z /= m; q.w /= m; }
  return q;
}

// --- Persistent look offset --------------------------------------------------
// The game's hero-follow logic recomputes the camera orientation from scratch
// every frame, so a per-frame mouse rotation springs back to the hero the moment
// the cursor stops. Instead we accumulate a running look offset (radians) and
// re-apply it to the freshly-reset hero-relative orientation each frame, so the
// camera stays where it is aimed. Only the guest camera hook touches these.
inline float& cum_yaw() { static float v = 0.0f; return v; }
inline float& cum_pitch() { static float v = 0.0f; return v; }
inline void reset_cumulative() { cum_yaw() = 0.0f; cum_pitch() = 0.0f; }

// Persistent hero pivot the orbit is computed around (low-passed by the hook so
// per-frame jitter in the forward/eye estimate cannot shake the orbit). Only the
// guest camera hook touches these.
inline Vec3& orbit_pivot() { static Vec3 v{0.0f, 0.0f, 0.0f}; return v; }
inline bool& orbit_pivot_init() { static bool v = false; return v; }
inline void reset_orbit_pivot() { orbit_pivot_init() = false; }

}  // namespace fable2::mouselook
