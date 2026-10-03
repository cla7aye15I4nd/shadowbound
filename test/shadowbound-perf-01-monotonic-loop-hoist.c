// PERF: monotonic-loop check hoisting (-shadowbound-loop-opt).
//
// For an affine induction pointer in a counted loop, monotonicLoopOptimize
// replaces the per-iteration bounds check with a single pre-loop check of the
// first and last accessed addresses. This is sound: the accessed addresses are
// monotonic, so bounding the extremes bounds every iteration.
//
// RUN: %clang -fsanitize=shadowbound -O1 -fno-vectorize -S -emit-llvm -o - -mllvm -shadowbound-loop-opt %s 2>/dev/null | FileCheck %s --check-prefix=OPT
// RUN: %clang -fsanitize=shadowbound -O1 -fno-vectorize -S -emit-llvm -o - %s 2>/dev/null | FileCheck %s --check-prefix=NOOPT

long sum(int *p, int n) {
  long s = 0;
  for (int i = 0; i < n; i++)
    s += p[i];
  return s;
}

// With the optimization: the last-iteration index (n-1) is precomputed, one
// bound check + abort is emitted before the loop, and the loop latch that
// follows has NO further check (the per-iteration check is gone).
// OPT-LABEL: @sum(
// OPT: add nsw i64 %{{[0-9a-z.]+}}, -1
// OPT: call void @__shadowbound_abort()
// OPT: br i1 %{{.+}}, label %{{.+}}, label %{{.+}}, !llvm.loop
// OPT-NOT: call void @__shadowbound_abort()

// Without it: a per-iteration check remains (baseline behaviour).
// NOOPT-LABEL: @sum(
// NOOPT: call void @__shadowbound_abort()
