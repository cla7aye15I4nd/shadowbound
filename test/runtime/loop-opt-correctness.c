// Correctness of the monotonic-loop optimization: hoisting the per-iteration
// check to a single pre-loop bound check must NOT create a false negative — an
// out-of-bounds monotonic loop still has to be caught.
//
// RUN: %clang_sb -mllvm -shadowbound-loop-opt %s -o %t
// RUN: %t inbounds 2>&1 | FileCheck %s --check-prefix=INB
// RUN: not %t overflow 2>&1 | FileCheck %s --check-prefix=OOB

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *volatile g_sink;

__attribute__((noinline)) long sum(int *p, int n) {
  long s = 0;
  for (int i = 0; i < n; i++)
    s += p[i];
  return s;
}

int main(int argc, char **argv) {
  int n = 256;
  int *p = malloc(n * sizeof(int));
  memset(p, 0, n * sizeof(int));

  if (argc > 1 && strcmp(argv[1], "overflow") == 0) {
    // Read far past the end: the hoisted bound check must trap.
    g_sink = p;
    long r = sum(p, n + 4096);
    // INB-NOT: UNDETECTED
    printf("UNDETECTED %ld\n", r);
    return 0;
  }

  // In bounds: must run cleanly.
  long r = sum(p, n);
  // INB: inbounds-ok
  printf("inbounds-ok %ld\n", r);
  free(p);
  return 0;
}

// OOB: Overflow detected
