// BUG 07: redundant-check elimination trusts post-dominating checks.
//
// Where: OverflowDefense.cpp, dependencyOptimizeForGep() /
//        dependencyOptimizeForBc() (`DT.dominates(J, I) || PDT.dominates(J, I)`).
//
// Check I is removed when a check J on the same base with a larger offset
// post-dominates it. A post-dominating check runs AFTER I's pointer has
// already been dereferenced, and never runs if the program exits, longjmps
// or throws in between. So the access through I happens unchecked.
//
// Expected: both stores in two_writes() are preceded by their own check.
//
// RUN: %odef_ir %s 2>/dev/null | FileCheck %s

void log_progress(void);

// CHECK-LABEL: @two_writes(
// CHECK: call void @__odef_abort()
// CHECK: store i8 1
// CHECK: call void @log_progress()
// CHECK: call void @__odef_abort()
// CHECK: store i8 2
void two_writes(char *p) {
  p[100] = 1;
  log_progress();
  p[200] = 2;
}

// CHECK-LABEL: @odef.module_ctor(
