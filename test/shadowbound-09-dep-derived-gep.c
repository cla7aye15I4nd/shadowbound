// BUG 09: a check is dropped because a GEP derived from it is checked later.
//
// Where: OverflowDefense.cpp, dependencyOptimizeForGep(), the
//        `J->getPointerOperand() == I` case.
//
// If J = gep I, k with all indices k >= 0 (here k = 64) and J post-dominates I, I is
// dropped. But J >= Begin does not imply I >= Begin, and I is dereferenced
// before J's check runs (see BUG 07).
//
// Expected: the first store, through q, is preceded by a check.
//
// RUN: %shadowbound_ir %s 2>/dev/null | FileCheck %s

void log_progress(void);

// CHECK-LABEL: @f(
// CHECK: call void @__shadowbound_abort()
// CHECK: store i8 1
void f(char *p, long i) {
  char *q = p + i;
  *q = 1;
  log_progress();
  q[64] = 2;
}

// CHECK-LABEL: @shadowbound.module_ctor(
