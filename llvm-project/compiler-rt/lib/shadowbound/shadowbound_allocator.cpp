#include "shadowbound_allocator.h"
#include "shadowbound.h"
#include "shadowbound_interceptors.h"
#include "shadowbound_thread.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_libc.h"
namespace __shadowbound {

struct ShadowBoundMapUnmapCallback {
  void OnMap(uptr p, uptr size) const {}
  void OnUnmap(uptr p, uptr size) const {
    uptr shadow_p = MEM_TO_SHADOW(p);
    ReleaseMemoryPagesToOS(shadow_p, shadow_p + size);
  }
};

// Every heap object gets kReservedBytes after it and kFrontReserveBytes before
// it, both inside its chunk and so inside its shadow bounds. Checks run when a
// pointer is created, not when it is dereferenced; the reserves let programs
// form pointers just past the end (one-past-the-end, `p += len` loops) or just
// before the start (`p - 1` scans, `new + (field - old)` rebasing in nginx) of
// an object without a report, while such a pointer can never reach another
// object.
static const uptr kReservedBytes = 0x20;
static const uptr kFrontReserveBytes = 0x20;
static const uptr kAllocatorSpace = 0x600000000000ULL;
static const uptr kMaxAllowedMallocSize = 8UL << 30;
static const s32 kAllocatorReleaseToOsIntervalMs = 5000;

struct AP64 { // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = 0x20000000000; // 2T.
  static const uptr kMetadataSize = 0;
  typedef LargeSizeClassMap SizeClassMap;
  typedef ShadowBoundMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = LocalAddressSpaceView;
};

typedef SizeClassAllocator64<AP64> PrimaryAllocator;
typedef CombinedAllocator<PrimaryAllocator> Allocator;
typedef Allocator::AllocatorCache AllocatorCache;

static Allocator allocator;
static AllocatorCache fallback_allocator_cache;
static StaticSpinMutex fallback_mutex;

void ShadowBoundAllocatorInit() { allocator.Init(kAllocatorReleaseToOsIntervalMs); }

AllocatorCache *GetAllocatorCache(ShadowBoundThreadLocalMallocStorage *ms) {
  return reinterpret_cast<AllocatorCache *>(ms->allocator_cache);
}

void ShadowBoundThreadLocalMallocStorage::CommitBack() {
  allocator.SwallowCache(GetAllocatorCache(this));
}

// Add the reserved bytes without wrapping: a request that would overflow is
// clamped to a value above the max so ShadowBoundAllocate rejects it (and returns
// NULL) rather than silently allocating a tiny buffer.
static uptr AddReserve(uptr size) {
  if (size > (uptr)-1 - kReservedBytes)
    return (uptr)-1;
  return size + kReservedBytes;
}

// The front reserve, rounded up so the returned pointer keeps the requested
// alignment (alignments are powers of two).
static uptr FrontPad(uptr alignment) {
  return alignment > kFrontReserveBytes ? alignment : kFrontReserveBytes;
}

// The chunk an object pointer returned by ShadowBoundAllocate lives in.
static void *ChunkOf(void *p) {
  if (!p || !allocator.PointerIsMine(p))
    return p;
  return allocator.GetBlockBegin(p);
}

// Bytes from p to the end of its chunk.
static uptr ChunkBytesFrom(void *p) {
  void *chunk = ChunkOf(p);
  return (uptr)chunk + allocator.GetActuallyAllocatedSize(chunk) - (uptr)p;
}

static void *ShadowBoundAllocate(uptr size, uptr alignment) {
  uptr pad = FrontPad(alignment);
  if (size > kMaxAllowedMallocSize || size + pad > kMaxAllowedMallocSize) {
    // Too large: return NULL (ENOMEM) instead of aborting, so callers that
    // probe large sizes behave normally.
    errno = errno_ENOMEM;
    return nullptr;
  }

  ShadowBoundThread *t = GetCurrentThread();
  void *allocated;
  if (t) {
    AllocatorCache *cache = GetAllocatorCache(&t->malloc_storage());
    allocated = allocator.Allocate(cache, size + pad, alignment);
  } else {
    SpinMutexLock l(&fallback_mutex);
    AllocatorCache *cache = &fallback_allocator_cache;
    allocated = allocator.Allocate(cache, size + pad, alignment);
  }
  if (!allocated) {
    errno = errno_ENOMEM;
    return nullptr;
  }
  // The shadow covers the whole chunk, front reserve included, so the
  // object's lower bound is kFrontReserveBytes (or the alignment) before the
  // pointer handed out.
  SetShadow(allocated, allocator.GetActuallyAllocatedSize(allocated));
  return (char *)allocated + pad;
}

void ShadowBoundDeallocate(void *p) {
  p = ChunkOf(p);

  ShadowBoundThread *t = GetCurrentThread();
  if (t) {
    AllocatorCache *cache = GetAllocatorCache(&t->malloc_storage());
    allocator.Deallocate(cache, p);
  } else {
    SpinMutexLock l(&fallback_mutex);
    AllocatorCache *cache = &fallback_allocator_cache;
    allocator.Deallocate(cache, p);
  }
}

static void *ShadowBoundReallocate(void *old_p, uptr new_size, uptr alignment) {
  uptr old_size = ChunkBytesFrom(old_p);
  if (new_size <= old_size) {
    return old_p;
  }
  uptr memcpy_size = Min(new_size, old_size);
  void *new_p = ShadowBoundAllocate(new_size, alignment);
  if (new_p) {
    internal_memcpy(new_p, old_p, memcpy_size);
    ShadowBoundDeallocate(old_p);
  }
  return new_p;
}

void *shadowbound_malloc(uptr size) {
  return ShadowBoundAllocate(AddReserve(size), sizeof(u64));
}

void *shadowbound_calloc(uptr nmemb, uptr size) {
  // calloc(n, 0) and 0*size are legal; guard the multiplication against
  // overflow (a wrapped product would under-allocate).
  if (size != 0 && nmemb > ((uptr)-1 - kReservedBytes) / size) {
    errno = errno_ENOMEM;
    return nullptr;
  }
  uptr bytes = nmemb * size;
  void *p = ShadowBoundAllocate(bytes + kReservedBytes, sizeof(u64));
  if (p)
    internal_memset(p, 0, bytes);
  return p;
}

void *shadowbound_realloc(void *p, uptr size) {
  if (!size) {
    ShadowBoundDeallocate(p);
    return nullptr;
  }

  size = AddReserve(size);
  if (!p)
    return ShadowBoundAllocate(size, sizeof(u64));
  else
    return ShadowBoundReallocate(p, size, sizeof(u64));
}

void *shadowbound_reallocarray(void *p, uptr nmemb, uptr size) {
  if (size != 0 && nmemb > (uptr)-1 / size) {
    errno = errno_ENOMEM;
    return nullptr;
  }
  return shadowbound_realloc(p, nmemb * size);
}

void *shadowbound_valloc(uptr size) {
  return ShadowBoundAllocate(AddReserve(size), GetPageSizeCached());
}

void *shadowbound_pvalloc(uptr size) {
  uptr PageSize = GetPageSizeCached();
  return ShadowBoundAllocate(RoundUpTo(AddReserve(size), PageSize), PageSize);
}

void *shadowbound_aligned_alloc(uptr alignment, uptr size) {
  return ShadowBoundAllocate(AddReserve(size), alignment);
}

void *shadowbound_memalign(uptr alignment, uptr size) {
  return ShadowBoundAllocate(AddReserve(size), alignment);
}

uptr shadowbound_allocated_size(void *p) {
  if (!p)
    return 0;
  uptr n = ChunkBytesFrom(p);
  return n > kReservedBytes ? n - kReservedBytes : 0;
}

int shadowbound_posix_memalign(void **memptr, uptr alignment, uptr size) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(alignment))) {
    return errno_EINVAL;
  }
  void *p = ShadowBoundAllocate(AddReserve(size), alignment);
  if (!p)
    return errno_ENOMEM;
  *memptr = p;
  return 0;
}

} // namespace __shadowbound

using namespace __shadowbound;