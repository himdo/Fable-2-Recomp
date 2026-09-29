// Compatibility helpers adapted from XeniOS base/math.h, base/memory.h and base/memory.cc.
// Copyright 2019 Ben Vanik. All rights reserved. (base/math.h)
// Copyright 2020 Ben Vanik. All rights reserved. (base/memory.h)
// Copyright 2022 Ben Vanik. All rights reserved. (base/memory.cc)
// BSD-3-Clause; see backends/metal/licenses/XeniOS-LICENSE.
#pragma once
#include <rex/assert.h>
#include <rex/hash.h>
#include <rex/memory/utils.h>
#include <rex/thread/mutex.h>
#include <cstring>
namespace rex {
using global_unique_lock_type = std::unique_lock<std::recursive_mutex>;
class global_critical_region : public thread::global_critical_region {
 public:
 global_unique_lock_type Acquire() const { return AcquireDirect(); }
};
namespace hash {
template<class T> using XXHasher = rex::XXHasher<T>;
template<class T> using IdentityHasher = rex::IdentityHasher<T>;
}
// Same ARM64 implementation as XeniOS memory.cc.
namespace memory {
inline void vastcpy(uint8_t* dest, uint8_t* src, uint32_t size) { std::memcpy(dest, src, size); }
}
template <size_t sz>
class FixedVMemVector {
  static_assert((sz & 65535) == 0,
                "Always give fixed_vmem_vector a size divisible by 65536 to "
                "avoid wasting memory on windows");

  uint8_t* data_;
  size_t nbytes_;

 public:
  FixedVMemVector()
      : data_((uint8_t*)memory::AllocFixed(
            nullptr, sz, memory::AllocationType::kReserveCommit,
            memory::PageAccess::kReadWrite)),
        nbytes_(0) {}
  ~FixedVMemVector() {
    if (data_) {
      memory::DeallocFixed(data_, sz, memory::DeallocationType::kRelease);
      data_ = nullptr;
    }
    nbytes_ = 0;
  }

  uint8_t* data() const { return data_; }
  size_t size() const { return nbytes_; }

  void resize(size_t newsize) {
    nbytes_ = newsize;
    xenia_assert(newsize < sz);
  }
  size_t alloc() const { return sz; }

  void clear() {
    resize(0);  // todo:maybe zero out
  }
  void reserve(size_t size) { xenia_assert(size < sz); }
};
// software prefetches/cache operations
namespace swcache {
/*
        warning, prefetchw's current behavior is not consistent across msvc and
   clang, for clang it will only compile to prefetchw if the set architecture
   supports it, for msvc however it will unconditionally compile to prefetchw!
        so prefetchw support is still in process


        only use these if you're absolutely certain you know what you're doing;
   you can easily tank performance through misuse CPUS have excellent automatic
   prefetchers that can predict patterns, but in situations where memory
   accesses are super unpredictable and follow no pattern you can make use of
   them

        another scenario where it can be handy is when crossing page boundaries,
   as many automatic prefetchers do not allow their streams to cross pages (no
   idea what this means for huge pages)

        I believe software prefetches do not kick off an automatic prefetcher
   stream, so you can't just prefetch one line of the data you're about to
   access and be fine, you need to go all the way

        prefetchnta is implementation dependent, and that makes its use a bit
   limited. For intel cpus, i believe it only prefetches the line into one way
   of the L3

        for amd cpus, it marks the line as requiring immediate eviction, the
   next time an entry is needed in the set it resides in it will be evicted. ms
   does dumb shit for memcpy, like looping over the contents of the source
   buffer and doing prefetchnta on them, likely evicting some of the data they
   just prefetched by the end of the buffer, and probably messing up data that
   was already in the cache


        another warning for these: this bypasses what i think is called
   "critical word load", the data will always become available starting from the
   very beginning of the line instead of from the piece that is needed

        L1I cache is not prefetchable, however likely all cpus can fulfill
   requests for the L1I from L2, so prefetchL2 on instructions should be fine

        todo: clwb, clflush
*/
#if XE_COMPILER_HAS_GNU_EXTENSIONS == 1

XE_FORCEINLINE
static void PrefetchW(const void* addr) { __builtin_prefetch(addr, 1, 0); }
XE_FORCEINLINE

static void PrefetchNTA(const void* addr) { __builtin_prefetch(addr, 0, 0); }
XE_FORCEINLINE

static void PrefetchL3(const void* addr) { __builtin_prefetch(addr, 0, 1); }
XE_FORCEINLINE

static void PrefetchL2(const void* addr) { __builtin_prefetch(addr, 0, 2); }
XE_FORCEINLINE

static void PrefetchL1(const void* addr) { __builtin_prefetch(addr, 0, 3); }
#elif XE_ARCH_AMD64 == 1 && XE_COMPILER_MSVC == 1
XE_FORCEINLINE
static void PrefetchW(const void* addr) { _m_prefetchw(addr); }

XE_FORCEINLINE
static void PrefetchNTA(const void* addr) {
  _mm_prefetch((const char*)addr, _MM_HINT_NTA);
}
XE_FORCEINLINE

static void PrefetchL3(const void* addr) {
  _mm_prefetch((const char*)addr, _MM_HINT_T2);
}
XE_FORCEINLINE

static void PrefetchL2(const void* addr) {
  _mm_prefetch((const char*)addr, _MM_HINT_T1);
}
XE_FORCEINLINE

static void PrefetchL1(const void* addr) {
  _mm_prefetch((const char*)addr, _MM_HINT_T0);
}

#else
XE_FORCEINLINE
static void PrefetchW(const void* addr) {}

XE_FORCEINLINE
static void PrefetchNTA(const void* addr) {}
XE_FORCEINLINE

static void PrefetchL3(const void* addr) {}
XE_FORCEINLINE

static void PrefetchL2(const void* addr) {}
XE_FORCEINLINE

static void PrefetchL1(const void* addr) {}

#endif

enum class PrefetchTag { Write, Nontemporal, Level3, Level2, Level1 };

template <PrefetchTag tag>
static void Prefetch(const void* addr) {
  xenia_assert(false && "Unknown tag");
}

template <>
inline void Prefetch<PrefetchTag::Write>(const void* addr) {
  PrefetchW(addr);
}
template <>
inline void Prefetch<PrefetchTag::Nontemporal>(const void* addr) {
  PrefetchNTA(addr);
}
template <>
inline void Prefetch<PrefetchTag::Level3>(const void* addr) {
  PrefetchL3(addr);
}
template <>
inline void Prefetch<PrefetchTag::Level2>(const void* addr) {
  PrefetchL2(addr);
}
template <>
inline void Prefetch<PrefetchTag::Level1>(const void* addr) {
  PrefetchL1(addr);
}
// todo: does aarch64 have streaming stores/loads?

/*
        non-temporal stores/loads

        the stores allow cacheable memory to behave like write-combining memory.
        on the first nt store to a line, an intermediate buffer will be
   allocated by the cpu for stores that come after. once the entire contents of
   the line have been written the intermediate buffer will be transmitted to
   memory

        the written line will not be cached and if it is in the cache it will be
   invalidated from all levels of the hierarchy

        the cpu in this case does not have to read line from memory when we
   first write to it if it is not anywhere in the cache, so we use half the
   memory bandwidth using these stores

        non-temporal loads are... loads, but they dont use the cache. you need
   to manually insert memory barriers (_ReadWriteBarrier, ReadBarrier, etc, do
   not use any barriers that generate actual code) if on msvc to prevent it from
   moving the load of the data to just before the use of the data (immediately
   requiring the memory to be available = big stall)



*/

#if XE_COMPILER_MSVC == 1 && XE_COMPILER_CLANG_CL == 0
#define XE_MSVC_REORDER_BARRIER _ReadWriteBarrier

#else
// if the compiler actually has pipelining for instructions we dont need a
// barrier
#define XE_MSVC_REORDER_BARRIER() static_cast<void>(0)
#endif
#if XE_ARCH_AMD64 == 1
union alignas(XE_HOST_CACHE_LINE_SIZE) CacheLine {
  struct {
    __m256 low32;
    __m256 high32;
  };
  struct {
    __m128i xmms[4];
  };
  float floats[XE_HOST_CACHE_LINE_SIZE / sizeof(float)];
};
XE_FORCEINLINE
static void WriteLineNT(CacheLine* XE_RESTRICT destination,
                        const CacheLine* XE_RESTRICT source) {
  assert_true((reinterpret_cast<uintptr_t>(destination) & 63ULL) == 0);
  __m256 low = _mm256_loadu_ps(&source->floats[0]);
  __m256 high = _mm256_loadu_ps(&source->floats[8]);
  _mm256_stream_ps(&destination->floats[0], low);
  _mm256_stream_ps(&destination->floats[8], high);
}

XE_FORCEINLINE
static void ReadLineNT(CacheLine* XE_RESTRICT destination,
                       const CacheLine* XE_RESTRICT source) {
  assert_true((reinterpret_cast<uintptr_t>(source) & 63ULL) == 0);

  __m128i first = _mm_stream_load_si128(&source->xmms[0]);
  __m128i second = _mm_stream_load_si128(&source->xmms[1]);
  __m128i third = _mm_stream_load_si128(&source->xmms[2]);
  __m128i fourth = _mm_stream_load_si128(&source->xmms[3]);

  destination->xmms[0] = first;
  destination->xmms[1] = second;
  destination->xmms[2] = third;
  destination->xmms[3] = fourth;
}
XE_FORCEINLINE
static void ReadLine(CacheLine* XE_RESTRICT destination,
                     const CacheLine* XE_RESTRICT source) {
  assert_true((reinterpret_cast<uintptr_t>(source) & 63ULL) == 0);
  __m256 low = _mm256_loadu_ps(&source->floats[0]);
  __m256 high = _mm256_loadu_ps(&source->floats[8]);
  _mm256_storeu_ps(&destination->floats[0], low);
  _mm256_storeu_ps(&destination->floats[8], high);
}
XE_FORCEINLINE
static void WriteLine(CacheLine* XE_RESTRICT destination,
                      const CacheLine* XE_RESTRICT source) {
  assert_true((reinterpret_cast<uintptr_t>(destination) & 63ULL) == 0);
  __m256 low = _mm256_loadu_ps(&source->floats[0]);
  __m256 high = _mm256_loadu_ps(&source->floats[8]);
  _mm256_storeu_ps(&destination->floats[0], low);
  _mm256_storeu_ps(&destination->floats[8], high);
}

XE_FORCEINLINE
static void WriteFence() { _mm_sfence(); }
XE_FORCEINLINE
static void ReadFence() { _mm_lfence(); }
XE_FORCEINLINE
static void ReadWriteFence() { _mm_mfence(); }

#else
union alignas(XE_HOST_CACHE_LINE_SIZE) CacheLine {
  uint8_t bvals[XE_HOST_CACHE_LINE_SIZE];
};
XE_FORCEINLINE
static void WriteLineNT(CacheLine* destination, const CacheLine* source) {
  memcpy(destination, source, XE_HOST_CACHE_LINE_SIZE);
}

XE_FORCEINLINE
static void ReadLineNT(CacheLine* destination, const CacheLine* source) {
  memcpy(destination, source, XE_HOST_CACHE_LINE_SIZE);
}
XE_FORCEINLINE
static void WriteLine(CacheLine* destination, const CacheLine* source) {
  memcpy(destination, source, XE_HOST_CACHE_LINE_SIZE);
}
XE_FORCEINLINE
static void ReadLine(CacheLine* destination, const CacheLine* source) {
  memcpy(destination, source, XE_HOST_CACHE_LINE_SIZE);
}

XE_FORCEINLINE
static void WriteFence() {}
XE_FORCEINLINE
static void ReadFence() {}
XE_FORCEINLINE
static void ReadWriteFence() {}
#endif
}  // namespace swcache
namespace divisors {
union IDivExtraInfo {
  uint32_t value_;
  struct {
    uint32_t shift_ : 31;
    uint32_t add_ : 1;
  } info;
};
// returns magicnum multiplier
static constexpr uint32_t PregenerateUint32Div(uint32_t _denom,
                                               uint32_t& out_extra) {
  IDivExtraInfo extra{};

  uint32_t d = _denom;
  int p = 0;
  uint32_t nc = 0, delta = 0, q1 = 0, r1 = 0, q2 = 0, r2 = 0;
  struct {
    unsigned M;
    int a;
    int s;
  } magu{};
  magu.a = 0;
  nc = -1 - ((uint32_t)-(int32_t)d) % d;
  p = 31;
  q1 = 0x80000000 / nc;
  r1 = 0x80000000 - q1 * nc;
  q2 = 0x7FFFFFFF / d;
  r2 = 0x7FFFFFFF - q2 * d;
  do {
    p += 1;
    if (r1 >= nc - r1) {
      q1 = 2 * q1 + 1;
      r1 = 2 * r1 - nc;
    } else {
      q1 = 2 * q1;
      r1 = 2 * r1;
    }
    if (r2 + 1 >= d - r2) {
      if (q2 >= 0x7FFFFFFF) {
        magu.a = 1;
      }
      q2 = 2 * q2 + 1;
      r2 = 2 * r2 + 1 - d;

    } else {
      if (q2 >= 0x80000000U) {
        magu.a = 1;
      }
      q2 = 2 * q2;
      r2 = 2 * r2 + 1;
    }
    delta = d - 1 - r2;
  } while (p < 64 && (q1 < delta || r1 == 0));

  extra.info.add_ = magu.a;
  extra.info.shift_ = p - 32;
  out_extra = extra.value_;
  return static_cast<uint64_t>(q2 + 1);
}

static constexpr uint32_t ApplyUint32Div(uint32_t num, uint32_t mul,
                                         uint32_t extradata) {
  IDivExtraInfo extra{};

  extra.value_ = extradata;

  uint32_t result = static_cast<uint32_t>(
      (static_cast<uint64_t>(num) * static_cast<uint64_t>(mul)) >> 32);
  if (extra.info.add_) {
    uint32_t addend = result + num;
    addend = ((addend < result ? 0x80000000 : 0) | addend);
    result = addend;
  }
  return result >> extra.info.shift_;
}

static constexpr uint32_t ApplyUint32UMod(uint32_t num, uint32_t mul,
                                          uint32_t extradata,
                                          uint32_t original) {
  uint32_t dived = ApplyUint32Div(num, mul, extradata);
  unsigned result = num - (dived * original);

  return result;
}

struct MagicDiv {
  uint32_t multiplier_;
  uint32_t extradata_;
  constexpr MagicDiv() : multiplier_(0), extradata_(0) {}
  constexpr MagicDiv(uint32_t original) : MagicDiv() {
    multiplier_ = PregenerateUint32Div(original, extradata_);
  }

  constexpr uint32_t GetRightShift() const {
    IDivExtraInfo extra{};

    extra.value_ = extradata_;
    return extra.info.shift_;
  }

  constexpr bool AddFlag() const {
    IDivExtraInfo extra{};

    extra.value_ = extradata_;
    return extra.info.shift_;
  }

  constexpr uint32_t GetMultiplier() const { return multiplier_; }
  constexpr uint32_t Apply(uint32_t numerator) const {
    return ApplyUint32Div(numerator, multiplier_, extradata_);
  }
};
}  // namespace divisors
}

namespace rex {
template <typename T>
inline T roundToNearestOrderOfMagnitude(T value) {
  if (!value) {
    return value;
  }

  const double order = std::pow(10, std::floor(std::log10(std::fabs(value))));
  const double rounded = std::round(value / order) * order;

  return static_cast<T>(rounded);
}

}

// XeniOS non-x86 math helpers.
namespace rex {
static float ArchMin(float x, float y) { return std::min<float>(x, y); }
static float ArchMax(float x, float y) { return std::max<float>(x, y); }
static float ArchReciprocal(float den) { return 1.0f / den; }
using ArchFloatMask = unsigned;

XE_FORCEINLINE
static ArchFloatMask ArchCmpneqFloatMask(float x, float y) {
  return static_cast<unsigned>(-static_cast<signed>(x != y));
}

XE_FORCEINLINE
static ArchFloatMask ArchORFloatMask(ArchFloatMask x, ArchFloatMask y) {
  return x | y;
}
XE_FORCEINLINE
static ArchFloatMask ArchXORFloatMask(ArchFloatMask x, ArchFloatMask y) {
  return x ^ y;
}

XE_FORCEINLINE
static ArchFloatMask ArchANDFloatMask(ArchFloatMask x, ArchFloatMask y) {
  return x & y;
}
constexpr ArchFloatMask floatmask_zero = 0;

XE_FORCEINLINE
static uint32_t ArchFloatMaskSignbit(ArchFloatMask x) { return x >> 31; }


XE_FORCEINLINE
static float RefineReciprocal(float initial, float den) {
  float t0 = initial * den;
  float t1 = t0 * initial;
  float rcp2 = initial + initial;
  return rcp2 - t1;
}
XE_FORCEINLINE
static float ArchReciprocalRefined(float den) {
  return RefineReciprocal(ArchReciprocal(den), den);
}

}
