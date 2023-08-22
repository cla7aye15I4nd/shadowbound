#ifndef SHADOW_H
#define SHADOW_H

#include <sys/types.h>

typedef size_t uptr;
typedef unsigned int u32;
typedef unsigned long long u64;

#define MEM_TO_SHADOW(mem) (((uptr)(mem)) & ~0x400000000007ULL)
#define kReservedBytes (0x20)

#define MEM_IS_APP(mem) \
  (((uptr)(mem)) >= 0x600000000000ULL && ((uptr)(mem)) < 0x700000000000ULL)
#define MEM_IS_SHADOW(mem) \
  (((uptr)(mem)) >= 0x200000000000ULL && ((uptr)(mem)) < 0x300000000000ULL)

#define GC_shadow_init()                                           \
  do                                                               \
  {                                                                \
    uptr start = 0x200000000000ULL;                                \
    uptr end = 0x300000000000ULL;                                  \
    uptr size = end - start;                                       \
    int prot = PROT_READ | PROT_WRITE;                             \
    int flag = MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE | MAP_ANON; \
    mmap((void *)start, size, prot, flag, -1, 0);                  \
    madvise((void *)start, size, MADV_NOHUGEPAGE);                 \
  } while (0)

#define GC_set_shadow_normal(ptr, size)                  \
  do                                                     \
  {                                                      \
    if (!MEM_IS_APP(ptr))                                \
      break;                                             \
                                                         \
    u32 *shadow_beg = (u32 *)MEM_TO_SHADOW(ptr);         \
    u32 *shadow_end = shadow_beg + (size) / sizeof(u32); \
                                                         \
    u32 a = 0;                                           \
    u32 b = size;                                        \
                                                         \
    while (shadow_beg < shadow_end)                      \
    {                                                    \
      *(shadow_beg + 0) = b;                             \
      *(shadow_beg + 1) = a;                             \
      b -= sizeof(u64);                                  \
      a += sizeof(u64);                                  \
      shadow_beg += 2;                                   \
    }                                                    \
  } while (0)

#define GC_set_shadow_perf(ptr, size)             \
  do                                              \
  {                                               \
    if (!MEM_IS_APP(ptr))                         \
      break;                                      \
                                                  \
    memset((void *)MEM_TO_SHADOW(ptr), -1, size); \
  } while (0)

#if 0
#define GC_set_shadow(ptr, size) GC_set_shadow_normal(ptr, size)
#else
#define GC_set_shadow(ptr, size) GC_set_shadow_perf(ptr, size)
#endif

#define GC_unreachable()                                            \
  do                                                                \
  {                                                                 \
    fprintf(stderr, "GC_unreachable: %s:%d\n", __FILE__, __LINE__); \
    exit(1);                                                        \
  } while (0);

#endif // SHADOW_H