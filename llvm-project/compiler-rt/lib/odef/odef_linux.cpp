#include "odef.h"
#include "odef_interface_internal.h"
#include "odef_thread.h"

#include <elf.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// Note that sometimes the program will check the global variable in the
// library, so we need to ensure that checking will not lead any errors.
static void HackDynLib() {
  static const int kMaxPathLength = 1024;

  FILE *f = fopen("/proc/self/maps", "r");

  char buf[kMaxPathLength + 100];
  char perm[5], dev[6];
  char mapname[kMaxPathLength];
  uptr begin, end, inode, foo;

  while (!feof(f)) {
    if (fgets(buf, sizeof(buf), f) == 0)
      break;

    sscanf(buf, "%lx-%lx %4s %lx %s %ld %s", &begin, &end, perm, &foo, dev,
           &inode, mapname);

    if (!MEM_IS_APP(begin))
      continue;

    char *last_slash = mapname + strlen(mapname);
    while (last_slash > mapname && *last_slash != '/')
      last_slash--;

    // FIXME: I need to hook all dynamic libraries
    last_slash[4] = '\0';
    if (strcmp(last_slash, "/lib") == 0) {
      u32 *shadow_beg = (u32 *)MEM_TO_SHADOW(begin);
      u32 *shadow_end = (u32 *)MEM_TO_SHADOW(end);
#ifdef __clang__
#pragma unroll
#endif
      while (shadow_beg < shadow_end)
        *shadow_beg++ = 1 << 30;
    }
  }

  fclose(f);
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
      DontDumpShadowMemory(start, size);
    }
    if (protect) {
      if (!CheckMemoryRangeAvailability(start, size))
        return false;
      if (!ProtectMemoryRange(start, size, kMemoryLayout[i].name))
        return false;
    }
  }

  // HackDynLib();

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