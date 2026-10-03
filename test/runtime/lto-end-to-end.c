// End to end under LTO: a program built with -flto is instrumented at link
// time and still catches heap overflows, including through a function whose
// other callers only pass stack buffers (those callers must not hide the heap
// call site from the interprocedural analysis).
//
// RUN: %clang_sb -flto -fuse-ld=lld %s -o %t
// RUN: %t inbounds 2>&1 | FileCheck %s --check-prefix=INB
// RUN: not %t overflow 2>&1 | FileCheck %s --check-prefix=OOB
//
// Same with ThinLTO.
// RUN: %clang_sb -flto=thin -fuse-ld=lld %s -o %t.thin
// RUN: %t.thin inbounds 2>&1 | FileCheck %s --check-prefix=INB
// RUN: not %t.thin overflow 2>&1 | FileCheck %s --check-prefix=OOB

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *volatile g_sink;

__attribute__((noinline)) void put(char *p, long i) { p[i] = 1; }

int main(int argc, char **argv) {
  char stack[64];
  g_sink = stack;
  put(stack, 3);

  long n = 64;
  char *h = malloc(n);
  g_sink = h;
  int overflow = argc > 1 && strcmp(argv[1], "overflow") == 0;
  put(h, overflow ? n + 512 : n - 1);
  // INB-NOT: UNDETECTED
  // INB: inbounds-ok
  printf(overflow ? "UNDETECTED\n" : "inbounds-ok\n");
  return 0;
}

// OOB: Overflow detected
