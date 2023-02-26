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

static const uptr kReservedBytes = sizeof(uptr);
static const uptr kAllocatorSpace = 0x600000000000ULL;
static const uptr kMaxAllowedMallocSize = 8UL << 30;

struct AP64 { // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = 0x20000000000; // 2T.
  static const uptr kMetadataSize = 0;
  typedef DefaultSizeClassMap SizeClassMap;
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

static uptr max_malloc_size;

void OdefAllocatorInit() {
  SetAllocatorMayReturnNull(common_flags()->allocator_may_return_null);
  allocator.Init(common_flags()->allocator_release_to_os_interval_ms);
  if (common_flags()->max_allocation_size_mb)
    max_malloc_size = Min(common_flags()->max_allocation_size_mb << 20,
                          kMaxAllowedMallocSize);
  else
    max_malloc_size = kMaxAllowedMallocSize;
}

AllocatorCache *GetAllocatorCache(OdefThreadLocalMallocStorage *ms) {
  return reinterpret_cast<AllocatorCache *>(ms->allocator_cache);
}

void OdefThreadLocalMallocStorage::CommitBack() {
  allocator.SwallowCache(GetAllocatorCache(this));
}

static void *OdefAllocate(uptr size, uptr alignment) {
  // FIXME: CHECK size is larger than max_malloc_size.
  // FIXME: CHECK if RSS limit is exceeded.

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
  SetShadow(allocated, size);
  RunMallocHooks(allocated, size);
  return allocated;
}

void OdefDeallocate(void *p) {
  RunFreeHooks(p);

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

static void *OdefCalloc(uptr nmemb, uptr size) {
  uptr bytes = nmemb * size;
  void *p = OdefAllocate(bytes, sizeof(u64));
  if (p)
    internal_memset(p, 0, bytes);
  return p;
}

void *odef_malloc(uptr size) {
  size += kReservedBytes;
  return OdefAllocate(size, sizeof(u64));
}

// TODO: Increasing the `nmemb` amy should be moved to the Instrumentation.
void *odef_calloc(uptr nmemb, uptr size) {
  nmemb += 1;
  return OdefCalloc(nmemb, size);
}

void *odef_realloc(void *p, uptr size) {
  size += kReservedBytes;

  if (!p)
    return OdefAllocate(size, sizeof(u64));
  if (!size) {
    OdefDeallocate(p);
    return nullptr;
  }
  return OdefReallocate(p, size, sizeof(u64));
}

void *odef_reallocarray(void *p, uptr nmemb, uptr size) {
  nmemb += 1;
  return odef_realloc(p, nmemb * size);
}

void *odef_valloc(uptr size) {
  size += kReservedBytes;
  return OdefAllocate(size, GetPageSizeCached());
}

void *odef_pvalloc(uptr size) {
  uptr PageSize = GetPageSizeCached();

  size += kReservedBytes;
  size = size ? RoundUpTo(size, PageSize) : PageSize;
  return OdefAllocate(size, PageSize);
}

void *odef_aligned_alloc(uptr alignment, uptr size) {
  size += kReservedBytes;
  return OdefAllocate(size, alignment);
}

void *odef_memalign(uptr alignment, uptr size) {
  size += kReservedBytes;
  return OdefAllocate(size, alignment);
}

uptr odef_allocated_size(void *p) {
  // FIXME: May me we need return requested size instead of actually allocated
  return allocator.GetActuallyAllocatedSize(p) - kReservedBytes;
}

int odef_posix_memalign(void **memptr, uptr alignment, uptr size) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(alignment))) {
    return errno_EINVAL;
  }
  size += kReservedBytes;
  void *p = OdefAllocate(size, alignment);
  if (!p)
    return errno_ENOMEM;
  *memptr = p;
  return 0;
}

} // namespace __odef

using namespace __odef;