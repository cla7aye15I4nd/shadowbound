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