#ifndef ODEF_ALLOCATOR_H
#define ODEF_ALLOCATOR_H

#include "odef.h"

#include "sanitizer_common/sanitizer_common.h"

namespace __odef {

struct OdefThreadLocalMallocStorage {
  // Allocator cache contains atomic_uint64_t which must be 8-byte aligned.
  ALIGNED(8) uptr allocator_cache[96 * (512 * 8 + 16)]; // Opaque.
  void CommitBack();

private:
  // These objects are allocated via mmap() and are zero-initialized.
  OdefThreadLocalMallocStorage() {}
};

} // namespace __odef

#endif // ODEF_ALLOCATOR_H