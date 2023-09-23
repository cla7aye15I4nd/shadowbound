#include "odef_allocator.h"
#include "odef.h"
#include "odef_interceptors.h"
#include "odef_thread.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_libc.h"
namespace __odef {

struct OdefMapUnmapCallback {
  void OnMap(uptr p, uptr size) const {}
  void OnUnmap(uptr p, uptr size) const {
    uptr shadow_p = MEM_TO_SHADOW(p);
    ReleaseMemoryPagesToOS(shadow_p, shadow_p + size);
  }
};

static const uptr kReservedBytes = 0x20;
static const uptr kAllocatorSpace = 0x600000000000ULL;
static const uptr kMaxAllowedMallocSize = 8UL << 30;
static const s32 kAllocatorReleaseToOsIntervalMs = 5000;

struct AP64 { // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = 0x20000000000; // 2T.
  static const uptr kMetadataSize = 0;
  typedef LargeSizeClassMap SizeClassMap;
  typedef OdefMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = LocalAddressSpaceView;
};

typedef SizeClassAllocator64<AP64> PrimaryAllocator;
typedef CombinedAllocator<PrimaryAllocator> Allocator;
typedef Allocator::AllocatorCache AllocatorCache;

static Allocator allocator;
static AllocatorCache fallback_allocator_cache;
static StaticSpinMutex fallback_mutex;

void OdefAllocatorInit() { allocator.Init(kAllocatorReleaseToOsIntervalMs); }

AllocatorCache *GetAllocatorCache(OdefThreadLocalMallocStorage *ms) {
  return reinterpret_cast<AllocatorCache *>(ms->allocator_cache);
}

void OdefThreadLocalMallocStorage::CommitBack() {
  allocator.SwallowCache(GetAllocatorCache(this));
}

static void *OdefAllocate(uptr size, uptr alignment, void *caller) {
  if (size > kMaxAllowedMallocSize) {
    Report(" ERROR: odef_malloc(%zu) exceeds the maximum supported size "
           "of %zu\n",
           size, kMaxAllowedMallocSize);
    Die();
  }

  if (should_flush(caller))
    flush_frees();

  OdefThread *t = GetCurrentThread();
  void *allocated;
  if (t) {
    AllocatorCache *cache = GetAllocatorCache(&t->malloc_storage());
    allocated = allocator.Allocate(cache, size, alignment);
  } else {
    SpinMutexLock l(&fallback_mutex);
    AllocatorCache *cache = &fallback_allocator_cache;
    allocated = allocator.Allocate(cache, size, alignment);
  }
  // FIXME: CHECK if out of memory.
  SetShadow(allocated, allocator.GetActuallyAllocatedSize(allocated));
  return allocated;
}

void OdefDeallocate(void *p) {
  if (UNLIKELY(!p))
    return;
  OdefThread *t = GetCurrentThread();
  if (t) {
    AllocatorCache *cache = GetAllocatorCache(&t->malloc_storage());
    allocator.Deallocate(cache, p);
  } else {
    SpinMutexLock l(&fallback_mutex);
    AllocatorCache *cache = &fallback_allocator_cache;
    allocator.Deallocate(cache, p);
  }
}

static void *OdefReallocate(void *old_p, uptr new_size, uptr alignment, void *caller) {
  uptr old_size = allocator.GetActuallyAllocatedSize(old_p);
  if (new_size <= old_size) {
    return old_p;
  }
  uptr memcpy_size = Min(new_size, old_size);
  void *new_p = OdefAllocate(new_size, alignment, caller);
  if (new_p) {
    internal_memcpy(new_p, old_p, memcpy_size);
    queue_free(old_p);
  }
  return new_p;
}

static void *OdefCalloc(uptr nmemb, uptr size, void *caller) {
  uptr bytes = nmemb * size;
  void *p = OdefAllocate(bytes, sizeof(u64), caller);
  if (p)
    internal_memset(p, 0, bytes);
  return p;
}

void *odef_malloc(uptr size, void *caller) {
  size += kReservedBytes;
  return OdefAllocate(size, sizeof(u64), caller);
}

// TODO: Increasing the `nmemb` amy should be moved to the Instrumentation.
void *odef_calloc(uptr nmemb, uptr size, void *caller) {
  nmemb += (kReservedBytes + size - 1) / size;
  return OdefCalloc(nmemb, size, caller);
}

void *odef_realloc(void *p, uptr size, void *caller) {
  if (!size) {
    queue_free(p);
    return nullptr;
  }

  size += kReservedBytes;
  if (!p)
    return OdefAllocate(size, sizeof(u64), caller);
  else
    return OdefReallocate(p, size, sizeof(u64), caller);
}

void *odef_reallocarray(void *p, uptr nmemb, uptr size, void *caller) {
  return odef_realloc(p, nmemb * size, caller);
}

void *odef_valloc(uptr size, void *caller) {
  size += kReservedBytes;
  return OdefAllocate(size, GetPageSizeCached(), caller);
}

void *odef_pvalloc(uptr size, void *caller) {
  uptr PageSize = GetPageSizeCached();

  size = RoundUpTo(size + kReservedBytes, PageSize);
  return OdefAllocate(size, PageSize, caller);
}

void *odef_aligned_alloc(uptr alignment, uptr size, void *caller) {
  size += kReservedBytes;
  return OdefAllocate(size, alignment, caller);
}

void *odef_memalign(uptr alignment, uptr size, void *caller) {
  size += kReservedBytes;
  return OdefAllocate(size, alignment, caller);
}

uptr odef_allocated_size(void *p) {
  return allocator.GetActuallyAllocatedSize(p) - kReservedBytes;
}

int odef_posix_memalign(void **memptr, uptr alignment, uptr size, void *caller) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(alignment))) {
    return errno_EINVAL;
  }
  size += kReservedBytes;
  void *p = OdefAllocate(size, alignment, caller);
  if (!p)
    return errno_ENOMEM;
  *memptr = p;
  return 0;
}

} // namespace __odef

using namespace __odef;