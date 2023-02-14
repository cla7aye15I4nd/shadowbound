#include "odef.h"
#include "odef_thread.h"
#include "odef_interface_internal.h"

#include <elf.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include <unwind.h>

namespace __odef {

static bool CheckMemoryRangeAvailability(uptr beg, uptr size) {
  if (size > 0) {
    uptr end = beg + size - 1;
    if (!MemoryRangeIsAvailable(beg, end)) {
      Printf("FATAL: Memory range 0x%zx - 0x%zx is not available.\n", beg, end);
      return false;
    }
  }
  return true;
}

static bool ProtectMemoryRange(uptr beg, uptr size, const char *name) {
  if (size > 0) {
    void *addr = MmapFixedNoAccess(beg, size, name);
    if (beg == 0 && addr) {
      // Depending on the kernel configuration, we may not be able to protect
      // the page at address zero.
      uptr gap = 16 * GetPageSizeCached();
      beg += gap;
      size -= gap;
      addr = MmapFixedNoAccess(beg, size, name);
    }
    if ((uptr)addr != beg) {
      uptr end = beg + size - 1;
      Printf("FATAL: Cannot protect memory range 0x%zx - 0x%zx (%s).\n", beg,
             end, name);
      return false;
    }
  }
  return true;
}

bool InitShadow() {
  const uptr maxVirtualAddress = GetMaxUserVirtualAddress();

  for (unsigned i = 0; i < kMemoryLayoutSize; ++i) {
    uptr start = kMemoryLayout[i].start;
    uptr end = kMemoryLayout[i].end;
    uptr size = end - start;
    MappingDesc::Type type = kMemoryLayout[i].type;

    // Check if the segment should be mapped based on platform constraints.
    if (start >= maxVirtualAddress)
      continue;

    bool map = type == MappingDesc::SHADOW;
    bool protect = type == MappingDesc::INVALID;
    CHECK(!(map && protect));
    if (!map && !protect)
      CHECK(type == MappingDesc::APP);
    if (map) {
      if (!CheckMemoryRangeAvailability(start, size))
        return false;
      if (!MmapFixedSuperNoReserve(start, size, kMemoryLayout[i].name))
        return false;
      if (common_flags()->use_madv_dontdump)
        DontDumpShadowMemory(start, size);
    }
    if (protect) {
      if (!CheckMemoryRangeAvailability(start, size))
        return false;
      if (!ProtectMemoryRange(start, size, kMemoryLayout[i].name))
        return false;
    }
  }

  return true;
}

static THREADLOCAL OdefThread *odef_current_thread;

static pthread_key_t tsd_key;
static bool tsd_key_inited = false;

void OdefTSDInit(void (*destructor)(void *tsd)) {
  CHECK(!tsd_key_inited);
  tsd_key_inited = true;
  CHECK_EQ(0, pthread_key_create(&tsd_key, destructor));
}

OdefThread *GetCurrentThread() { return odef_current_thread; }

void SetCurrentThread(OdefThread *t) {
  odef_current_thread = t;
  pthread_setspecific(tsd_key, (void *)t);
}

void OdefTSDDtor(void *tsd) {
  OdefThread *t = (OdefThread *)tsd;
  if (t->destructor_iterations_ > 1) {
    t->destructor_iterations_--;
    CHECK_EQ(0, pthread_setspecific(tsd_key, tsd));
    return;
  }
  odef_current_thread = nullptr;
  // Make sure that signal handler can not see a stale current thread pointer.
  atomic_signal_fence(memory_order_seq_cst);
  OdefThread::TSDDtor(tsd);
}

} // namespace __odef