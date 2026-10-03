// BUG 08: two equal-size bitcasts of the same pointer eliminate each other.
//
// Where: OverflowDefense.cpp, dependencyOptimizeForBc().
//
// A bitcast I is dropped if another bitcast J of the same operand dominates
// or post-dominates it and is at least as large. Unlike the GEP version,
// nothing records that J itself was dropped. With two equal-size casts A and
// B in one block, B post-dominates A (so A is dropped) and A dominates B (so
// B is dropped): neither is checked.
//
// Expected: both() keeps at least one cast check.
//
// RUN: %odef_ir %s 2>/dev/null | FileCheck %s

struct A {
  char x[64];
};
struct B {
  char y[64];
};
int use_a(struct A *a);
int use_b(struct B *b);

// CHECK-LABEL: @both(
// CHECK: call void @__shadowbound_abort()
int both(void *p) { return use_a((struct A *)p) + use_b((struct B *)p); }

// CHECK-LABEL: @odef.module_ctor(
