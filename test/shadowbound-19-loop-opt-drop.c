// BUG 19: -shadowbound-loop-opt removes the induction-pointer check without a replacement.
//
// Where: OverflowDefense.cpp, loopOptimize().
//
// For a recognised monotonic loop, the GEP that steps the induction pointer
// is dropped whenever the loop bound is not itself a GEP. The replacement
// check was meant to come from monotonicLoopOptimize(), but that function
// only prints debug output and returns false. So with -shadowbound-loop-opt every
// `for (q = p; q < end; q++) *q = ...` loop runs without a check.
//
// Expected: fill() is still checked when -shadowbound-loop-opt is given.
//
// RUN: %shadowbound_ir -mllvm -shadowbound-loop-opt %s 2>/dev/null | FileCheck %s

// CHECK-LABEL: @fill(
// CHECK: call void @__shadowbound_abort()
void fill(char *p, char *e) {
  for (char *q = p; q < e; q++)
    *(volatile char *)q = 0;
}

// CHECK-LABEL: @shadowbound.module_ctor(
