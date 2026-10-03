// BUG 13: the offset direction is derived from the index sign, not the byte offset.
//
// Where: OverflowDefense.cpp, setOffsetDir().
//
// A GEP is classified "positive only" (upper-bound check only) when each
// index is non-negative. But index * element size can wrap. Here n is
// provably in [0, 2^63), yet 8 * n wraps to any value, e.g. n = 2^61 - 1
// yields p - 8. Only the upper bound is checked, so the underflow is missed.
//
// Expected: get() checks both the lower and the upper bound.
//
// RUN: %odef_ir %s 2>/dev/null | FileCheck %s

// CHECK-LABEL: @get(
// Both bounds are checked: the scaled byte offset may wrap, so the direction
// is "both", producing a lower-bound compare and a reserve-adjusted upper one.
// CHECK-DAG: icmp ugt i64 %{{[0-9]+}}, %{{[0-9]+}}
// CHECK-DAG: add i64 %{{[0-9]+}}, 32
// CHECK: call void @__odef_abort()
long get(long *p, unsigned long x) {
  long n = (long)(x & 0x7fffffffffffffffUL);
  return p[n];
}

// CHECK-LABEL: @odef.module_ctor(
