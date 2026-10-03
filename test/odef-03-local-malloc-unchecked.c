// BUG 03: buffers allocated in the same function are never checked by default.
//
// Where: OverflowDefense.cpp, tryRuntimeFreeCheck() / commitBuiltInCheck().
//
// Every source whose size ObjectSizeOffsetEvaluator can compute is turned
// into a BuiltinCheck. That includes malloc(n), calloc, new[] and realloc
// calls, even when n is not a constant. Such a source never gets a
// shadow-memory check. But commitBuiltInCheck() returns immediately unless
// -odef-check-stack is set, and that flag is off by default. So
// `p = malloc(n); p[i] = 0;` is emitted without any bounds check.
//
// The builtin check also computes the object end as `Src + Size` instead of
// `Src - Offset + Size`.
//
// Expected: local() checks p[i].
//
// RUN: %odef_ir %s 2>/dev/null | FileCheck %s

#include <stdlib.h>

void sink(char *);

// CHECK-LABEL: @local(
// CHECK: call void @__shadowbound_abort()
// CHECK: store i8 1
void local(long n, long i) {
  char *p = malloc(n);
  p[i] = 1;
  sink(p);
}

// CHECK-LABEL: @odef.module_ctor(
