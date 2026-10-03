// BUG 12: isAccessMember() treats any GEP starting with index 0 on a struct as in bounds.
//
// Where: OverflowDefense.cpp, isAccessMember() / structPointerOptimizae().
//
// Every GEP whose source element type is a fixed-size struct or array and
// whose first index is the constant 0 is dropped, whatever the remaining
// indices are. That is only sound for constant field indices. A variable
// array index inside the struct (`s->arr[i]`) can point anywhere, including
// far past the end of the heap object.
//
// Expected: set() checks s->arr[i]; get_n() (constant field) needs no check.
//
// RUN: %shadowbound_ir %s 2>/dev/null | FileCheck %s

struct S {
  int n;
  int arr[4];
};

// CHECK-LABEL: @set(
// CHECK: call void @__shadowbound_abort()
void set(struct S *s, long i) { s->arr[i] = 1; }

// CHECK-LABEL: @get_n(
// CHECK-NOT: call void @__shadowbound_abort()
int get_n(struct S *s) { return s->n; }

// CHECK-LABEL: @shadowbound.module_ctor(
