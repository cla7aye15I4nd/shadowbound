// BUG 24: __attribute__((no_sanitize("shadowbound"))) is ignored.
//
// Where: clang CodeGen (no function attribute is attached) and
//        OverflowDefense::sanitizeFunction() (no attribute is checked).
//
// Clang accepts the attribute, but nothing tells the pass about it, so the
// function is instrumented anyway. There is no way to exclude one function
// short of the -odef-whitelist file.
//
// Expected: skipped() has no check; checked() keeps its check.
//
// RUN: %odef_ir %s 2>/dev/null | FileCheck %s

// CHECK-LABEL: @skipped(
// CHECK-NOT: call void @__shadowbound_abort()
__attribute__((no_sanitize("shadowbound"))) char skipped(char *p, long i) {
  return p[i];
}

// CHECK-LABEL: @checked(
// CHECK: call void @__shadowbound_abort()
char checked(char *p, long i) { return p[i]; }

// CHECK-LABEL: @odef.module_ctor(
