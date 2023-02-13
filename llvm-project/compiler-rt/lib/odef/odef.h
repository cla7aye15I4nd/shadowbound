#ifndef ODEF_H
#define ODEF_H

#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
using namespace __sanitizer;

struct MappingDesc {
  uptr start;
  uptr end;
  enum Type { INVALID, APP, SHADOW } type;
  const char *name;
};

const MappingDesc kMemoryLayout[] = {
    {0x000000000000ULL, 0x200000000000ULL, MappingDesc::INVALID, "invalid-1"},
    {0x200000000000ULL, 0x400000000000ULL, MappingDesc::SHADOW, "shadow"},
    {0x400000000000ULL, 0x600000000000ULL, MappingDesc::INVALID, "invalid-2"},
    {0x600000000000ULL, 0x800000000000ULL, MappingDesc::APP, "app"}};

#define MEM_TO_SHADOW(mem) (((uptr)(mem)) & ~0x400000000007ULL)

const uptr kMemoryLayoutSize = sizeof(kMemoryLayout) / sizeof(kMemoryLayout[0]);

#ifndef __clang__
__attribute__((optimize("unroll-loops")))
#endif
inline bool
addr_is_type(uptr addr, MappingDesc::Type mapping_type) {
// It is critical for performance that this loop is unrolled (because then it is
// simplified into just a few constant comparisons).
#ifdef __clang__
#pragma unroll
#endif
  for (unsigned i = 0; i < kMemoryLayoutSize; ++i)
    if (kMemoryLayout[i].type == mapping_type &&
        addr >= kMemoryLayout[i].start && addr < kMemoryLayout[i].end)
      return true;
  return false;
}

#define MEM_IS_APP(mem) addr_is_type((uptr)(mem), MappingDesc::APP)
#define MEM_IS_SHADOW(mem) addr_is_type((uptr)(mem), MappingDesc::SHADOW)

namespace __odef {

void *odef_malloc(uptr size);
void *odef_calloc(uptr nmemb, uptr size);
void *odef_realloc(void *ptr, uptr size);
void *odef_reallocarray(void *ptr, uptr nmemb, uptr size);
void *odef_valloc(uptr size);
void *odef_pvalloc(uptr size);
void *odef_aligned_alloc(uptr alignment, uptr size);
void *odef_memalign(uptr alignment, uptr size);
int odef_posix_memalign(void **memptr, uptr alignment, uptr size);

} // namespace __odef

#endif // ODEF_H