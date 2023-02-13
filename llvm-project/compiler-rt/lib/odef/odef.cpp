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

int odef_inited = 0;
bool odef_init_is_running;

void __odef_init() {
  if (odef_inited)
    return;

  if (odef_init_is_running)
    return;

  InitializeInterceptors();

  InitShadow();

  OdefTSDInit(OdefTSDDtor);

  OdefAllocatorInit();

  OdefThread *main_thread = OdefThread::Create(nullptr, nullptr);
  SetCurrentThread(main_thread);
  main_thread->Init();

  odef_init_is_running = 0;
  odef_inited = 1;
}

} // namespace __odef
