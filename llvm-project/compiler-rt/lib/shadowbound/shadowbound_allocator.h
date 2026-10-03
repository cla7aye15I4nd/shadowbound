#ifndef SHADOWBOUND_ALLOCATOR_H
#define SHADOWBOUND_ALLOCATOR_H

#include "shadowbound.h"

#include "sanitizer_common/sanitizer_common.h"

namespace __shadowbound {

struct ShadowBoundThreadLocalMallocStorage {
  // Allocator cache contains atomic_uint64_t which must be 8-byte aligned.
  ALIGNED(8) uptr allocator_cache[96 * (512 * 8 + 16)]; // Opaque.
  void CommitBack();

private:
  // These objects are allocated via mmap() and are zero-initialized.
  ShadowBoundThreadLocalMallocStorage() {}
};

} // namespace __shadowbound

#endif // SHADOWBOUND_ALLOCATOR_H