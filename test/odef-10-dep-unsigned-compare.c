// BUG 10: redundant-check elimination compares signed indices as unsigned.
//
// Where: OverflowDefense.cpp, dependencyOptimizeForGep(), the non-constant
//        branch (`getUnsignedRangeMin(J).ult(getUnsignedRangeMax(I))`).
//
// To decide that I's offset never exceeds J's, the code compares the
// UNSIGNED ranges of the raw indices. For negative indices this is wrong.
// With I's index in [-4, -1] and J's index -1, the unsigned min of J
// (2^64-1) is not below the unsigned max of I (2^64-1), so I counts as
// covered by J, even though p-4 is further below the object than p-1.
//
// Expected: the load of p[k] keeps its own check.
//
// RUN: %odef_ir %s 2>/dev/null | FileCheck %s

// CHECK-LABEL: @g(
// CHECK: call void @__odef_abort()
// CHECK: load i8
// CHECK: call void @__odef_abort()
// CHECK: load i8
char g(char *p, unsigned x) {
  char a = p[-1];
  long k = -(long)(x & 3) - 1;
  return a + p[k];
}

// CHECK-LABEL: @odef.module_ctor(
