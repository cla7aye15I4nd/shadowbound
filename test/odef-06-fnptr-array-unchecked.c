// BUG 06: indexing into an array of function pointers gets no check.
//
// Where: OverflowDefense.cpp, isVirtualTableGep().
//
// The function is meant to skip C++ vtable lookups. But it matches any GEP
// whose base points to a function pointer whose first parameter is a struct
// pointer, which also describes an ordinary C callback table
// (`void (**tbl)(struct ctx *); tbl[i](c);`). Those GEPs are dropped, so an
// out-of-bounds index into a callback table is not detected.
//
// Expected: dispatch() checks tbl[i]; a real C++ virtual call (vtable loaded
// from the object) still needs no check.
//
// RUN: %odef_ir %s 2>/dev/null | FileCheck %s

struct ctx {
  int v;
};
typedef void (*handler_t)(struct ctx *);

// CHECK-LABEL: @dispatch(
// CHECK: call void @__odef_abort()
void dispatch(handler_t *tbl, long i, struct ctx *c) { tbl[i](c); }

// CHECK-LABEL: @odef.module_ctor(
