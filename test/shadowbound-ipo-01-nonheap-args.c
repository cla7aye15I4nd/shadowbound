// IPO: checks on a pointer that can never be a heap pointer are dropped.
//
// Where: ShadowBound.cpp, ShadowBoundIPOAnalysis and ipoOptimize().
//
// Bounds live only in the heap's shadow; a check on a stack or global pointer
// is skipped at run time. The shadowbound-ipo analysis proves statically that
// an argument is never a heap pointer when every caller is visible (local
// linkage, address not taken) and passes a non-heap value, and that a function
// never returns a heap pointer. Such checks are removed at compile time. This
// replaces the "funarg" patterns that analyzer/ produced out of tree.
//
// RUN: %shadowbound_ir %s 2>/dev/null | FileCheck %s
// RUN: %shadowbound_ir -mllvm -shadowbound-nonheap-opt=0 %s 2>/dev/null | FileCheck %s --check-prefix=OFF

#include <stdlib.h>

void sink(void *);
char gbuf[256];

// Only ever called with stack buffers: no check.
static __attribute__((noinline)) void stack_only(char *p, long i) { p[i] = 1; }

// Reached through another static function that forwards its own non-heap
// argument, and recursively: still provably non-heap.
static __attribute__((noinline)) void forwarded(char *p, long i) {
  if (i > 0)
    forwarded(p, i - 1);
  p[i] = 2;
}
static __attribute__((noinline)) void forwarder(char *p, long i) {
  forwarded(p + 1, i);
}

// Called with a heap pointer at one call site: must keep its check.
static __attribute__((noinline)) void mixed(char *p, long i) { p[i] = 3; }

// Address taken: unknown callers, must keep its check.
static __attribute__((noinline)) void escaped(char *p, long i) { p[i] = 4; }

// Exported: unknown callers without LTO, must keep its check.
__attribute__((noinline)) void exported(char *p, long i) { p[i] = 5; }

// Returns only globals: the call result is non-heap.
static __attribute__((noinline)) char *pick(int c) {
  return c ? gbuf : gbuf + 128;
}
void use_pick(int c, long i) { pick(c)[i] = 6; }

void entry(long i) {
  char a[64], b[64];
  sink(a);
  sink(b);
  stack_only(a, i);
  stack_only(gbuf, i);
  forwarder(b, i);
  mixed(a, i);
  char *h = malloc(64);
  sink(h);
  mixed(h, i);
  sink((void *)escaped);
  exported(a, i);
}

// Functions are emitted in this order.
//
// Exported: unknown callers without LTO, must keep its check.
// CHECK-LABEL: define {{.*}}@exported(
// CHECK: call void @__shadowbound_abort()
//
// pick() returns only globals, so its call result is non-heap.
// CHECK-LABEL: define {{.*}}@use_pick(
// CHECK-NOT: call void @__shadowbound_abort()
// CHECK: ret void
//
// Only ever called with stack/global buffers: no check.
// CHECK-LABEL: define internal {{.*}}@stack_only(
// CHECK-NOT: call void @__shadowbound_abort()
// CHECK: ret void
//
// Called with a heap pointer at one call site: must keep its check.
// CHECK-LABEL: define internal {{.*}}@mixed(
// CHECK: call void @__shadowbound_abort()
//
// Address taken: unknown callers, must keep its check.
// CHECK-LABEL: define internal {{.*}}@escaped(
// CHECK: call void @__shadowbound_abort()
//
// Reached only through forwarder() and itself: still non-heap.
// CHECK-LABEL: define internal {{.*}}@forwarded(
// CHECK-NOT: call void @__shadowbound_abort()
// CHECK: ret void
//
// With the optimization off, stack_only() keeps its check.
// OFF-LABEL: define internal {{.*}}@stack_only(
// OFF: call void @__shadowbound_abort()
