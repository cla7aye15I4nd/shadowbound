#include "odef_allocator.h"
#include "odef.h"
#include "odef_interceptors.h"
#include "odef_thread.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_libc.h"
namespace __shadowbound {

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

// Add the reserved bytes without wrapping: a request that would overflow is
// clamped to a value above the max so OdefAllocate rejects it (and returns
// NULL) rather than silently allocating a tiny buffer.
static uptr AddReserve(uptr size) {
  if (size > (uptr)-1 - kReservedBytes)
    return (uptr)-1;
  return size + kReservedBytes;
}

static void *OdefAllocate(uptr size, uptr alignment) {
  if (size > kMaxAllowedMallocSize) {
    // Too large: return NULL (ENOMEM) instead of aborting, so callers that
    // probe large sizes behave normally.
    errno = errno_ENOMEM;
    return nullptr;
  }

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
  if (!allocated) {
    errno = errno_ENOMEM;
    return nullptr;
  }
  SetShadow(allocated, allocator.GetActuallyAllocatedSize(allocated));
  return allocated;
}

void OdefDeallocate(void *p) {

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

static void *OdefReallocate(void *old_p, uptr new_size, uptr alignment) {
  uptr old_size = allocator.GetActuallyAllocatedSize(old_p);
  if (new_size <= old_size) {
    return old_p;
  }
  uptr memcpy_size = Min(new_size, old_size);
  void *new_p = OdefAllocate(new_size, alignment);
  if (new_p) {
    internal_memcpy(new_p, old_p, memcpy_size);
    OdefDeallocate(old_p);
  }
  return new_p;
}

void *odef_malloc(uptr size) {
  return OdefAllocate(AddReserve(size), sizeof(u64));
}

void *odef_calloc(uptr nmemb, uptr size) {
  // calloc(n, 0) and 0*size are legal; guard the multiplication against
  // overflow (a wrapped product would under-allocate).
  if (size != 0 && nmemb > ((uptr)-1 - kReservedBytes) / size) {
    errno = errno_ENOMEM;
    return nullptr;
  }
  uptr bytes = nmemb * size;
  void *p = OdefAllocate(bytes + kReservedBytes, sizeof(u64));
  if (p)
    internal_memset(p, 0, bytes);
  return p;
}

void *odef_realloc(void *p, uptr size) {
  if (!size) {
    OdefDeallocate(p);
    return nullptr;
  }

  size = AddReserve(size);
  if (!p)
    return OdefAllocate(size, sizeof(u64));
  else
    return OdefReallocate(p, size, sizeof(u64));
}

void *odef_reallocarray(void *p, uptr nmemb, uptr size) {
  if (size != 0 && nmemb > (uptr)-1 / size) {
    errno = errno_ENOMEM;
    return nullptr;
  }
  return odef_realloc(p, nmemb * size);
}

void *odef_valloc(uptr size) {
  return OdefAllocate(AddReserve(size), GetPageSizeCached());
}

void *odef_pvalloc(uptr size) {
  uptr PageSize = GetPageSizeCached();
  return OdefAllocate(RoundUpTo(AddReserve(size), PageSize), PageSize);
}

void *odef_aligned_alloc(uptr alignment, uptr size) {
  return OdefAllocate(AddReserve(size), alignment);
}

void *odef_memalign(uptr alignment, uptr size) {
  return OdefAllocate(AddReserve(size), alignment);
}

uptr odef_allocated_size(void *p) {
  if (!p)
    return 0;
  uptr n = allocator.GetActuallyAllocatedSize(p);
  return n > kReservedBytes ? n - kReservedBytes : 0;
}

int odef_posix_memalign(void **memptr, uptr alignment, uptr size) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(alignment))) {
    return errno_EINVAL;
  }
  void *p = OdefAllocate(AddReserve(size), alignment);
  if (!p)
    return errno_ENOMEM;
  *memptr = p;
  return 0;
}

} // namespace __shadowbound

using namespace __shadowbound;