#pragma once
// ReXGlue has no Xenia MicroProfile runtime. This affects profiling only.
#define SCOPE_profile_cpu_f(...) ((void)0)
#define SCOPE_profile_cpu_i(...) ((void)0)
#define COUNT_profile(...) ((void)0)

#define COUNT_profile_set(...) ((void)0)
#define COUNT_profile_add(...) ((void)0)
