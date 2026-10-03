// BUG 04: isAccessMemberBoost() wraps its offset sum and adds elements instead of bytes.
//
// Where: OverflowDefense.cpp, isAccessMemberBoost() (HANDLE_GEP).
//
// A GEP off a fixed-size struct pointer is treated as safe when
// "maxOffset <= sizeof(struct)". Two things are wrong:
//   * getUnsignedMax() of the raw index is added into a 64-bit accumulator.
//     An unknown or negative i64 index has max 2^64-1, so the sum wraps to a
//     small value and the GEP is declared safe;
//   * the index itself is added, not index * element size, so
//     ((long *)s)[50] counts as offset 50 instead of 400.
//
// Expected: both functions keep their bounds check.
//
// RUN: %shadowbound_ir %s 2>/dev/null | FileCheck %s

struct S {
  char b[64];
};

// CHECK-LABEL: @write_at(
// CHECK: call void @__shadowbound_abort()
void write_at(struct S *s, long n) { ((char *)s)[n] = 1; }

// CHECK-LABEL: @read_scaled(
// CHECK: call void @__shadowbound_abort()
long read_scaled(struct S *s) { return ((long *)s)[50]; }

// CHECK-LABEL: @shadowbound.module_ctor(
