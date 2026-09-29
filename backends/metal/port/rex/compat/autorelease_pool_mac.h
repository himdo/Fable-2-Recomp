#pragma once
#include <objc/objc.h>
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void*);
namespace rex {
class ScopedAutoreleasePool {
 public:
  explicit ScopedAutoreleasePool(const char*) : pool_(objc_autoreleasePoolPush()) {}
  ~ScopedAutoreleasePool() { objc_autoreleasePoolPop(pool_); }
  ScopedAutoreleasePool(const ScopedAutoreleasePool&) = delete;
  ScopedAutoreleasePool& operator=(const ScopedAutoreleasePool&) = delete;
 private:
  void* pool_;
};
}
#define XE_SCOPED_AUTORELEASE_POOL(name) rex::ScopedAutoreleasePool _metal_autorelease_pool(name)
