#include "odef.h"
#include "odef_thread.h"

#include "interception/interception.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_dlsym.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_glibc_version.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_linux.h"

extern "C" SANITIZER_WEAK_ATTRIBUTE const int __shadowbound_only_small_alloc_opt;
extern "C" SANITIZER_WEAK_ATTRIBUTE const int __shadowbound_skip_instrument;
extern "C" SANITIZER_WEAK_ATTRIBUTE const int __shadowbound_perf_test;

namespace __shadowbound {

void SetShadow(const void *ptr, uptr size) {
  // Layout of the shadow memory:
  //  |BACK|FRON|....|BACK|FRON|BACK|FRON|....|BACK|FRON|
  // Layout Example for size = 0x100:
  // if __shadowbound_only_small_alloc_opt:
  //    |0x000|0x100|0x008|0x0f8|0x010|0x0f0|....|0x0f0|0x010|0x0f8|0x008|0x100|0x000|
  // else
  //    |0x00|0x20|0x01|0x1f|0x02|0x1e|....|0x1e|0x02|0x1f|0x01|0x20|0x00|

  if (__shadowbound_skip_instrument || !MEM_IS_APP(ptr))
    return;

  if (__shadowbound_perf_test) {
    internal_memset((void *)MEM_TO_SHADOW(ptr), u8(-1), size);
    return;
  }

  u32 *shadow_beg = (u32 *)MEM_TO_SHADOW(ptr);
  u32 *shadow_end = shadow_beg + size / sizeof(u32);

  if (__builtin_expect(__shadowbound_only_small_alloc_opt, 1)) {
    if (__builtin_expect(size > u32(-1), false)) {
      Report("ERROR: __shadowbound_only_small_alloc_opt is enabled, but the size "
             "of the allocation is too big: %zu\n",
             size);
      Die();
    }

    u32 a = 0;
    u32 b = size;

#ifdef __clang__
#pragma unroll
#endif
    while (shadow_beg < shadow_end) {
      *(shadow_beg + 0) = b;
      *(shadow_beg + 1) = a;
      shadow_beg += 2;
      b -= sizeof(u64);
      a += sizeof(u64);
    }
  } else {
    u32 a = 0;
    u32 b = size / sizeof(u32);

#ifdef __clang__
#pragma unroll
#endif
    while (shadow_beg < shadow_end) {
      *(shadow_beg + 0) = b--;
      *(shadow_beg + 1) = a++;
      shadow_beg += 2;
    }
  }
}

bool odef_inited = false;
bool odef_init_is_running = false;

} // namespace __shadowbound

using namespace __shadowbound;

// ShadowBound maps its shadow and its allocator at fixed addresses, which can
// collide with ASLR-placed mappings and cause nondeterministic startup crashes.
// If address-space randomization is on, turn it off and re-exec once so the
// fixed regions are always available.
extern "C" int personality(unsigned long persona);
static void DisableAslrIfNeeded() {
  const unsigned long kNoRandomize = 0x0040000; // ADDR_NO_RANDOMIZE
  int persona = personality(0xffffffffUL);
  if (persona == -1 || (persona & kNoRandomize))
    return; // cannot query, or already disabled.
  if (personality((unsigned long)persona | kNoRandomize) == -1)
    return; // not permitted (e.g. restricted container); avoid a re-exec loop.
  // Only re-exec if the change actually took effect, otherwise we would loop
  // forever re-executing with randomization still on.
  int now = personality(0xffffffffUL);
  if (now != -1 && (now & kNoRandomize))
    ReExec();
}

void __shadowbound_init() {
  if (odef_inited)
    return;

  if (odef_init_is_running)
    return;

  odef_init_is_running = true;

  DisableAslrIfNeeded();

  SetCommonFlagsDefaults();

  InitializeInterceptors();

  InitTlsSize();

  // Fail loudly if the shadow region could not be mapped. Ignoring this left
  // the shadow unmapped and caused nondeterministic crashes / false reports on
  // the first shadow access.
  if (!InitShadow()) {
    Report("ERROR: ShadowBound failed to map its shadow memory. The program "
           "must be built as PIE and the shadow region must be free.\n");
    Die();
  }

  OdefTSDInit(OdefTSDDtor);

  OdefAllocatorInit();

  OdefThread *main_thread = OdefThread::Create(nullptr, nullptr);
  SetCurrentThread(main_thread);
  main_thread->Init();

  odef_init_is_running = false;
  odef_inited = true;
}

void __shadowbound_report() {}

void __shadowbound_abort() {
  Report(" Overflow detected\n");
  Die();
}

void __shadowbound_set_shadow(uptr addr, uptr num, uptr size) {
  uptr real_size = (num * size + (sizeof(uptr) - 1)) & ~(sizeof(uptr) - 1);
  SetShadow((const void*) addr, real_size);
}