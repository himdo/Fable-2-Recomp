// Native zlib inflate for the game's statically linked zlib 1.2.3.
//
// Fable 2 links zlib 1.2.3 (version string "1.2.3" at 0x820F8F34, inflate
// error strings at 0x820F9BA0..0x820F9D30). Its inflate is used by the VFS
// (bnk archives, windowBits 47 = zlib/gzip auto) and by Scaleform GFx
// (compressed SWF/GFX, GFx_InflateWrapper feeds ONE input byte per call).
// Recompiled PowerPC inflate is slow; this file replaces it with host
// zlib-ng (fetched by native_inflate.cmake).
//
// Guest API (identified from the image):
//   0x82B537C0 inflateInit2_(strm, windowBits, version, stream_size)
//   0x82B538E0 inflateInit_(strm, version, stream_size) -> b inflateInit2_(.., 15, ..)
//   0x82B53740 inflateReset(strm)
//   0x82B53A40 inflate(strm, flush)
//   0x82B55128 inflateEnd(strm)   (identical-code-folded: also called by
//                                  non-inflate callers, so only act on
//                                  streams we know)
//
// libpng 1.0.5 carries its own zlib 1.1.4 (PNG loading). Its API:
//   0x82D38A50 inflateInit2_, 0x82D38BB0 inflateInit_, 0x82D38950 inflateReset,
//   0x82D38BC0 inflate, 0x82D389C8 inflateEnd.
// That copy DOES verify the adler32 trailer, so it maps onto zlib-ng's normal
// zlib mode with no header/trailer emulation.
//
// Design: the guest init/reset/end still run (the guest state is allocated
// and freed exactly as before, so nothing else in the game sees a
// difference). A host-side stream is attached per guest z_stream address and
// inflate() runs on it, reading/writing the guest buffers in place.
//
// Fidelity notes (from the disassembly of 0x82B53A40):
//   - The game's zlib does NOT verify the zlib adler32 trailer: the CHECK
//     state only consumes the 4 bytes and sets strm->adler = 0 (the build has
//     no "incorrect data check" string). zlib-ng always verifies it, so zlib
//     streams are inflated in RAW mode here, with the 2-byte header and the
//     4-byte trailer handled below exactly like the guest.
//   - gzip streams (windowBits 47 auto-detect) go to zlib-ng in gzip mode.
//   - Return codes follow 1.2.3's tail rule: Z_OK with no progress, or with
//     Z_FINISH, becomes Z_BUF_ERROR.
#include "fable_2_pch.h"

#include <cstring>
#include <mutex>
#include <unordered_map>

#include <zlib-ng.h>

extern "C" void __imp__ProcessAndCheckAndProcess2_82B537C0(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__ProcessAndResetState_82B53740(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__ProcessStructureValidation_82B53A40(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__ProcessAndCallMethod_82B55128(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_82D38A50(PPCContext& ctx, uint8_t* base);  // 1.1.4 inflateInit2_
extern "C" void __imp__sub_82D38950(PPCContext& ctx, uint8_t* base);  // 1.1.4 inflateReset
extern "C" void __imp__sub_82D38BC0(PPCContext& ctx, uint8_t* base);  // 1.1.4 inflate
extern "C" void __imp__sub_82D389C8(PPCContext& ctx, uint8_t* base);  // 1.1.4 inflateEnd

namespace {

// Guest z_stream (32-bit, big-endian), sizeof = 0x38.
constexpr uint32_t kNextIn = 0x00, kAvailIn = 0x04, kTotalIn = 0x08;
constexpr uint32_t kNextOut = 0x0C, kAvailOut = 0x10, kTotalOut = 0x14;
constexpr uint32_t kMsg = 0x18, kDataType = 0x2C, kAdler = 0x30;

constexpr int Z_OK_ = 0, Z_STREAM_END_ = 1, Z_NEED_DICT_ = 2;
constexpr int Z_STREAM_ERROR_ = -2, Z_DATA_ERROR_ = -3, Z_BUF_ERROR_ = -5;
constexpr int Z_FINISH_ = 4;

inline uint32_t G32(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, REX_RAW_ADDR(a), 4);
  return __builtin_bswap32(v);
}
inline void S32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(REX_RAW_ADDR(a), &v, 4);
}

// Guest copies of zlib's messages, so strm->msg stays a guest pointer.
struct GuestMsg { const char* text; uint32_t addr; };
constexpr GuestMsg kMsgs[] = {
    {"incorrect header check", 0x820F9BA0},
    {"unknown compression method", 0x820F9BB8},
    {"invalid window size", 0x820F9BD4},
    {"unknown header flags set", 0x820F9BE8},
    {"header crc mismatch", 0x820F9C04},
    {"invalid block type", 0x820F9C18},
    {"invalid stored block lengths", 0x820F9C2C},
    {"too many length or distance symbols", 0x820F9C4C},
    {"invalid code lengths set", 0x820F9C70},
    {"invalid bit length repeat", 0x820F9C8C},
    {"invalid literal/lengths set", 0x820F9CA8},
    {"invalid distances set", 0x820F9CC4},
    {"invalid literal/length code", 0x820F9CDC},
    {"invalid distance code", 0x820F9CF8},
    {"invalid distance too far back", 0x820F9D10},
    {"incorrect length check", 0x820F9D30},
    {"incorrect data check", 0x82021144},  // libpng's zlib 1.1.4 copy
};
uint32_t GuestMsgFor(const char* m) {
  if (m)
    for (const auto& g : kMsgs)
      if (std::strcmp(g.text, m) == 0) return g.addr;
  return 0x820F9CA8;  // no guest copy (e.g. "invalid code -- missing end-of-block")
}

enum class Mode { Head, DictId, NeedDict, Body, Trailer, Done, Bad };

struct HostStream {
  zng_stream z{};
  bool z_live = false;
  bool gzip = false;
  int wrap = 1;   // guest semantics: bit0 zlib, bit1 gzip, 0 raw
  int wbits = 15;
  Mode mode = Mode::Head;
  uint8_t buf[4]{};
  int have = 0;
  uint32_t dictid = 0;
  uint32_t bad_msg = 0;
  bool v114 = false;  // libpng's zlib 1.1.4: plain zlib-ng in its own wrapper mode
  int zwin = 15;      // v114: windowBits exactly as the game passed them

  void FreeZ() {
    if (z_live) zng_inflateEnd(&z);
    z_live = false;
  }
  bool StartZ(int window_bits) {
    std::memset(&z, 0, sizeof(z));
    z_live = zng_inflateInit2(&z, window_bits) == Z_OK;
    return z_live;
  }
  void Reset() {
    FreeZ();
    gzip = false;
    have = 0;
    dictid = 0;
    bad_msg = 0;
    if (v114) {
      mode = StartZ(zwin) ? Mode::Body : Mode::Bad;
    } else if (wrap == 0) {
      mode = StartZ(-wbits) ? Mode::Body : Mode::Bad;
    } else {
      mode = Mode::Head;
    }
  }
  ~HostStream() { FreeZ(); }
};

std::mutex g_map_mutex;
std::unordered_map<uint32_t, HostStream*> g_streams;

HostStream* Find(uint32_t strm) {
  std::lock_guard lock(g_map_mutex);
  auto it = g_streams.find(strm);
  return it == g_streams.end() ? nullptr : it->second;
}
void Drop(uint32_t strm) {
  HostStream* s = nullptr;
  {
    std::lock_guard lock(g_map_mutex);
    auto it = g_streams.find(strm);
    if (it != g_streams.end()) {
      s = it->second;
      g_streams.erase(it);
    }
  }
  delete s;
}

// ---- host inflate ----------------------------------------------------------
int HostInflate(HostStream& s, const uint8_t*& in, uint32_t& in_left, uint8_t*& out,
                uint32_t& out_left, int flush, uint32_t& data_type) {
  for (;;) {
    switch (s.mode) {
      case Mode::Head: {
        while (s.have < 2) {
          if (!in_left) return Z_OK_;
          s.buf[s.have++] = *in++;
          --in_left;
        }
        s.have = 0;
        const uint8_t cmf = s.buf[0], flg = s.buf[1];
        if ((s.wrap & 2) && cmf == 0x1F && flg == 0x8B) {
          // gzip: hand the whole thing (magic replayed) to zlib-ng.
          if (!s.StartZ(s.wbits + 16)) { s.bad_msg = 0x820F9BA0; s.mode = Mode::Bad; break; }
          s.gzip = true;
          s.z.next_in = s.buf;
          s.z.avail_in = 2;
          uint8_t dummy;
          s.z.next_out = &dummy;
          s.z.avail_out = 0;
          zng_inflate(&s.z, 0);  // consumes the 2 header bytes, no output
          s.mode = Mode::Body;
          break;
        }
        if (!(s.wrap & 1) || ((static_cast<uint32_t>(cmf) << 8) | flg) % 31) {
          s.bad_msg = 0x820F9BA0;  // incorrect header check
          s.mode = Mode::Bad;
          break;
        }
        if ((cmf & 0x0F) != 8) {
          s.bad_msg = 0x820F9BB8;  // unknown compression method
          s.mode = Mode::Bad;
          break;
        }
        if ((cmf >> 4) + 8 > s.wbits) {
          s.bad_msg = 0x820F9BD4;  // invalid window size
          s.mode = Mode::Bad;
          break;
        }
        if (!s.StartZ(-s.wbits)) { s.bad_msg = 0x820F9BA0; s.mode = Mode::Bad; break; }
        s.mode = (flg & 0x20) ? Mode::DictId : Mode::Body;
        break;
      }
      case Mode::DictId:
        while (s.have < 4) {
          if (!in_left) return Z_OK_;
          s.buf[s.have++] = *in++;
          --in_left;
        }
        s.have = 0;
        s.dictid = (uint32_t(s.buf[0]) << 24) | (uint32_t(s.buf[1]) << 16) |
                   (uint32_t(s.buf[2]) << 8) | s.buf[3];
        s.mode = Mode::NeedDict;
        [[fallthrough]];
      case Mode::NeedDict:
        return Z_NEED_DICT_;  // the game never sets a dictionary
      case Mode::Body: {
        s.z.next_in = const_cast<uint8_t*>(in);  // zlib-ng: const; zlib: not
        s.z.avail_in = in_left;
        s.z.next_out = out;
        s.z.avail_out = out_left;
        const int r = zng_inflate(&s.z, flush);
        in = s.z.next_in;
        in_left = s.z.avail_in;
        out = s.z.next_out;
        out_left = s.z.avail_out;
        data_type = static_cast<uint32_t>(s.z.data_type);
        if (r == Z_STREAM_END_) {
          s.mode = (s.gzip || s.wrap == 0 || s.v114) ? Mode::Done : Mode::Trailer;
          break;
        }
        if (r == Z_DATA_ERROR_ || r == Z_NEED_DICT_ || r == Z_STREAM_ERROR_) {
          s.bad_msg = GuestMsgFor(s.z.msg);
          s.mode = Mode::Bad;
          break;
        }
        if (r < 0 && r != Z_BUF_ERROR_) return r;  // Z_MEM_ERROR
        return Z_OK_;  // the 1.2.3 tail rule turns no-progress into Z_BUF_ERROR
      }
      case Mode::Trailer:
        // Guest CHECK state: needs the 4 adler bytes but never compares them.
        while (s.have < 4) {
          if (!in_left) return Z_OK_;
          ++in;
          --in_left;
          ++s.have;
        }
        s.have = 0;
        s.mode = Mode::Done;
        [[fallthrough]];
      case Mode::Done:
        return Z_STREAM_END_;
      case Mode::Bad:
        return Z_DATA_ERROR_;
    }
  }
}


// One guest zlib copy: its original entry points and which flavour it is.
using GuestFn = void (*)(PPCContext&, uint8_t*);
struct Zlib {
  GuestFn init2, reset, inflate, end;
  bool v114;
};

void Init2(const Zlib& z, PPCContext& ctx, uint8_t* base) {
  const uint32_t strm = ctx.r3.u32;
  int32_t wb = static_cast<int32_t>(ctx.r4.u32);
  z.init2(ctx, base);
  if (static_cast<int32_t>(ctx.r3.u32) != Z_OK_) return;
  Drop(strm);  // re-init without inflateEnd

  auto* s = new HostStream();
  if (z.v114) {
    // 1.1.4: negative = raw, else zlib wrapper (adler32 IS checked), 8..15.
    const int w = wb < 0 ? -wb : wb;
    if (w < 8 || w > 15) { delete s; return; }
    s->v114 = true;
    s->zwin = wb;
  } else {
    // Same windowBits decoding as the guest: wrap = (wb >> 4) + 1; wb < 48 -> wb & 15.
    const int wrap = (wb >> 4) + 1;
    if (wb < 48) wb &= 15;
    if (wb < 8 || wb > 15 || wrap < 0 || wrap > 3) { delete s; return; }  // leave it to the guest
    s->wrap = wrap;
    s->wbits = wb;
  }
  s->Reset();
  {
    std::lock_guard lock(g_map_mutex);
    g_streams[strm] = s;
  }
}

// inflateInit_(strm, version, stream_size): both copies tail-branch into
// inflateInit2_ with windowBits 15.
void Init(const Zlib& z, PPCContext& ctx, uint8_t* base) {
  ctx.r6.u64 = ctx.r5.u64;
  ctx.r5.u64 = ctx.r4.u64;
  ctx.r4.s64 = 15;
  Init2(z, ctx, base);
}

void Reset(const Zlib& z, PPCContext& ctx, uint8_t* base) {
  const uint32_t strm = ctx.r3.u32;
  z.reset(ctx, base);
  if (static_cast<int32_t>(ctx.r3.u32) != Z_OK_) return;
  if (HostStream* s = Find(strm)) s->Reset();
}

void End(const Zlib& z, PPCContext& ctx, uint8_t* base) {
  if (ctx.r3.u32) Drop(ctx.r3.u32);
  z.end(ctx, base);
}

void Inflate(const Zlib& z, PPCContext& ctx, uint8_t* base) {
  const uint32_t strm = ctx.r3.u32;
  HostStream* s = strm ? Find(strm) : nullptr;

  if (!s) {
    z.inflate(ctx, base);
    return;
  }

  const int flush = static_cast<int32_t>(ctx.r4.u32);
  const uint32_t g_in = G32(base, strm + kNextIn);
  const uint32_t g_out = G32(base, strm + kNextOut);
  const uint32_t avail_in = G32(base, strm + kAvailIn);
  const uint32_t avail_out = G32(base, strm + kAvailOut);
  if (g_out == 0 || (g_in == 0 && avail_in != 0)) {
    ctx.r3.s64 = Z_STREAM_ERROR_;
    return;
  }

  const uint8_t* in = g_in ? reinterpret_cast<const uint8_t*>(REX_RAW_ADDR(g_in)) : nullptr;
  uint8_t* out = reinterpret_cast<uint8_t*>(REX_RAW_ADDR(g_out));
  const uint8_t* const in_start = in;
  uint8_t* const out_start = out;
  uint32_t in_left = avail_in, out_left = avail_out;
  uint32_t data_type = G32(base, strm + kDataType);

  int ret = HostInflate(*s, in, in_left, out, out_left, flush, data_type);

  const uint32_t used_in = static_cast<uint32_t>(in - in_start);
  const uint32_t made_out = static_cast<uint32_t>(out - out_start);
  if (((used_in == 0 && made_out == 0) || flush == Z_FINISH_) && ret == Z_OK_) ret = Z_BUF_ERROR_;

  S32(base, strm + kNextIn, g_in + used_in);
  S32(base, strm + kAvailIn, in_left);
  S32(base, strm + kTotalIn, G32(base, strm + kTotalIn) + used_in);
  S32(base, strm + kNextOut, g_out + made_out);
  S32(base, strm + kAvailOut, out_left);
  S32(base, strm + kTotalOut, G32(base, strm + kTotalOut) + made_out);
  if (s->v114) {
    S32(base, strm + kAdler, static_cast<uint32_t>(s->z.adler));  // 1.1.4 keeps a running adler32
  } else {
    S32(base, strm + kDataType, data_type);
    S32(base, strm + kAdler, s->mode == Mode::NeedDict ? s->dictid : 0);
  }
  if (s->mode == Mode::Bad && s->bad_msg) S32(base, strm + kMsg, s->bad_msg);

  ctx.r3.s64 = ret;
}

// zlib 1.2.3 (VFS, Scaleform)
constexpr Zlib kZlib123{__imp__ProcessAndCheckAndProcess2_82B537C0, __imp__ProcessAndResetState_82B53740,
                        __imp__ProcessStructureValidation_82B53A40, __imp__ProcessAndCallMethod_82B55128, false};
// zlib 1.1.4 inside libpng 1.0.5 (PNG loading)
constexpr Zlib kZlib114{__imp__sub_82D38A50, __imp__sub_82D38950, __imp__sub_82D38BC0, __imp__sub_82D389C8, true};

}  // namespace

// zlib 1.2.3: inflateInit2_ / inflateInit_ / inflateReset / inflateEnd / inflate
extern "C" void ProcessAndCheckAndProcess2_82B537C0(PPCContext& __restrict ctx, uint8_t* base) { Init2(kZlib123, ctx, base); }
extern "C" void sub_82B538E0(PPCContext& __restrict ctx, uint8_t* base) { Init(kZlib123, ctx, base); }
extern "C" void ProcessAndResetState_82B53740(PPCContext& __restrict ctx, uint8_t* base) { Reset(kZlib123, ctx, base); }
extern "C" void ProcessAndCallMethod_82B55128(PPCContext& __restrict ctx, uint8_t* base) { End(kZlib123, ctx, base); }
extern "C" void ProcessStructureValidation_82B53A40(PPCContext& __restrict ctx, uint8_t* base) { Inflate(kZlib123, ctx, base); }

// zlib 1.1.4 (libpng): same five entry points
extern "C" void sub_82D38A50(PPCContext& __restrict ctx, uint8_t* base) { Init2(kZlib114, ctx, base); }
extern "C" void sub_82D38BB0(PPCContext& __restrict ctx, uint8_t* base) { Init(kZlib114, ctx, base); }
extern "C" void sub_82D38950(PPCContext& __restrict ctx, uint8_t* base) { Reset(kZlib114, ctx, base); }
extern "C" void sub_82D389C8(PPCContext& __restrict ctx, uint8_t* base) { End(kZlib114, ctx, base); }
extern "C" void sub_82D38BC0(PPCContext& __restrict ctx, uint8_t* base) { Inflate(kZlib114, ctx, base); }
