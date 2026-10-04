// Every heap object has a 32-byte reserve in front of it, like the one after
// it, inside its bounds.
//
// Checks run when a pointer is created. Programs routinely form a pointer just
// before an object without dereferencing it there: nginx rebases parser
// pointers with `new + (field - old)` where a field sits 2 bytes before `old`,
// and backward scans compute `start - 1`. Without a front reserve each of these
// aborted (27 nginx-tests files). A pointer further below than the reserve is
// still reported, and the allocator paths that map an object back to its
// chunk (free, realloc, malloc_usable_size, aligned allocation) keep working.
//
// RUN: %clang_sb %s -o %t
// RUN: %t inbounds 2>&1 | FileCheck %s --check-prefix=INB
// RUN: not %t underflow 2>&1 | FileCheck %s --check-prefix=UNF
// RUN: %clang_sb -flto -fuse-ld=lld %s -o %t.lto
// RUN: %t.lto inbounds 2>&1 | FileCheck %s --check-prefix=INB
// RUN: not %t.lto underflow 2>&1 | FileCheck %s --check-prefix=UNF

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *volatile g_ptr;
volatile long g_off;

// Store a pointer `off` bytes before `p` (an escape, so it is checked).
__attribute__((noinline)) void rebase(char *p, long off) { g_ptr = p - off; }

int main(int argc, char **argv) {
  char *p = malloc(100);
  memset(p, 'x', 100);

  if (argc > 1 && strcmp(argv[1], "underflow") == 0) {
    // UNF: Overflow detected
    // UNF-NOT: UNDETECTED
    g_off = 4096;
    rebase(p, g_off);
    printf("UNDETECTED\n");
    return 0;
  }

  for (long off = 1; off <= 32; off++) {
    g_off = off;
    rebase(p, g_off);
  }

  // The chunk is still found from the object pointer.
  if (malloc_usable_size(p) < 100)
    return 1;
  p = realloc(p, 10000);
  for (int i = 0; i < 100; i++)
    if (p[i] != 'x')
      return 2;
  free(p);

  void *a = NULL;
  if (posix_memalign(&a, 64, 200) || ((uintptr_t)a & 63))
    return 3;
  free(a);
  void *b = aligned_alloc(4096, 4096);
  if (!b || ((uintptr_t)b & 4095))
    return 4;
  free(b);
  for (int i = 0; i < 100000; i++)
    free(malloc(i % 512));

  // INB: inbounds-ok
  printf("inbounds-ok\n");
  return 0;
}
