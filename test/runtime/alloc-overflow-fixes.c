// Runtime PoC: the ShadowBound (odef) allocator must handle overflow, zero,
// and NULL edge cases instead of under-allocating or crashing.
//
// Bugs fixed (compiler-rt/lib/odef/odef_allocator.cpp):
//   - `size += kReservedBytes` wrapped for huge sizes, so malloc(SIZE_MAX)
//     returned a tiny buffer instead of NULL (AddReserve now saturates).
//   - calloc(nmemb, size) did `nmemb += (reserve+size-1)/size`, dividing by
//     zero for size==0 and overflowing the product (now overflow-checked).
//   - reallocarray did not check nmemb*size overflow.
//   - OdefAllocate called SetShadow on a NULL allocation on OOM, and oversize
//     requests aborted; both now return NULL with ENOMEM.
//   - malloc_usable_size(NULL) dereferenced a bad header; now returns 0.
//
// The results are routed through volatile sinks so the optimizer cannot delete
// the (unused) allocations and assume success.
//
// RUN: %clang_sb %s -o %t
// RUN: %t 2>&1 | FileCheck %s

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>

static void *volatile g_sink;

static int alloc_is_null(void *p) {
  g_sink = p; // observe the pointer so the call is not optimized away
  return g_sink == NULL;
}

int main(void) {
  // CHECK: malloc-huge: NULL
  printf("malloc-huge: %s\n", alloc_is_null(malloc(SIZE_MAX)) ? "NULL" : "PTR");

  // CHECK: calloc-zero: OK
  g_sink = calloc(16, 0);
  printf("calloc-zero: OK\n"); // reaching here means no SIGFPE

  // CHECK: calloc-overflow: NULL
  printf("calloc-overflow: %s\n",
         alloc_is_null(calloc((SIZE_MAX / 2) + 1, 4)) ? "NULL" : "PTR");

  // CHECK: reallocarray-overflow: NULL
  printf("reallocarray-overflow: %s\n",
         alloc_is_null(reallocarray(NULL, (SIZE_MAX / 2) + 1, 4)) ? "NULL" : "PTR");

  // CHECK: usable-null: 0
  printf("usable-null: %zu\n", malloc_usable_size(NULL));

  // CHECK: normal: ok
  char *p = malloc(100);
  if (!p || malloc_usable_size(p) < 100) {
    printf("normal: FAIL\n");
    return 1;
  }
  memset(p, 0x5a, 100);
  free(p);
  printf("normal: ok\n");

  // CHECK: DONE
  printf("DONE\n");
  return 0;
}
