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

namespace __odef {

void SetShadow(const void *ptr, uptr size) {
  u32 *shadow_beg = (u32 *)MEM_TO_SHADOW(ptr);
  u32 *shadow_end = shadow_beg + size / sizeof(u32);

  u32 a = 0;
  u32 b = size / sizeof(uptr);

#ifdef __clang__
#pragma unroll
#endif
  while (shadow_beg < shadow_end) {
    *(shadow_beg + 0) = b--;
    *(shadow_beg + 1) = a++;
    shadow_beg += 2;
  }
}

bool odef_inited = false;
bool odef_init_is_running = false;

} // namespace __odef

using namespace __odef;

void __odef_init() {
  if (odef_inited)
    return;

  if (odef_init_is_running)
    return;

  odef_init_is_running = true;

  InitializeInterceptors();

  InitShadow();

  OdefTSDInit(OdefTSDDtor);

  OdefAllocatorInit();

  OdefThread *main_thread = OdefThread::Create(nullptr, nullptr);
  SetCurrentThread(main_thread);
  main_thread->Init();

  odef_init_is_running = false;
  odef_inited = true;
}

void __odef_report() {
  Report(" Overflow detected\n");
}

void __odef_abort() {
  __odef_report();
  Die();
}